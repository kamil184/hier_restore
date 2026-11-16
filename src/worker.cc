#include "worker.h"

#include <optional>

namespace hier_restore {

void HierRestoreWorker::run()
{
	collect_scopes();
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
		cells.push_back(place(cell));
	}
	// sorted only so that the report lists the cells in the same order on every run
	std::sort(cells.begin(), cells.end(), [](const CellInfo &x, const CellInfo &y) { return x.name < y.name; });

	for (auto &c : cells) {
		c.scope->cells++;
		// set_string_attribute() drops empty values, but the top module's path is the empty string
		c.cell->attributes[ID(hier_scope)] = RTLIL::Const(path_str(c.scope->path));
	}

	place_wires();
}

// A wire whose name still tells its instance stays there. Any other wire lies in the innermost instance
// that contains all cells connected to it, so that it can reach each of them.
void HierRestoreWorker::place_wires()
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

} // namespace hier_restore
