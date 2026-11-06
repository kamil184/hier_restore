#include "worker.h"

namespace hier_restore {

Scope *HierRestoreWorker::add_scope(const Path &path, const std::string &flat_name, const std::string &module, const std::string &module_src)
{
	storage.emplace_back(new Scope);
	Scope *s = storage.back().get();
	s->path = path;
	s->flat_name = flat_name;
	s->module = module;
	s->module_src = parse_src(module_src);
	by_path[path] = s;
	all_scopes.insert(s);
	if (!flat_name.empty())
		by_flat_name[flat_name] = s;
	return s;
}

void HierRestoreWorker::collect_scopes()
{
	root = add_scope({}, "", log_id(top), top->get_string_attribute(ID::src));

	for (auto cell : top->cells()) {
		if (cell->type != ID($scopeinfo) || !cell->hasParam(ID::TYPE) || cell->getParam(ID::TYPE).decode_string() != "module")
			continue;
		std::string flat_name = RTLIL::unescape_id(cell->name);
		// flatten only sets hdlname on nested instances; a direct child of the top module is named by its instance name
		std::string hdlname = cell->get_string_attribute(ID::hdlname);
		Path path = hdlname.empty() ? Path{flat_name} : split_tokens(hdlname, " ");
		std::string module = cell->get_string_attribute(ID(module_hdlname));
		if (module.empty())
			module = cell->get_string_attribute(ID(module));
		Scope *s = add_scope(path, flat_name, module, cell->get_string_attribute(ID(module_src)));
		s->info = cell;
	}

	for (auto &s : storage) {
		if (s.get() == root)
			continue;
		Path p = s->path;
		do {
			p.pop_back();
		} while (!p.empty() && !by_path.count(p));
		s->parent = by_path.at(p);
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
		auto it = by_path.find(p);
		return {it == by_path.end() ? nullptr : it->second, local};
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
			bit_names[bit].emplace_back(s, local);
		}
	}
}

// All instances of the innermost module definition that contains the given source range.
ScopeSet HierRestoreWorker::instances_at(const SrcRange &r)
{
	const SrcRange *best = nullptr;
	for (auto &s : storage)
		for (auto &def : s->module_src)
			if (def.contains(r) && (best == nullptr || def.size() < best->size()))
				best = &def;
	ScopeSet out;
	if (best == nullptr)
		return out;
	for (auto &s : storage)
		for (auto &def : s->module_src)
			if (def == *best)
				out.insert(s.get());
	return out;
}

// The scope whose flattened name is the longest prefix of the cell name. synth scripts name cells after
// a connected wire, which may as well lie on the other side of an instance boundary, so this is weak.
Scope *HierRestoreWorker::name_scope(const std::string &name)
{
	Scope *best = nullptr;
	for (size_t pos = name.find(sep); pos != std::string::npos; pos = name.find(sep, pos + sep.size())) {
		auto it = by_flat_name.find(name.substr(0, pos));
		if (it != by_flat_name.end())
			best = it->second;
	}
	return best;
}

Scope *HierRestoreWorker::scope_at(Path p)
{
	while (!by_path.count(p))
		p.pop_back();
	return by_path.at(p);
}

// Without $scopeinfo cells the instances are unknown, which only is fine if there never were any.
void HierRestoreWorker::check_flattened_with_scopes()
{
	if (storage.size() > 1)
		return;
	auto nested = [](RTLIL::AttrObject *obj) {
		return obj->get_string_attribute(ID::hdlname).find(' ') != std::string::npos;
	};
	bool lost = std::any_of(top->wires().begin(), top->wires().end(), nested) ||
		    std::any_of(top->cells().begin(), top->cells().end(), nested);
	if (lost)
		log_cmd_error("%s was flattened without $scopeinfo cells (flatten -noscopeinfo), its instances cannot be "
			      "recovered.\n",
			      log_id(top));
}

} // namespace hier_restore
