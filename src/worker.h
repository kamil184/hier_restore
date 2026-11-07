#pragma once

#include "types.h"

#include <memory>

namespace hier_restore {

struct HierRestoreWorker {
	RTLIL::Module *top;
	std::string sep;
	bool inside;
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

	HierRestoreWorker(RTLIL::Module *top, const std::string &sep, bool inside) : top(top), sep(sep), inside(inside), sigmap(top) {}

	// Instances and the names of signal bits
	Scope *add_scope(const Path &path, const std::string &flat_name, const std::string &module, const std::string &module_src);
	void collect_scopes();
	void check_flattened_with_scopes();
	std::pair<Scope *, std::string> wire_scope(RTLIL::Wire *wire);
	void collect_bits();
	ScopeSet instances_at(const SrcRange &r);
	Scope *name_scope(const std::string &name);
	Scope *scope_at(Path p);

	// Assigning cells to instances
	CellInfo describe(RTLIL::Cell *cell);
	void propagate();
	ScopeSet duplicates(const CellInfo &c);
	bool shared(const CellInfo &c);
	void set_result(CellInfo &c, CellStatus status, const ScopeSet &scopes);
	void place_inside();
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
