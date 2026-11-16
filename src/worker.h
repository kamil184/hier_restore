#pragma once

#include "types.h"

namespace hier_restore {

struct HierRestoreWorker {
	RTLIL::Module *top;
	std::string sep;
	SigMap sigmap;

	// Every instance, ordered by path, so that every result is independent of the order of the design.
	std::map<Path, Scope> scopes;
	Scope *root = nullptr;

	// Scopes in which a signal bit is visible: the scope a name of the bit belongs to and its parent, since
	// a port of an instance is also a net of the instantiating module.
	dict<RTLIL::SigBit, ScopeSet> bit_visible;

	std::vector<CellInfo> cells;

	HierRestoreWorker(RTLIL::Module *top, const std::string &sep) : top(top), sep(sep), sigmap(top) {}

	void run();

	// Preparation: the instances and where the names of signal bits are visible
	void collect_scopes();
	void check_flattened_with_scopes();
	std::pair<Scope *, std::string> wire_scope(RTLIL::Wire *wire);
	void collect_bits();
	ScopeSet instances_at(const SrcRange &r);
	Scope *scope_at(Path p);

	// Placing cells and wires
	CellInfo place(RTLIL::Cell *cell);
	void place_wires();

	// Log and JSON report
	void log_tree(Scope *s, int depth);
	void log_summary();
	void write_report(const std::string &filename);
};

} // namespace hier_restore
