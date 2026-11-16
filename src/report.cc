#include "worker.h"

#include <fstream>

namespace hier_restore {

void HierRestoreWorker::log_tree(Scope *s, int depth)
{
	log("  %*s%s  [%s]  cells=%d\n", depth * 2, "", s == root ? "<top>" : s->flat_name.c_str(), s->module.c_str(), s->cells);
	for (auto &[path, c] : scopes)
		if (c.parent == s)
			log_tree(&c, depth + 1);
}

void HierRestoreWorker::log_summary()
{
	std::map<CellStatus, int> total;
	for (auto &c : cells)
		total[c.status]++;
	log("\nRecovered hierarchy of %s (%d cells):\n", log_id(top), (int)cells.size());
	log_tree(root, 0);
	log("\nCells by status:");
	for (auto &kv : total)
		log("  %s=%d", status_name(kv.first), kv.second);
	log("\n");
}

void HierRestoreWorker::write_report(const std::string &filename)
{
	std::ofstream os(filename);
	if (!os)
		log_cmd_error("Cannot open %s for writing.\n", filename.c_str());

	os << "{\n  \"top\": " << json_str(log_id(top)) << ",\n  \"scopes\": [";
	bool first = true;
	for (auto &[path, s] : scopes) {
		os << (first ? "\n" : ",\n") << "    {\"path\": " << json_path(path) << ", \"module\": " << json_str(s.module)
		   << ", \"cells\": " << s.cells << "}";
		first = false;
	}
	os << "\n  ],\n  \"cells\": [";
	first = true;
	for (auto &c : cells) {
		os << (first ? "\n" : ",\n") << "    {\"name\": " << json_str(c.name) << ", \"type\": " << json_str(RTLIL::unescape_id(c.cell->type))
		   << ", \"status\": " << json_str(status_name(c.status)) << ", \"scope\": " << json_path(c.scope->path) << "}";
		first = false;
	}
	os << "\n  ]\n}\n";
}

} // namespace hier_restore
