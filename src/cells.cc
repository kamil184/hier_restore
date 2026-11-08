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
		if (by_path.count(p))
			c.groups.push_back({by_path.at(p)});
	}
	for (auto &r : parse_src(cell->get_string_attribute(ID::src))) {
		ScopeSet alts = instances_at(r);
		if (!alts.empty() && std::find(c.groups.begin(), c.groups.end(), alts) == c.groups.end())
			c.groups.push_back(alts);
	}

	for (auto &conn : cell->connections())
		for (auto bit : sigmap(conn.second)) {
			if (bit.wire == nullptr)
				continue;
			auto it = bit_visible.find(bit);
			if (it != bit_visible.end()) {
				c.visible.push_back(&it->second);
				c.named.push_back(bit);
			} else
				c.unnamed.push_back(bit);
		}

	c.any = c.groups.empty() && c.visible.empty();
	if (!c.any) {
		c.domain = c.groups.empty() ? all_scopes : c.groups.front();
		for (auto &g : c.groups)
			c.domain = intersect(c.domain, g);
		// Keep the instances that need the fewest port crossings to see the named nets, see port_cost().
		std::map<Scope *, int, ByPath> total;
		for (auto s : c.domain) {
			int sum = 0;
			for (auto bit : c.named) {
				int cost = port_cost(s, bit_named.at(bit));
				if (cost < 0) {
					sum = -1;
					break;
				}
				sum += cost;
			}
			if (sum >= 0)
				total[s] = sum;
		}
		int best = INT_MAX;
		for (auto &kv : total)
			best = std::min(best, kv.second);
		c.domain.clear();
		for (auto &kv : total)
			if (kv.second == best)
				c.domain.insert(kv.first);
	}
	return c;
}

// How many port crossings it takes for a net named in the given instances to be seen in 's': none in
// an instance it is named in, one in the parent of such an instance, whose net the named port is, and
// -1 if 's' cannot see the net.
int HierRestoreWorker::port_cost(Scope *s, const ScopeSet &named)
{
	if (named.count(s))
		return 0;
	for (auto n : named)
		if (n->parent == s)
			return 1;
	return -1;
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
					// neighbours holding logic of several instances constrain nothing
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
			cells[u.first].any = false;
			cells[u.first].domain = u.second;
			changed = true;
		}
	}
}

// Places the cell in the innermost instance containing all given ones; with several, it is ambiguous.
void HierRestoreWorker::set_result(CellInfo &c, const ScopeSet &scopes)
{
	Path p = (*scopes.begin())->path;
	for (auto s : scopes)
		p = common_ancestor(p, s->path);
	c.scope = scope_at(p);
	c.status = scopes.size() > 1 ? CellStatus::Ambiguous : CellStatus::Unique;
}

// No single instance fits, so synthesis merged logic of several: collect the instances the pieces of
// the cell come from.
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
	set_result(c, all);
}

void HierRestoreWorker::resolve(CellInfo &c)
{
	if (c.any) {
		c.status = CellStatus::Unknown;
		c.scope = root;
	} else if (c.domain.empty())
		resolve_merged(c);
	else
		set_result(c, c.domain);
}

} // namespace hier_restore
