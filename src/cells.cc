#include "worker.h"

namespace hier_restore {

static bool sees_any(const ScopeSet &visible, const ScopeSet &scopes)
{
	return std::any_of(scopes.begin(), scopes.end(), [&](Scope *s) { return visible.count(s) > 0; });
}

static Scope *deepest(const ScopeSet &scopes)
{
	Scope *best = nullptr;
	for (auto s : scopes)
		if (best == nullptr || s->path.size() > best->path.size())
			best = s;
	return best;
}

// Places one cell. No step chooses among alternatives, each one can only widen the place, so the place
// is never wrong, at worst coarser than the truth.
CellInfo HierRestoreWorker::place(RTLIL::Cell *cell)
{
	CellInfo c;
	c.cell = cell;
	c.name = RTLIL::unescape_id(cell->name);

	// Step 1: every instance of the module each source location of the cell lies in. Of several instances
	// of one module none is chosen, all of them count.
	ScopeSet from_src;
	for (auto &r : parse_src(cell->get_string_attribute(ID::src)))
		for (auto s : instances_at(r))
			from_src.insert(s);
	if (from_src.empty()) {
		c.status = CellStatus::Unknown;
		c.scope = root;
		return c;
	}

	// Step 2: the source locations can be incomplete. A named net that none of these instances sees shows
	// that the cell also holds logic of an instance that sees it, the deepest one counts as well.
	ScopeSet found = from_src;
	for (auto &conn : cell->connections())
		for (auto bit : sigmap(conn.second)) {
			auto it = bit_visible.find(bit);
			if (it != bit_visible.end() && !sees_any(it->second, from_src))
				found.insert(deepest(it->second));
		}

	// Step 3: the innermost instance that contains all of them.
	Path p = (*found.begin())->path;
	for (auto s : found)
		p = common_ancestor(p, s->path);
	c.scope = scope_at(p);
	c.status = found.size() > 1 ? CellStatus::Ambiguous : CellStatus::Unique;
	return c;
}

} // namespace hier_restore
