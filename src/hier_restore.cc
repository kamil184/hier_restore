// hier_restore: a yosys pass that recovers the module hierarchy of a flattened, synthesized design.
//
// After 'flatten' and synthesis all cells lie in the top module. This pass finds for every cell the instance
// it comes from, for example "u_alu u_add", and then rebuilds the modules: every instance becomes a module of
// its own with its cells, and a port wherever a signal crosses its frame. The logic itself is not changed.
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
//
//   Rebuilding the modules (see rebuild_modules)
//     1. A signal that is driven outside an instance and read inside becomes an input of the instance, one
//        driven inside and read outside an output.
//     2. The ports are named after the nets of their signals, those of the instance itself first.
//     3. Every instance becomes a module with its cells, a cell for each child instance and its nets.

#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#include <climits>
#include <algorithm>
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

// Whether the instance with path p lies inside the instance with path scope, or is it.
static bool is_inside(const Path &p, const Path &scope)
{
	return p.size() >= scope.size() && std::equal(scope.begin(), scope.end(), p.begin());
}

// A path as text, like the 'hdlname' attribute: instance names separated by spaces.
static std::string path_str(const Path &p)
{
	std::string s;
	for (auto &name : p)
		s += (s.empty() ? "" : " ") + name;
	return s;
}

// The name of a new wire or cell as yosys keeps it. Names starting with '$' stay hidden, as those yosys makes up.
static RTLIL::IdString new_id(const std::string &name)
{
	return name[0] == '$' ? name : "\\" + name;
}

// The name, or the name with the first free number appended if it is already taken. The name is then taken.
static std::string unique_name(std::set<std::string> &taken, const std::string &name)
{
	std::string out = name;
	for (int i = 1; taken.count(out); i++)
		out = name + "_" + std::to_string(i);
	taken.insert(out);
	return out;
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

// A net of the flattened design, with its name inside the instance it lies in.
struct Net {
	std::string name;
	std::vector<RTLIL::SigBit> bits;  // its signals from the lowest index up, without constants and unused bits
	bool hidden;                      // a name made up by yosys, starting with '$'
	bool has_hdlname;                 // a name from the source text
	std::string src;
};

// Nets with better names come first: visible names, then names from the source text, then wider nets.
static bool better_name(const Net &a, const Net &b)
{
	return std::make_tuple(a.hidden, !a.has_hdlname, -(long)a.bits.size(), a.name) <
	       std::make_tuple(b.hidden, !b.has_hdlname, -(long)b.bits.size(), b.name);
}

// A wire of a rebuilt module, before it is created.
struct NewWire {
	std::string name;                 // without the leading '\\' of public names
	std::vector<RTLIL::SigBit> bits;  // the signals of the flattened design it carries, or constants
	std::string src;
	bool input = false, output = false;
	int offset = 0;                   // only for the ports of the top module, which stay as they are
	bool upto = false, is_signed = false;
};

struct Instance {
	std::string name;                  // the name flatten gave it, for the log
	std::string module;                // the module it is an instance of
	std::vector<SrcRange> module_src;  // where that module is written in the source files
	std::string cell_src;              // where the instance is written in the source files
	Path parent;
	int cells = 0;                     // cells placed here

	// for rebuilding the modules
	std::vector<RTLIL::SigBit> inputs, outputs;  // signals entering and leaving the instance
	std::vector<NewWire> ports;
	std::vector<Net> nets;                       // the nets that lie in the instance, best names first
	RTLIL::IdString new_module;
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
	dict<RTLIL::Wire *, Path> wire_place;

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
				cells.push_back({cell, {}});
				continue;
			}
			cells.push_back(place_cell(cell));
		}

		for (auto &c : cells)
			instances.at(c.place).cells++;
		place_wires();
		log_summary();
		rebuild_modules();
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
			inst.cell_src = cell->get_string_attribute(ID(cell_src));
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
			wire_place[wire] = *place;
		}
	}

	// -----------------------------------------------------------------------------------------------------
	// Rebuilding the modules
	// -----------------------------------------------------------------------------------------------------

	// A name without the path of its instance, for example "u_cnt.q" in "u_cnt" becomes "q".
	std::string local_name(const std::string &name, const Path &path)
	{
		std::string prefix;
		for (auto &part : path)
			prefix += part + sep;
		if (!path.empty() && name.compare(0, prefix.size(), prefix) == 0)
			return name.substr(prefix.size());
		return name;
	}

	// The signals of a wire from its lowest index up, without constants and the bits listed as unused.
	std::vector<RTLIL::SigBit> used_bits(RTLIL::Wire *wire)
	{
		std::set<int> unused;
		for (auto &index : split_tokens(wire->get_string_attribute(ID(unused_bits)), " "))
			unused.insert(std::stoi(index));
		std::vector<RTLIL::SigBit> out;
		for (int i = 0; i < wire->width; i++) {
			int position = wire->upto ? wire->width - 1 - i : i;
			RTLIL::SigBit bit = sigmap(RTLIL::SigBit(wire, position));
			if (bit.wire != nullptr && !unused.count(wire->start_offset + position))
				out.push_back(bit);
		}
		return out;
	}

	void rebuild_modules()
	{
		if (instances.size() == 1)
			return; // no instances, the netlist stays as it is

		// cells and wires in the order write_json writes them, which decides among equal names
		auto by_name = [](RTLIL::IdString a, RTLIL::IdString b) { return strcmp(a.c_str(), b.c_str()) < 0; };
		std::sort(cells.begin(), cells.end(),
			  [&](const PlacedCell &a, const PlacedCell &b) { return by_name(a.cell->name, b.cell->name); });
		std::vector<RTLIL::Wire *> wires(top->wires().begin(), top->wires().end());
		std::sort(wires.begin(), wires.end(), [&](RTLIL::Wire *a, RTLIL::Wire *b) { return by_name(a->name, b->name); });

		find_crossing_signals();

		// Every net goes to the instance it lies in
		std::vector<Net> all_nets;
		for (auto wire : wires) {
			const Path &path = wire_place.at(wire);
			std::string hdlname = wire->get_string_attribute(ID::hdlname);
			Net net;
			if (hdlname.empty())
				net.name = local_name(RTLIL::unescape_id(wire->name), path);
			else
				net.name = split_tokens(hdlname, " ").back();
			net.bits = used_bits(wire);
			net.hidden = !wire->name.isPublic();
			net.has_hdlname = !hdlname.empty();
			net.src = wire->get_string_attribute(ID::src);
			instances.at(path).nets.push_back(net);
			all_nets.push_back(net);
		}
		for (auto &[path, inst] : instances)
			std::stable_sort(inst.nets.begin(), inst.nets.end(), better_name);
		std::stable_sort(all_nets.begin(), all_nets.end(), better_name);

		// The ports of every instance, named after the nets of their signals, those of the instance first
		for (auto &[path, inst] : instances) {
			if (path.empty())
				continue;
			std::vector<Net> candidates = inst.nets;
			candidates.insert(candidates.end(), all_nets.begin(), all_nets.end());
			add_ports(inst, true, inst.inputs, candidates);
			add_ports(inst, false, inst.outputs, candidates);
			inst.new_module = "\\" + inst.module + "$" + inst.name;
			if (top->design->module(inst.new_module) != nullptr)
				log_cmd_error("Module %s already exists.\n", log_id(inst.new_module));
		}

		// The top module is built anew as well and then takes the place of the flattened one
		for (auto &[path, inst] : instances)
			build_module(path, inst, all_nets);
		RTLIL::Design *design = top->design;
		RTLIL::IdString name = top->name;
		RTLIL::Module *root = design->module(instances.at({}).new_module);
		root->attributes = top->attributes;
		design->remove(top);
		design->rename(root, name);
		top = nullptr;
	}

	// Rebuilding 1: the signals that enter or leave each instance. An input port of the top module drives
	// its signal, an output port reads it.
	void find_crossing_signals()
	{
		dict<RTLIL::SigBit, std::set<Path>> drivers, readers;
		std::vector<RTLIL::SigBit> all_bits;
		auto note = [&](dict<RTLIL::SigBit, std::set<Path>> &where, const RTLIL::SigSpec &sig, const Path &path) {
			for (auto bit : sigmap(sig)) {
				if (bit.wire == nullptr)
					continue;
				if (!drivers.count(bit) && !readers.count(bit))
					all_bits.push_back(bit);
				where[bit].insert(path);
			}
		};
		for (auto wire : top->wires()) {
			if (wire->port_input)
				note(drivers, wire, {});
			if (wire->port_output)
				note(readers, wire, {});
		}
		for (auto &c : cells)
			for (auto &conn : c.cell->connections()) {
				bool output = c.cell->output(conn.first);
				if (output)
					note(drivers, conn.second, c.place);
				if (c.cell->input(conn.first) || !output)
					note(readers, conn.second, c.place);
			}

		for (auto &[path, inst] : instances) {
			if (path.empty())
				continue;
			for (auto bit : all_bits) {
				bool driven_inside = false, driven_outside = false, read_inside = false, read_outside = false;
				for (auto &p : drivers[bit])
					(is_inside(p, path) ? driven_inside : driven_outside) = true;
				for (auto &p : readers[bit])
					(is_inside(p, path) ? read_inside : read_outside) = true;
				if (driven_outside && read_inside)
					inst.inputs.push_back(bit);
				else if (driven_inside && read_outside)
					inst.outputs.push_back(bit);
			}
		}
	}

	// Rebuilding 2: the ports of one direction for the given signals, each named after the first net that
	// carries some of them.
	void add_ports(Instance &inst, bool input, const std::vector<RTLIL::SigBit> &bits,
		       const std::vector<Net> &candidates)
	{
		std::set<std::string> taken;
		for (auto &port : inst.ports)
			taken.insert(port.name);
		pool<RTLIL::SigBit> remaining(bits.begin(), bits.end());
		auto add_port = [&](std::string name, const std::vector<RTLIL::SigBit> &port_bits, const std::string &src) {
			std::replace(name.begin(), name.end(), ' ', '_');
			NewWire port;
			port.name = unique_name(taken, name);
			port.bits = port_bits;
			port.src = src;
			port.input = input;
			port.output = !input;
			inst.ports.push_back(port);
			for (auto bit : port_bits)
				remaining.erase(bit);
		};

		for (auto &net : candidates) {
			std::vector<RTLIL::SigBit> port_bits;
			for (auto bit : net.bits)
				if (remaining.count(bit) && std::find(port_bits.begin(), port_bits.end(), bit) == port_bits.end())
					port_bits.push_back(bit);
			if (!port_bits.empty())
				add_port(net.name, port_bits, net.src);
		}
		int n = 0;
		for (auto bit : bits)
			if (remaining.count(bit))
				add_port((input ? "in_" : "out_") + std::to_string(n++), {bit}, "");
	}

	// Rebuilding 3: the module of one instance, with its cells, a cell for each child instance and its nets.
	void build_module(const Path &path, Instance &inst, const std::vector<Net> &all_nets)
	{
		RTLIL::Module *m;
		if (path.empty()) {
			m = top->design->addModule(NEW_ID);
			inst.new_module = m->name;
		} else {
			m = top->design->addModule(inst.new_module);
			m->set_string_attribute(ID::hdlname, inst.module.empty() ? path.back() : inst.module);
		}

		// The cells of the instance and its child instances
		std::vector<RTLIL::Cell *> own_cells;
		for (auto &c : cells)
			if (c.place == path)
				own_cells.push_back(c.cell);
		std::vector<std::pair<Path, Instance *>> children;
		for (auto &[child_path, child] : instances)
			if (!child_path.empty() && child.parent == path)
				children.push_back({child_path, &child});

		// The ports of the top module stay as they are
		std::vector<NewWire> ports = inst.ports;
		if (path.empty())
			for (auto port_name : top->ports) {
				RTLIL::Wire *wire = top->wire(port_name);
				NewWire port;
				port.name = RTLIL::unescape_id(wire->name);
				port.bits = sigmap(wire).bits();
				port.input = wire->port_input;
				port.output = wire->port_output;
				port.offset = wire->start_offset;
				port.upto = wire->upto;
				port.is_signed = wire->is_signed;
				ports.push_back(port);
			}

		// The signals used in the module
		std::vector<RTLIL::SigBit> used;
		pool<RTLIL::SigBit> is_used;
		auto use = [&](const std::vector<RTLIL::SigBit> &bits) {
			for (auto bit : bits)
				if (bit.wire != nullptr && is_used.insert(bit).second)
					used.push_back(bit);
		};
		for (auto &port : ports)
			use(port.bits);
		for (auto cell : own_cells)
			for (auto &conn : cell->connections())
				use(sigmap(conn.second).bits());
		for (auto &[child_path, child] : children)
			for (auto &port : child->ports)
				use(port.bits);

		// The nets of the module: every port is a net of its own; then follow the nets of the instance, and
		// for signals still without a net, the nets of other instances. Wires and cells of a module must not
		// share a name, so all names come from one set, the ports first, as the parent uses their names.
		std::vector<NewWire> nets = ports;
		std::set<std::string> names;
		std::map<std::string, size_t> net_index;
		pool<RTLIL::SigBit> uncovered = is_used;
		auto add_net = [&](const std::string &name, const std::vector<RTLIL::SigBit> &bits, const std::string &src) {
			NewWire net;
			net.name = unique_name(names, name);
			net.bits = bits;
			net.src = src;
			net_index[net.name] = nets.size();
			nets.push_back(net);
			for (auto bit : bits)
				uncovered.erase(bit);
		};
		for (size_t i = 0; i < ports.size(); i++) {
			names.insert(ports[i].name);
			net_index[ports[i].name] = i;
			for (auto bit : ports[i].bits)
				uncovered.erase(bit);
		}
		for (auto &net : inst.nets) {
			std::vector<RTLIL::SigBit> bits;
			for (auto bit : net.bits)
				if (is_used.count(bit))
					bits.push_back(bit);
			if (bits.empty())
				continue;
			// a port carries the same signals under the same name: it takes over the source location
			auto it = net_index.find(net.name);
			if (it != net_index.end() && nets[it->second].bits == bits) {
				if (nets[it->second].src.empty())
					nets[it->second].src = net.src;
				continue;
			}
			add_net(net.name, bits, net.src);
		}
		for (auto &net : all_nets) {
			std::vector<RTLIL::SigBit> bits;
			for (auto bit : net.bits)
				if (uncovered.count(bit))
					bits.push_back(bit);
			if (!bits.empty())
				add_net(net.name, bits, net.src);
		}
		int n = 0;
		for (auto bit : used)
			if (uncovered.count(bit))
				add_net("$sig_" + std::to_string(n++), {bit}, "");

		// Create the wires. The first wire bit of each signal carries it in the module, the other ones are
		// connected to it.
		dict<RTLIL::SigBit, RTLIL::SigBit> here;
		int port_id = 0;
		for (auto &net : nets) {
			RTLIL::Wire *wire = m->addWire(new_id(net.name), net.bits.size());
			wire->port_input = net.input;
			wire->port_output = net.output;
			if (net.input || net.output)
				wire->port_id = ++port_id; // the ports keep their order
			wire->start_offset = net.offset;
			wire->upto = net.upto;
			wire->is_signed = net.is_signed;
			if (!net.src.empty())
				wire->set_src_attribute(net.src);
			for (int i = 0; i < wire->width; i++) {
				RTLIL::SigBit bit = net.bits[i];
				if (bit.wire == nullptr)
					m->connect(RTLIL::SigBit(wire, i), bit);
				else if (here.count(bit))
					m->connect(RTLIL::SigBit(wire, i), here.at(bit));
				else
					here[bit] = RTLIL::SigBit(wire, i);
			}
		}
		auto in_module = [&](const RTLIL::SigSpec &sig) {
			RTLIL::SigSpec out;
			for (auto bit : sigmap(sig))
				out.append(bit.wire == nullptr ? bit : here.at(bit));
			return out;
		};

		// Copy the cells, named without the instance path, and keep the instance each one was placed in
		for (auto cell : own_cells) {
			std::string name = unique_name(names, local_name(RTLIL::unescape_id(cell->name), path));
			RTLIL::Cell *copy = m->addCell(new_id(name), cell);
			for (auto &conn : cell->connections())
				copy->setPort(conn.first, in_module(conn.second));
			copy->attributes[ID(hier_scope)] = RTLIL::Const(path_str(path));
		}
		for (auto &[child_path, child] : children) {
			RTLIL::Cell *cell = m->addCell(new_id(unique_name(names, child_path.back())), child->new_module);
			cell->attributes[ID(module_not_derived)] = RTLIL::Const(1);
			if (!child->cell_src.empty())
				cell->set_src_attribute(child->cell_src);
			for (auto &port : child->ports)
				cell->setPort(new_id(port.name), in_module(port.bits));
		}
		m->fixup_ports();
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
		log("from and rebuilds the modules. Run it after synthesis. The instances are taken\n");
		log("from the $scopeinfo cells that 'flatten' and the synth_* scripts create by\n");
		log("default. synth_gatemate removes them in its last step, so run it with\n");
		log("'-run :vlog' to stop before that step.\n");
		log("The hierarchy separator is the one flatten used ('flatten.separator').\n");
		log("\n");
		log("Every instance becomes a module of its own, named <module>$<instance>, with the\n");
		log("cells placed in it and a cell for each of its child instances. Where a signal\n");
		log("crosses the frame of an instance, the module gets a port, named after a net of\n");
		log("that signal. The logic is not changed; every cell keeps the instance it was\n");
		log("placed in as attribute 'hier_scope', written like 'hdlname'.\n");
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
	}
} HierRestorePass;

PRIVATE_NAMESPACE_END
