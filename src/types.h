#pragma once

#include "kernel/sigtools.h"
#include "kernel/yosys.h"

#include <algorithm>
#include <climits>
#include <map>
#include <set>

USING_YOSYS_NAMESPACE

namespace hier_restore {

// Instance names from the top module down, empty for the top module itself.
using Path = std::vector<std::string>;

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

// unique: one instance; ambiguous: one or more of several instances, which ones is unknown;
// unknown: nothing tells where the cell comes from.
enum class CellStatus { Unique, Ambiguous, Unknown };

struct Scope {
	Path path;
	std::string flat_name; // prefix that flatten put in front of the names of this instance's objects
	std::string module;
	std::vector<SrcRange> module_src;
	Scope *parent = nullptr;
	int cells = 0; // cells placed here
};

// Orders scopes by path so that every result is independent of memory layout.
struct ByPath {
	bool operator()(const Scope *a, const Scope *b) const { return a->path < b->path; }
};
using ScopeSet = std::set<Scope *, ByPath>;

struct CellInfo {
	RTLIL::Cell *cell;
	std::string name;
	CellStatus status = CellStatus::Unknown;
	Scope *scope = nullptr;
};

Path common_ancestor(const Path &a, const Path &b);
std::string path_str(const Path &p);
std::vector<SrcRange> parse_src(const std::string &src);
const char *status_name(CellStatus s);
std::string json_str(const std::string &s);
std::string json_path(const Path &p);

} // namespace hier_restore
