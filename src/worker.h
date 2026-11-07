#pragma once

#include "types.h"

#include <memory>

namespace hier_restore {

struct HierRestoreWorker {
	RTLIL::Module *top;
	std::string sep;
	SigMap sigmap;

	std::vector<std::unique_ptr<Scope>> storage;
	std::map<Path, Scope *> by_path;
	Scope *root = nullptr;
	ScopeSet all_scopes;

	// Scopes in which a signal bit is visible: the scope a name of the bit belongs to and its parent, since
	// a port of an instance is also a net of the instantiating module.
	dict<RTLIL::SigBit, ScopeSet> bit_visible;
	// Scopes a signal bit lies in for sure: the scope of its name, or where it has names in several
	// scopes, all scopes it is visible in.
	dict<RTLIL::SigBit, ScopeSet> bit_named;

	std::vector<CellInfo> cells;
	// Cells connected to each wire without a reliable name, by index into 'cells'.
	dict<RTLIL::SigBit, std::vector<int>> users;

	HierRestoreWorker(RTLIL::Module *top, const std::string &sep) : top(top), sep(sep), sigmap(top) {}

	// Instances and the names of signal bits
	Scope *add_scope(const Path &path, const std::string &flat_name, const std::string &module, const std::string &module_src);
	void collect_scopes();
	void check_flattened_with_scopes();
	std::pair<Scope *, std::string> wire_scope(RTLIL::Wire *wire);
	void collect_bits();
	ScopeSet instances_at(const SrcRange &r);
	Scope *scope_at(Path p);

	// Assigning cells to instances
	int port_cost(Scope *s, const ScopeSet &named);
	CellInfo describe(RTLIL::Cell *cell);
	void propagate();
	void set_result(CellInfo &c, const ScopeSet &scopes);
	void resolve_merged(CellInfo &c);
	void resolve(CellInfo &c);

	// Writing the attributes
	void run();
	void place_wires();

	// Log and JSON report
	void log_tree(Scope *s, int depth);
	void log_summary();
	void write_report(const std::string &filename);
};

} // namespace hier_restore
