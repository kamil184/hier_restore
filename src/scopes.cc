#include "worker.h"

namespace hier_restore {

// Builds the tree of instances from the $scopeinfo cells, which flatten leaves for every instance.
void HierRestoreWorker::collect_scopes()
{
	root = &scopes[{}];
	root->module = log_id(top);
	root->module_src = parse_src(top->get_string_attribute(ID::src));

	for (auto cell : top->cells()) {
		if (cell->type != ID($scopeinfo) || !cell->hasParam(ID::TYPE) || cell->getParam(ID::TYPE).decode_string() != "module")
			continue;
		std::string flat_name = RTLIL::unescape_id(cell->name);
		// flatten only sets hdlname on nested instances; a direct child of the top module is named by its instance name
		std::string hdlname = cell->get_string_attribute(ID::hdlname);
		Path path = hdlname.empty() ? Path{flat_name} : split_tokens(hdlname, " ");
		Scope &s = scopes[path];
		s.path = path;
		s.flat_name = flat_name;
		s.module = cell->get_string_attribute(ID(module_hdlname));
		if (s.module.empty())
			s.module = cell->get_string_attribute(ID(module));
		s.module_src = parse_src(cell->get_string_attribute(ID(module_src)));
	}
	if (scopes.size() == 1)
		check_flattened_with_scopes();

	for (auto &[path, s] : scopes) {
		if (&s == root)
			continue;
		Path p = path;
		do {
			p.pop_back();
		} while (!p.empty() && !scopes.count(p));
		s.parent = &scopes.at(p);
	}
}

// The scope and local name of a wire, if its name still tells them reliably.
std::pair<Scope *, std::string> HierRestoreWorker::wire_scope(RTLIL::Wire *wire)
{
	std::string hdlname = wire->get_string_attribute(ID::hdlname);
	if (!hdlname.empty()) {
		Path p = split_tokens(hdlname, " ");
		std::string local = p.back();
		p.pop_back();
		auto it = scopes.find(p);
		return {it == scopes.end() ? nullptr : &it->second, local};
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

void HierRestoreWorker::collect_bits()
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
		}
	}
}

// All instances of the innermost module definition that contains the given source range.
ScopeSet HierRestoreWorker::instances_at(const SrcRange &r)
{
	const SrcRange *best = nullptr;
	for (auto &[path, s] : scopes)
		for (auto &def : s.module_src)
			if (def.contains(r) && (best == nullptr || def.size() < best->size()))
				best = &def;
	ScopeSet out;
	if (best == nullptr)
		return out;
	for (auto &[path, s] : scopes)
		for (auto &def : s.module_src)
			if (def == *best)
				out.insert(&s);
	return out;
}

Scope *HierRestoreWorker::scope_at(Path p)
{
	while (!scopes.count(p))
		p.pop_back();
	return &scopes.at(p);
}

// A design without $scopeinfo cells either had no instances or lost them. 'flatten -noscopeinfo' does not
// create them, and synth_gatemate removes them again with its final 'opt_clean -purge'. Names that still
// carry an instance path tell the second case apart.
void HierRestoreWorker::check_flattened_with_scopes()
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

} // namespace hier_restore
