#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#include <algorithm>
#include <climits>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

// Instance names from the top module down, empty for the top module itself.
using Path = std::vector<std::string>;

static bool is_within(const Path &inner, const Path &outer)
{
	return inner.size() >= outer.size() && std::equal(outer.begin(), outer.end(), inner.begin());
}

static Path common_ancestor(const Path &a, const Path &b)
{
	size_t n = 0;
	while (n < a.size() && n < b.size() && a[n] == b[n])
		n++;
	return Path(a.begin(), a.begin() + n);
}

// Same convention as the 'hdlname' attribute: instance names joined by spaces.
static std::string path_str(const Path &p)
{
	std::string s;
	for (auto &name : p)
		s += (s.empty() ? "" : " ") + name;
	return s;
}

struct SrcRange {
	std::string file;
	int l1 = 0, c1 = 0, l2 = 0, c2 = INT_MAX;

	bool contains(const SrcRange &o) const
	{
		return file == o.file && std::tie(l1, c1) <= std::tie(o.l1, o.c1) && std::tie(o.l2, o.c2) <= std::tie(l2, c2);
	}

	bool operator==(const SrcRange &o) const { return std::tie(file, l1, c1, l2, c2) == std::tie(o.file, o.l1, o.c1, o.l2, o.c2); }

	long size() const { return (long)(l2 - l1) * 100000 + (c2 == INT_MAX ? 99999 : c2 - c1); }
};

// Parses one "file:l1.c1-l2.c2", "file:l.c", "file:l1-l2" or "file:l" entry.
static bool parse_range(const std::string &s, SrcRange &r)
{
	size_t colon = s.rfind(':');
	if (colon == std::string::npos || colon == 0)
		return false;
	r.file = s.substr(0, colon);
	const char *pos = s.c_str() + colon + 1;
	char tail;
	if (sscanf(pos, "%d.%d-%d.%d%c", &r.l1, &r.c1, &r.l2, &r.c2, &tail) == 4)
		return true;
	if (sscanf(pos, "%d.%d%c", &r.l1, &r.c1, &tail) == 2) {
		r.l2 = r.l1, r.c2 = r.c1;
		return true;
	}
	r.c1 = 0, r.c2 = INT_MAX;
	if (sscanf(pos, "%d-%d%c", &r.l1, &r.l2, &tail) == 2)
		return true;
	if (sscanf(pos, "%d%c", &r.l1, &tail) == 1) {
		r.l2 = r.l1;
		return true;
	}
	return false;
}

// yosys joins the origins of merged objects with '|'
static std::vector<SrcRange> parse_src(const std::string &src)
{
	std::vector<SrcRange> out;
	for (auto &part : split_tokens(src, "|")) {
		SrcRange r;
		if (parse_range(part, r))
			out.push_back(r);
	}
	return out;
}

enum class CellStatus { Unique, Inferred, Merged, Ambiguous, Unknown };

static const char *status_name(CellStatus s)
{
	switch (s) {
	case CellStatus::Unique:
		return "unique";
	case CellStatus::Inferred:
		return "inferred";
	case CellStatus::Merged:
		return "merged";
	case CellStatus::Ambiguous:
		return "ambiguous";
	default:
		return "unknown";
	}
}

struct Scope {
	Path path;
	std::string flat_name; // prefix that flatten put in front of the names of this instance's objects
	std::string module;
	std::vector<SrcRange> module_src;
	RTLIL::Cell *info = nullptr; // the $scopeinfo cell, null for the top module
	Scope *parent = nullptr;
	std::map<CellStatus, int> own;
	int mentions = 0; // merged or ambiguous cells listing this scope as a candidate

	const char *status() const
	{
		int placed = 0;
		for (auto s : {CellStatus::Unique, CellStatus::Inferred})
			placed += own.count(s) ? own.at(s) : 0;
		if (placed > 0)
			return mentions > 0 ? "partial" : "complete";
		return mentions > 0 ? "absorbed" : "empty";
	}
};

// Orders scopes by path so that every result is independent of memory layout.
struct ByPath {
	bool operator()(const Scope *a, const Scope *b) const { return a->path < b->path; }
};
using ScopeSet = std::set<Scope *, ByPath>;

static ScopeSet intersect(const ScopeSet &a, const ScopeSet &b)
{
	ScopeSet out;
	std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::inserter(out, out.end()), ByPath());
	return out;
}

struct CellInfo {
	RTLIL::Cell *cell;
	std::string name;

	// Each group lists the instances one origin of the cell may lie in: several entries are several
	// instances of the same module, several groups are logic merged from several places.
	std::vector<ScopeSet> groups;
	std::vector<const ScopeSet *> visible; // per named connected bit, the scopes that can see it
	std::vector<RTLIL::SigBit> unnamed;    // connected bits without a reliable name
	std::vector<RTLIL::SigBit> outputs;
	std::set<std::string> evidence;

	// Instances the cell can lie in as a whole. 'any' means nothing restricts it, an empty domain
	// means no single instance fits and the cell holds logic of several.
	bool any = false;
	ScopeSet domain;
	bool from_neighbours = false; // nothing but its neighbours restricted the domain

	CellStatus status = CellStatus::Unknown;
	Scope *scope = nullptr;
	std::vector<Scope *> candidates; // set for merged and ambiguous cells
};

static std::string json_str(const std::string &s)
{
	std::string o = "\"";
	for (unsigned char c : s) {
		if (c == '"' || c == '\\')
			o += std::string("\\") + (char)c;
		else if (c < 0x20)
			o += stringf("\\u%04x", c);
		else
			o += c;
	}
	return o + "\"";
}

static std::string json_path(const Path &p)
{
	std::string o = "[";
	for (size_t i = 0; i < p.size(); i++)
		o += (i ? ", " : "") + json_str(p[i]);
	return o + "]";
}

static std::string join(const std::set<std::string> &items, const std::string &sep)
{
	std::string o;
	for (auto &i : items)
		o += (o.empty() ? "" : sep) + i;
	return o;
}

struct HierRestoreWorker {
	RTLIL::Module *top;
	std::string sep;
	SigMap sigmap;

	std::vector<std::unique_ptr<Scope>> storage;
	std::map<Path, Scope *> by_path;
	dict<std::string, Scope *> by_flat_name;
	Scope *root = nullptr;
	ScopeSet all_scopes;

	// Scopes in which a signal bit is visible: the scope a name of the bit belongs to and its parent, since
	// a port of an instance is also a net of the instantiating module.
	dict<RTLIL::SigBit, ScopeSet> bit_visible;
	// The names of a bit as (scope, local name) pairs.
	dict<RTLIL::SigBit, std::vector<std::pair<Scope *, std::string>>> bit_names;

	std::vector<CellInfo> cells;
	// Cells connected to each wire without a reliable name, by index into 'cells'.
	dict<RTLIL::SigBit, std::vector<int>> users;

	HierRestoreWorker(RTLIL::Module *top, const std::string &sep) : top(top), sep(sep), sigmap(top) {}

	Scope *add_scope(const Path &path, const std::string &flat_name, const std::string &module, const std::string &module_src)
	{
		storage.emplace_back(new Scope);
		Scope *s = storage.back().get();
		s->path = path;
		s->flat_name = flat_name;
		s->module = module;
		s->module_src = parse_src(module_src);
		by_path[path] = s;
		all_scopes.insert(s);
		if (!flat_name.empty())
			by_flat_name[flat_name] = s;
		return s;
	}

	void collect_scopes()
	{
		root = add_scope({}, "", log_id(top), top->get_string_attribute(ID::src));

		for (auto cell : top->cells()) {
			if (cell->type != ID($scopeinfo) || !cell->hasParam(ID::TYPE) || cell->getParam(ID::TYPE).decode_string() != "module")
				continue;
			std::string flat_name = RTLIL::unescape_id(cell->name);
			// flatten only sets hdlname on nested instances; a direct child of the top module is named by its instance name
			std::string hdlname = cell->get_string_attribute(ID::hdlname);
			Path path = hdlname.empty() ? Path{flat_name} : split_tokens(hdlname, " ");
			std::string module = cell->get_string_attribute(ID(module_hdlname));
			if (module.empty())
				module = cell->get_string_attribute(ID(module));
			Scope *s = add_scope(path, flat_name, module, cell->get_string_attribute(ID(module_src)));
			s->info = cell;
		}

		for (auto &s : storage) {
			if (s.get() == root)
				continue;
			Path p = s->path;
			do {
				p.pop_back();
			} while (!p.empty() && !by_path.count(p));
			s->parent = by_path.at(p);
		}
	}

	// The scope and local name of a wire, if its name still tells them reliably.
	std::pair<Scope *, std::string> wire_scope(RTLIL::Wire *wire)
	{
		std::string hdlname = wire->get_string_attribute(ID::hdlname);
		if (!hdlname.empty()) {
			Path p = split_tokens(hdlname, " ");
			std::string local = p.back();
			p.pop_back();
			auto it = by_path.find(p);
			return {it == by_path.end() ? nullptr : it->second, local};
		}
		// Wires of the top module keep their plain name and the source location inside the top module.
		// Names made up by synthesis (autoname, abc) carry no or foreign source locations and are skipped.
		std::string name = RTLIL::unescape_id(wire->name);
		if (!wire->name.isPublic() || name.find(sep) != std::string::npos)
			return {nullptr, ""};
		for (auto &r : parse_src(wire->get_string_attribute(ID::src)))
			for (auto &def : root->module_src)
				if (def.contains(r))
					return {root, name};
		return {nullptr, ""};
	}

	void collect_bits()
	{
		for (auto wire : top->wires()) {
			auto [s, local] = wire_scope(wire);
			if (s == nullptr)
				continue;
			for (auto bit : sigmap(wire)) {
				if (bit.wire == nullptr)
					continue;
				bit_visible[bit].insert(s);
				if (s->parent != nullptr)
					bit_visible[bit].insert(s->parent);
				bit_names[bit].emplace_back(s, local);
			}
		}
	}

	// All instances of the innermost module definition that contains the given source range.
	ScopeSet instances_at(const SrcRange &r)
	{
		const SrcRange *best = nullptr;
		for (auto &s : storage)
			for (auto &def : s->module_src)
				if (def.contains(r) && (best == nullptr || def.size() < best->size()))
					best = &def;
		ScopeSet out;
		if (best == nullptr)
			return out;
		for (auto &s : storage)
			for (auto &def : s->module_src)
				if (def == *best)
					out.insert(s.get());
		return out;
	}

	// The scope whose flattened name is the longest prefix of the cell name. synth scripts name cells after
	// a connected wire, which may as well lie on the other side of an instance boundary, so this is weak.
	Scope *name_scope(const std::string &name)
	{
		Scope *best = nullptr;
		for (size_t pos = name.find(sep); pos != std::string::npos; pos = name.find(sep, pos + sep.size())) {
			auto it = by_flat_name.find(name.substr(0, pos));
			if (it != by_flat_name.end())
				best = it->second;
		}
		return best;
	}

	CellInfo describe(RTLIL::Cell *cell)
	{
		CellInfo c;
		c.cell = cell;
		c.name = RTLIL::unescape_id(cell->name);

		std::string hdlname = cell->get_string_attribute(ID::hdlname);
		if (!hdlname.empty()) {
			Path p = split_tokens(hdlname, " ");
			p.pop_back();
			if (by_path.count(p)) {
				c.groups.push_back({by_path.at(p)});
				c.evidence.insert("hdlname");
			}
		}
		for (auto &r : parse_src(cell->get_string_attribute(ID::src))) {
			ScopeSet alts = instances_at(r);
			if (alts.empty() || std::find(c.groups.begin(), c.groups.end(), alts) != c.groups.end())
				continue;
			c.groups.push_back(alts);
			c.evidence.insert("src");
		}

		for (auto &conn : cell->connections()) {
			bool output = cell->output(conn.first);
			for (auto bit : sigmap(conn.second)) {
				if (bit.wire == nullptr)
					continue;
				if (output)
					c.outputs.push_back(bit);
				auto it = bit_visible.find(bit);
				if (it != bit_visible.end())
					c.visible.push_back(&it->second);
				else
					c.unnamed.push_back(bit);
			}
		}

		c.any = c.groups.empty() && c.visible.empty();
		if (!c.any) {
			c.domain = c.groups.empty() ? all_scopes : c.groups.front();
			for (auto &g : c.groups)
				c.domain = intersect(c.domain, g);
			for (auto v : c.visible)
				c.domain = intersect(c.domain, *v);
			if (c.groups.empty() || c.groups.front().size() > 1)
				c.evidence.insert("net");
		}
		return c;
	}

	// A wire without a reliable name is local to one instance, so cells sharing it lie in the same instance.
	// Narrows each open domain to the instances most neighbours agree with, until nothing changes. If all
	// neighbours agree this is the intersection of their domains; a neighbour behind a boundary wire that
	// lost its name is outvoted instead of blocking the others. Votes are counted against the unchanged
	// domains, so the result does not depend on the order of cells and wires.
	void propagate()
	{
		for (int i = 0; i < (int)cells.size(); i++)
			for (auto bit : cells[i].unnamed)
				users[bit].push_back(i);

		auto open = [&](const CellInfo &c) { return c.any || c.domain.size() > 1; };

		for (bool changed = true; changed;) {
			changed = false;
			std::vector<std::pair<int, ScopeSet>> updates;
			for (int i = 0; i < (int)cells.size(); i++) {
				CellInfo &c = cells[i];
				if (!open(c))
					continue;
				std::set<int> neighbours;
				for (auto bit : c.unnamed)
					for (int j : users.at(bit))
						// merged neighbours span several instances and constrain nothing
						if (j != i && !cells[j].any && !cells[j].domain.empty())
							neighbours.insert(j);
				std::map<Scope *, int, ByPath> votes;
				for (int j : neighbours)
					for (auto s : cells[j].domain)
						if (c.any || c.domain.count(s))
							votes[s]++;
				int best = 0;
				for (auto &kv : votes)
					best = std::max(best, kv.second);
				if (best == 0)
					continue;
				ScopeSet domain;
				for (auto &kv : votes)
					if (kv.second == best)
						domain.insert(kv.first);
				if (c.any || domain != c.domain)
					updates.emplace_back(i, domain);
			}
			for (auto &u : updates) {
				if (cells[u.first].any)
					cells[u.first].from_neighbours = true;
				cells[u.first].any = false;
				cells[u.first].domain = u.second;
				cells[u.first].evidence.insert("net");
				changed = true;
			}
		}
	}

	// Instances of the same module whose equally named local wire this cell drives: synthesis kept one
	// copy of logic that existed in each of them.
	ScopeSet duplicates(const CellInfo &c)
	{
		ScopeSet out;
		for (auto bit : c.outputs) {
			auto it = bit_names.find(bit);
			if (it == bit_names.end())
				continue;
			for (auto &a : it->second)
				for (auto &b : it->second)
					if (a.first != b.first && a.second == b.second && a.first->module == b.first->module &&
					    c.domain.count(a.first) && c.domain.count(b.first))
						out.insert(a.first);
		}
		return out;
	}

	// A cell known only through its neighbours, such as a constant driver, that is wired to cells placed in
	// each of its remaining instances: synthesis shared one cell between those instances.
	bool shared(const CellInfo &c)
	{
		if (!c.from_neighbours)
			return false;
		ScopeSet seen;
		for (auto bit : c.unnamed)
			for (int j : users.at(bit))
				if (!cells[j].any && cells[j].domain.size() == 1)
					seen.insert(*cells[j].domain.begin());
		return std::includes(seen.begin(), seen.end(), c.domain.begin(), c.domain.end(), ByPath());
	}

	Scope *scope_at(Path p)
	{
		while (!by_path.count(p))
			p.pop_back();
		return by_path.at(p);
	}

	void set_result(CellInfo &c, CellStatus status, const ScopeSet &scopes)
	{
		c.status = status;
		Path p = (*scopes.begin())->path;
		for (auto s : scopes)
			p = common_ancestor(p, s->path);
		c.scope = scope_at(p);
		c.candidates.clear();
		if (scopes.size() > 1)
			c.candidates.assign(scopes.begin(), scopes.end());
	}

	// No single instance fits: collect the instances the pieces of the cell come from.
	void resolve_merged(CellInfo &c)
	{
		auto sees = [](const ScopeSet *visible, const ScopeSet &scopes) {
			return std::any_of(scopes.begin(), scopes.end(), [&](Scope *s) { return visible->count(s) > 0; });
		};

		ScopeSet placed;
		std::vector<ScopeSet> open;
		for (auto &g : c.groups)
			if (g.size() == 1)
				placed.insert(*g.begin());
		for (auto &g : c.groups) {
			if (g.size() == 1)
				continue;
			ScopeSet fits;
			for (auto s : g) {
				ScopeSet with = placed;
				with.insert(s);
				if (std::all_of(c.visible.begin(), c.visible.end(), [&](const ScopeSet *v) { return sees(v, with); }))
					fits.insert(s);
			}
			if (fits.size() == 1)
				placed.insert(*fits.begin());
			else
				open.push_back(fits.empty() ? g : fits);
		}

		// Nets the placed instances cannot see belong to further instances the logic was merged from:
		// prefer the innermost enclosing instance that sees the net, else the innermost one at all. Every
		// net is judged against the instances placed so far, so the order of the nets does not matter.
		if (open.empty() && !placed.empty()) {
			const ScopeSet from_src = placed;
			Path lca = (*from_src.begin())->path;
			for (auto s : from_src)
				lca = common_ancestor(lca, s->path);
			for (auto v : c.visible) {
				if (sees(v, from_src))
					continue;
				Scope *add = nullptr;
				for (auto s : *v)
					if (is_within(lca, s->path) && (add == nullptr || s->path.size() > add->path.size()))
						add = s;
				if (add == nullptr)
					for (auto s : *v)
						if (add == nullptr || s->path.size() > add->path.size())
							add = s;
				placed.insert(add);
				c.evidence.insert("net");
			}
		}

		ScopeSet all = placed;
		for (auto &g : open)
			all.insert(g.begin(), g.end());
		if (all.empty()) {
			c.status = CellStatus::Unknown;
			c.scope = root;
			return;
		}
		set_result(c, open.empty() ? (all.size() > 1 ? CellStatus::Merged : CellStatus::Unique) : CellStatus::Ambiguous, all);
	}

	void resolve(CellInfo &c)
	{
		if (c.any) {
			if (Scope *s = name_scope(c.name)) {
				c.evidence.insert("name");
				set_result(c, CellStatus::Inferred, {s});
			} else {
				c.status = CellStatus::Unknown;
				c.scope = root;
			}
			return;
		}
		if (c.domain.empty()) {
			resolve_merged(c);
			return;
		}
		if (c.domain.size() == 1) {
			set_result(c, c.groups.empty() ? CellStatus::Inferred : CellStatus::Unique, c.domain);
			return;
		}
		// The cell name is no tie-breaker here: it follows whichever connected wire synthesis picked.
		ScopeSet dups = duplicates(c);
		if (dups.size() > 1) {
			c.evidence.insert("net");
			set_result(c, CellStatus::Merged, dups);
			return;
		}
		if (shared(c)) {
			set_result(c, CellStatus::Merged, c.domain);
			return;
		}
		set_result(c, CellStatus::Ambiguous, c.domain);
	}

	// Without $scopeinfo cells the instances are unknown, which only is fine if there never were any.
	void check_flattened_with_scopes()
	{
		if (storage.size() > 1)
			return;
		auto nested = [](RTLIL::AttrObject *obj) {
			return obj->get_string_attribute(ID::hdlname).find(' ') != std::string::npos;
		};
		bool lost = std::any_of(top->wires().begin(), top->wires().end(), nested) ||
			    std::any_of(top->cells().begin(), top->cells().end(), nested);
		if (lost)
			log_cmd_error("%s was flattened without $scopeinfo cells (flatten -noscopeinfo), its instances cannot be "
				      "recovered.\n",
				      log_id(top));
	}

	void run()
	{
		collect_scopes();
		check_flattened_with_scopes();
		collect_bits();

		for (auto cell : top->cells()) {
			if (cell->type == ID($scopeinfo))
				continue;
			RTLIL::Module *def = top->design->module(cell->type);
			if (def != nullptr && !def->get_blackbox_attribute()) {
				log_warning("Cell %s instantiates module %s, the design is not fully flattened.\n", log_id(cell),
					    log_id(def));
				continue;
			}
			cells.push_back(describe(cell));
		}
		std::sort(cells.begin(), cells.end(), [](const CellInfo &x, const CellInfo &y) { return x.name < y.name; });

		propagate();
		for (auto &c : cells)
			resolve(c);

		for (auto &c : cells) {
			c.scope->own[c.status]++;
			for (auto s : c.candidates)
				s->mentions++;
		}

		for (auto &c : cells) {
			// set_string_attribute() drops empty values, but the top module's path is the empty string
			c.cell->attributes[ID(hier_scope)] = RTLIL::Const(path_str(c.scope->path));
			c.cell->set_string_attribute(ID(hier_status), status_name(c.status));
			c.cell->attributes.erase(ID(hier_candidates));
			if (!c.candidates.empty()) {
				// & joins the instances a merged cell belongs to all of, | those an ambiguous cell belongs to one of
				const char *sep = c.status == CellStatus::Merged ? "&" : "|";
				std::string s;
				for (size_t i = 0; i < c.candidates.size(); i++)
					s += (i ? sep : "") + path_str(c.candidates[i]->path);
				c.cell->set_string_attribute(ID(hier_candidates), s);
			}
			c.cell->set_string_attribute(ID(hier_evidence), join(c.evidence, ","));
		}
		for (auto &s : storage)
			if (s->info != nullptr)
				s->info->set_string_attribute(ID(hier_status), s->status());

		place_wires();
	}

	// A wire whose name still tells its instance stays there. Any other wire lies in the innermost instance
	// that contains all cells connected to it, so that it can reach each of them.
	void place_wires()
	{
		dict<RTLIL::SigBit, std::vector<Scope *>> bit_cells;
		for (auto &c : cells)
			for (auto &conn : c.cell->connections())
				for (auto bit : sigmap(conn.second))
					if (bit.wire != nullptr)
						bit_cells[bit].push_back(c.scope);

		for (auto wire : top->wires()) {
			Scope *s = wire_scope(wire).first;
			if (s == nullptr) {
				std::optional<Path> lca;
				for (auto bit : sigmap(wire)) {
					auto it = bit_cells.find(bit);
					if (it == bit_cells.end())
						continue;
					for (auto cs : it->second)
						lca = lca ? common_ancestor(*lca, cs->path) : cs->path;
				}
				s = lca ? scope_at(*lca) : root;
			}
			wire->attributes[ID(hier_scope)] = RTLIL::Const(path_str(s->path));
		}
	}

	void log_tree(Scope *s, int depth)
	{
		log("  %*s%s  [%s]  %s", depth * 2, "", s == root ? "<top>" : s->flat_name.c_str(), s->module.c_str(), s->status());
		for (auto &kv : s->own)
			log("  %s=%d", status_name(kv.first), kv.second);
		if (s->mentions > 0)
			log("  mentioned=%d", s->mentions);
		log("\n");
		for (auto c : all_scopes)
			if (c->parent == s)
				log_tree(c, depth + 1);
	}

	void log_summary()
	{
		std::map<CellStatus, int> total;
		for (auto &c : cells)
			total[c.status]++;
		log("\nRecovered hierarchy of %s (%d cells):\n", log_id(top), (int)cells.size());
		log_tree(root, 0);
		log("\nCells by status:");
		for (auto &kv : total)
			log("  %s=%d", status_name(kv.first), kv.second);
		log("\n");
	}

	void write_report(const std::string &filename)
	{
		std::ofstream os(filename);
		if (!os)
			log_cmd_error("Cannot open %s for writing.\n", filename.c_str());

		os << "{\n  \"top\": " << json_str(log_id(top)) << ",\n  \"scopes\": [";
		bool first = true;
		for (auto s : all_scopes) {
			os << (first ? "\n" : ",\n") << "    {\"path\": " << json_path(s->path) << ", \"module\": " << json_str(s->module)
			   << ", \"status\": " << json_str(s->status()) << ", \"cells\": {";
			bool first_count = true;
			for (auto &kv : s->own) {
				os << (first_count ? "" : ", ") << json_str(status_name(kv.first)) << ": " << kv.second;
				first_count = false;
			}
			os << "}, \"mentions\": " << s->mentions << "}";
			first = false;
		}
		os << "\n  ],\n  \"cells\": [";
		first = true;
		for (auto &c : cells) {
			os << (first ? "\n" : ",\n") << "    {\"name\": " << json_str(c.name) << ", \"type\": " << json_str(RTLIL::unescape_id(c.cell->type))
			   << ", \"status\": " << json_str(status_name(c.status)) << ", \"scope\": " << json_path(c.scope->path) << ", \"candidates\": [";
			for (size_t i = 0; i < c.candidates.size(); i++)
				os << (i ? ", " : "") << json_path(c.candidates[i]->path);
			os << "], \"evidence\": [";
			bool first_ev = true;
			for (auto &e : c.evidence) {
				os << (first_ev ? "" : ", ") << json_str(e);
				first_ev = false;
			}
			os << "]}";
			first = false;
		}
		os << "\n  ]\n}\n";
	}
};

struct HierRestorePass : public Pass {
	HierRestorePass() : Pass("hier_restore", "recover the module hierarchy of a synthesized, flattened design") {}

	void help() override
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    hier_restore [options]\n");
		log("\n");
		log("Assigns every cell of the flattened top module to the module instance it came\n");
		log("from. Run it after synthesis; the design must have been flattened with\n");
		log("$scopeinfo cells, which 'flatten' and the synth_* scripts create by default.\n");
		log("The netlist itself is not changed, only these attributes are set:\n");
		log("\n");
		log("    hier_scope        on every cell: the instance path, written like 'hdlname'\n");
		log("                      (instance names separated by spaces, empty for the top)\n");
		log("    hier_status       on every cell, one of:\n");
		log("                        unique     the source location and the connected nets\n");
		log("                                   point to exactly one instance\n");
		log("                        inferred   no source location; placed by the connected\n");
		log("                                   nets, or by the cell name if no net is known\n");
		log("                        merged     synthesis combined logic of several\n");
		log("                                   instances into this cell\n");
		log("                        ambiguous  the cell comes from one of several\n");
		log("                                   instances, which one is unknown\n");
		log("                        unknown    no usable information, placed in the top\n");
		log("                      merged and ambiguous cells are placed in the innermost\n");
		log("                      instance that contains all candidates.\n");
		log("    hier_candidates   on merged and ambiguous cells: the instance paths\n");
		log("                      involved, separated by '&' on merged cells (all of\n");
		log("                      them) and by '|' on ambiguous cells (one of them)\n");
		log("    hier_evidence     on every cell: what the result is based on, a comma\n");
		log("                      separated subset of hdlname, src, net, name\n");
		log("    hier_scope        on every wire: the instance its name belongs to, or the\n");
		log("                      innermost instance that contains all connected cells\n");
		log("    hier_status       on every $scopeinfo cell, one of:\n");
		log("                        complete   cells were placed here, none is shared\n");
		log("                        partial    some logic was merged with other instances\n");
		log("                        absorbed   all known logic was merged into other cells\n");
		log("                        empty      no cell left, the logic was optimized away\n");
		log("                                   or merged without a trace\n");
		log("\n");
		log("    -o <file>\n");
		log("        also write the result as a JSON report\n");
		log("\n");
		log("    -sep <sep>\n");
		log("        hierarchy separator used by flatten (default: the value of the\n");
		log("        'flatten.separator' scratchpad variable, or '.')\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		log_header(design, "Executing HIER_RESTORE pass.\n");

		std::string sep = design->scratchpad_get_string("flatten.separator", "."), outfile;
		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++) {
			if (args[argidx] == "-sep" && argidx + 1 < args.size()) {
				sep = args[++argidx];
				continue;
			}
			if (args[argidx] == "-o" && argidx + 1 < args.size()) {
				outfile = args[++argidx];
				continue;
			}
			break;
		}
		extra_args(args, argidx, design);

		if (sep.empty())
			log_cmd_error("The hierarchy separator must not be empty.\n");
		RTLIL::Module *top = design->top_module();
		if (top == nullptr)
			log_cmd_error("No top module found.\n");

		HierRestoreWorker worker(top, sep);
		worker.run();
		worker.log_summary();
		if (!outfile.empty())
			worker.write_report(outfile);
	}
} HierRestorePass;

PRIVATE_NAMESPACE_END
