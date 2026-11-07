#include "worker.h"

namespace hier_restore {

CellInfo HierRestoreWorker::describe(RTLIL::Cell *cell)
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
void HierRestoreWorker::propagate()
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
ScopeSet HierRestoreWorker::duplicates(const CellInfo &c)
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
bool HierRestoreWorker::shared(const CellInfo &c)
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

void HierRestoreWorker::set_result(CellInfo &c, CellStatus status, const ScopeSet &scopes)
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

// With -inside, merged and ambiguous cells are drawn in one of their candidates instead of the instance
// containing all of them. Cells joined by wires without a reliable name lie in one instance, so each such
// group with the same candidates makes one choice: the candidate most of their source locations point to,
// else the innermost one, else the one most of their names point to, else the first by path.
void HierRestoreWorker::place_inside()
{
	auto open = [](const CellInfo &c) { return c.status == CellStatus::Merged || c.status == CellStatus::Ambiguous; };
	std::vector<bool> done(cells.size());
	for (int i = 0; i < (int)cells.size(); i++) {
		if (done[i] || !open(cells[i]))
			continue;
		const std::vector<Scope *> &cands = cells[i].candidates;
		std::vector<int> group{i};
		done[i] = true;
		for (size_t g = 0; g < group.size(); g++)
			for (auto bit : cells[group[g]].unnamed)
				for (int j : users.at(bit))
					if (!done[j] && open(cells[j]) && cells[j].candidates == cands) {
						done[j] = true;
						group.push_back(j);
					}

		std::map<Scope *, std::pair<int, int>, ByPath> votes; // by source location, by name
		for (int m : group) {
			const CellInfo &c = cells[m];
			Scope *named = name_scope(c.name);
			for (auto s : cands) {
				if (std::any_of(c.groups.begin(), c.groups.end(), [&](const ScopeSet &g) { return g.count(s) > 0; }))
					votes[s].first++;
				votes[s].second += s == named;
			}
		}
		auto rank = [&](Scope *s) { return std::make_tuple(votes[s].first, s->path.size(), votes[s].second); };
		Scope *best = cands.front();
		for (auto s : cands)
			if (rank(s) > rank(best))
				best = s;
		for (int m : group)
			cells[m].scope = best;
	}
}

// No single instance fits: collect the instances the pieces of the cell come from.
void HierRestoreWorker::resolve_merged(CellInfo &c)
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

void HierRestoreWorker::resolve(CellInfo &c)
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

} // namespace hier_restore
