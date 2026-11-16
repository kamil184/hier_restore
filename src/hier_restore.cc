// hier_restore: a yosys pass that recovers the module hierarchy of a flattened, synthesized design.
//
// After 'flatten' and synthesis all cells lie in the top module. This pass writes into the attribute
// 'hier_scope' of every cell and wire the instance it comes from, for example "u_alu u_add". The netlist
// itself is not changed.
//
// It never guesses. Where the netlist does not prove the exact instance, a cell stays in an enclosing
// instance or in the top module: coarser than the truth, but never wrong.
//
// How it works:
//
//   Preparation
//     1. The tree of instances, from the $scopeinfo cells that flatten leaves for every instance.
//     2. For every bit of a named wire, the instances it is visible in: the instance its name belongs to
//        and, since a port is also a net of the instantiating module, the parent of that instance.
//
//   For every cell, independently of all other cells (see place_cell)
//     Step 1. Every instance of the module that a source location (src) of the cell lies in.
//     Step 2. For each named net of the cell that none of them sees, the deepest instance that sees it.
//     Step 3. The cell goes to the innermost instance that contains all of these.
//
//   For every wire (see place_wires)
//     A wire whose name tells its instance stays there, any other one goes to the innermost instance
//     that contains all its cells.

#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#include <climits>
#include <map>
#include <optional>
#include <set>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------------------

// The path of an instance: instance names from the top module down, for example {"u_alu", "u_add"}.
// The top module itself has the empty path.
using Path = std::vector<std::string>;

// The innermost instance that contains both: the longest common beginning of both paths.
static Path common_ancestor(const Path &a, const Path &b)
{
	size_t n = 0;
	while (n < a.size() && n < b.size() && a[n] == b[n])
		n++;
	return Path(a.begin(), a.begin() + n);
}

// A path as text, like the 'hdlname' attribute: instance names separated by spaces.
static std::string path_str(const Path &p)
{
	std::string s;
	for (auto &name : p)
		s += (s.empty() ? "" : " ") + name;
	return s;
}

// One source location, "file:line.column-line.column".
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

// Reads one "file:l1.c1-l2.c2", "file:l.c", "file:l1-l2" or "file:l" entry.
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

// All source locations of a src attribute; yosys joins those of merged objects with '|'.
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

// ---------------------------------------------------------------------------------------------------------
// The algorithm
// ---------------------------------------------------------------------------------------------------------

struct Instance {
	std::string name;                  // the name flatten gave it, for the log
	std::string module;                // the module it is an instance of
	std::vector<SrcRange> module_src;  // where that module is written in the source files
	Path parent;
	int cells = 0;                     // cells placed here
};

struct PlacedCell {
	RTLIL::Cell *cell;
	Path place;
};

struct HierRestore {
	RTLIL::Module *top;
	std::string sep;  // the hierarchy separator flatten used, usually "."
	SigMap sigmap;    // gives every net one representative bit, whatever wire it is seen through

	std::map<Path, Instance> instances;           // every instance; the top module has the empty path
	dict<RTLIL::SigBit, std::set<Path>> visible;  // for every bit of a named wire, where it is visible
	std::vector<PlacedCell> cells;

	HierRestore(RTLIL::Module *top, const std::string &sep) : top(top), sep(sep), sigmap(top) {}

	void run()
	{
		find_instances();
		find_visible_bits();

		for (auto cell : top->cells()) {
			if (cell->type == ID($scopeinfo))
				continue;
			RTLIL::Module *def = top->design->module(cell->type);
			if (def != nullptr && !def->get_blackbox_attribute()) {
				log_warning("Cell %s instantiates module %s, the design is not fully flattened.\n", log_id(cell),
					    log_id(def));
				continue;
			}
			cells.push_back(place_cell(cell));
		}

		for (auto &c : cells) {
			instances.at(c.place).cells++;
			// set_string_attribute() drops empty values, but the top module's path is the empty string
			c.cell->attributes[ID(hier_scope)] = RTLIL::Const(path_str(c.place));
		}
		place_wires();
	}

	// Preparation 1: the tree of instances, from the $scopeinfo cells.
	void find_instances()
	{
		Instance &root = instances[{}];
		root.module = log_id(top);
		root.module_src = parse_src(top->get_string_attribute(ID::src));

		for (auto cell : top->cells()) {
			if (cell->type != ID($scopeinfo) || !cell->hasParam(ID::TYPE) || cell->getParam(ID::TYPE).decode_string() != "module")
				continue;
			std::string name = RTLIL::unescape_id(cell->name);
			// flatten only sets hdlname on nested instances; a direct child of the top module is named by its instance name
			std::string hdlname = cell->get_string_attribute(ID::hdlname);
			Instance &inst = instances[hdlname.empty() ? Path{name} : split_tokens(hdlname, " ")];
			inst.name = name;
			inst.module = cell->get_string_attribute(ID(module_hdlname));
			if (inst.module.empty())
				inst.module = cell->get_string_attribute(ID(module));
			inst.module_src = parse_src(cell->get_string_attribute(ID(module_src)));
		}
		if (instances.size() == 1)
			check_scopeinfo_not_lost();

		for (auto &[path, inst] : instances)
			if (!path.empty())
				inst.parent = nearest_instance(Path(path.begin(), path.end() - 1));
	}

	// A design without $scopeinfo cells either had no instances or lost them. 'flatten -noscopeinfo' does
	// not create them, and synth_gatemate removes them again with its final 'opt_clean -purge'. Names that
	// still carry an instance path tell the second case apart.
	void check_scopeinfo_not_lost()
	{
		auto nested = [](RTLIL::AttrObject *obj) {
			return obj->get_string_attribute(ID::hdlname).find(' ') != std::string::npos ||
			       !obj->get_string_attribute(ID(scopename)).empty();
		};
		if (std::any_of(top->wires().begin(), top->wires().end(), nested) ||
		    std::any_of(top->cells().begin(), top->cells().end(), nested))
			log_cmd_error("%s has lost the $scopeinfo cells of its instances, which are needed to recover them. "
				      "'flatten -noscopeinfo' does not create them, and synth_gatemate removes them in its last "
				      "step; run it with '-run :vlog' to stop before that step.\n",
				      log_id(top));
	}

	// The given instance or, if it does not exist, the nearest one above it.
	Path nearest_instance(Path p)
	{
		while (!instances.count(p))
			p.pop_back();
		return p;
	}

	// The instance a wire's name belongs to, if the name still tells it reliably.
	std::optional<Path> instance_of_wire(RTLIL::Wire *wire)
	{
		std::string hdlname = wire->get_string_attribute(ID::hdlname);
		if (!hdlname.empty()) {
			Path p = split_tokens(hdlname, " ");
			p.pop_back(); // the last part is the wire's own name
			if (instances.count(p))
				return p;
			return std::nullopt;
		}
		// Wires of the top module keep their plain name and a source location inside the top module.
		// Names made up by synthesis (autoname, abc) carry no or foreign source locations and are skipped.
		std::string name = RTLIL::unescape_id(wire->name);
		if (!wire->name.isPublic() || name.find(sep) != std::string::npos)
			return std::nullopt;
		for (auto &r : parse_src(wire->get_string_attribute(ID::src)))
			for (auto &def : instances.at({}).module_src)
				if (def.contains(r))
					return Path{};
		return std::nullopt;
	}

	// Preparation 2: for every bit of a named wire, the instance of the name and its parent.
	void find_visible_bits()
	{
		for (auto wire : top->wires()) {
			auto inst = instance_of_wire(wire);
			if (!inst)
				continue;
			for (auto bit : sigmap(wire)) {
				if (bit.wire == nullptr)
					continue;
				visible[bit].insert(*inst);
				if (!inst->empty())
					visible[bit].insert(instances.at(*inst).parent);
			}
		}
	}

	// All instances of the module whose source contains the given source location. If module definitions
	// are nested in the source, as a VHDL architecture in its file, the innermost one counts.
	std::set<Path> instances_at(const SrcRange &r)
	{
		const SrcRange *best = nullptr;
		for (auto &[path, inst] : instances)
			for (auto &def : inst.module_src)
				if (def.contains(r) && (best == nullptr || def.size() < best->size()))
					best = &def;
		std::set<Path> out;
		if (best == nullptr)
			return out;
		for (auto &[path, inst] : instances)
			for (auto &def : inst.module_src)
				if (def == *best)
					out.insert(path);
		return out;
	}

	// Places one cell. No step chooses among alternatives, each one can only widen the place, so the place
	// is never wrong, at worst coarser than the truth.
	PlacedCell place_cell(RTLIL::Cell *cell)
	{
		// Step 1: every instance of the module each source location of the cell lies in. Of several
		// instances of one module none is chosen, all of them count.
		std::set<Path> from_src;
		for (auto &r : parse_src(cell->get_string_attribute(ID::src)))
			for (auto &p : instances_at(r))
				from_src.insert(p);
		if (from_src.empty())
			return {cell, {}}; // nothing tells where the cell comes from: the top module

		// Step 2: the source locations can be incomplete. A named net that none of these instances sees
		// shows that the cell also holds logic of an instance that sees it, the deepest one counts as well.
		std::set<Path> found = from_src;
		for (auto &conn : cell->connections())
			for (auto bit : sigmap(conn.second)) {
				auto it = visible.find(bit);
				if (it == visible.end())
					continue;
				bool seen = false;
				for (auto &p : from_src)
					if (it->second.count(p))
						seen = true;
				if (seen)
					continue;
				Path deepest = *it->second.begin();
				for (auto &p : it->second)
					if (p.size() > deepest.size())
						deepest = p;
				found.insert(deepest);
			}

		// Step 3: the innermost instance that contains all of them.
		Path place = *found.begin();
		for (auto &p : found)
			place = common_ancestor(place, p);
		return {cell, nearest_instance(place)};
	}

	// A wire whose name still tells its instance stays there. Any other wire lies in the innermost instance
	// that contains all cells connected to it, so that it can reach each of them.
	void place_wires()
	{
		dict<RTLIL::SigBit, std::vector<Path>> cells_of_bit;
		for (auto &c : cells)
			for (auto &conn : c.cell->connections())
				for (auto bit : sigmap(conn.second))
					if (bit.wire != nullptr)
						cells_of_bit[bit].push_back(c.place);

		for (auto wire : top->wires()) {
			std::optional<Path> place = instance_of_wire(wire);
			if (!place) {
				for (auto bit : sigmap(wire))
					if (cells_of_bit.count(bit))
						for (auto &p : cells_of_bit.at(bit))
							place = place ? common_ancestor(*place, p) : p;
				place = place ? nearest_instance(*place) : Path{};
			}
			wire->attributes[ID(hier_scope)] = RTLIL::Const(path_str(*place));
		}
	}

	// -----------------------------------------------------------------------------------------------------
	// Log
	// -----------------------------------------------------------------------------------------------------

	void log_tree(const Path &path, int depth)
	{
		const Instance &inst = instances.at(path);
		log("  %*s%s  [%s]  cells=%d\n", depth * 2, "", path.empty() ? "<top>" : inst.name.c_str(), inst.module.c_str(),
		    inst.cells);
		for (auto &[child, c] : instances)
			if (!child.empty() && c.parent == path)
				log_tree(child, depth + 1);
	}

	void log_summary()
	{
		log("\nRecovered hierarchy of %s (%d cells):\n", log_id(top), (int)cells.size());
		log_tree({}, 0);
	}
};

// ---------------------------------------------------------------------------------------------------------
// The command for yosys
// ---------------------------------------------------------------------------------------------------------

struct HierRestorePass : public Pass {
	HierRestorePass() : Pass("hier_restore", "recover the module hierarchy of a synthesized, flattened design") {}

	void help() override
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    hier_restore\n");
		log("\n");
		log("Assigns every cell of the flattened top module to the module instance it came\n");
		log("from. Run it after synthesis. The instances are taken from the $scopeinfo cells\n");
		log("that 'flatten' and the synth_* scripts create by default. synth_gatemate removes\n");
		log("them in its last step, so run it with '-run :vlog' to stop before that step.\n");
		log("The hierarchy separator is the one flatten used ('flatten.separator').\n");
		log("The netlist itself is not changed, only these attributes are set:\n");
		log("\n");
		log("    hier_scope        on every cell: the instance path, written like 'hdlname'\n");
		log("                      (instance names separated by spaces, empty for the top)\n");
		log("    hier_scope        on every wire: the instance its name belongs to, or the\n");
		log("                      innermost instance that contains all connected cells\n");
		log("\n");
		log("A cell is placed only as deep as the netlist proves. Where several instances\n");
		log("are possible, it lies in the innermost instance containing all of them, and a\n");
		log("cell without a source location in the design lies in the top module.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		log_header(design, "Executing HIER_RESTORE pass.\n");

		extra_args(args, 1, design); // the pass has no options

		std::string sep = design->scratchpad_get_string("flatten.separator", ".");
		if (sep.empty())
			log_cmd_error("The hierarchy separator must not be empty.\n");
		RTLIL::Module *top = design->top_module();
		if (top == nullptr)
			log_cmd_error("No top module found.\n");

		HierRestore worker(top, sep);
		worker.run();
		worker.log_summary();
	}
} HierRestorePass;

PRIVATE_NAMESPACE_END
