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

enum class CellStatus { Unique, Inferred, Merged, Ambiguous, Unknown };

struct Scope {
	Path path;
	std::string flat_name; // prefix that flatten put in front of the names of this instance's objects
	std::string module;
	std::vector<SrcRange> module_src;
	RTLIL::Cell *info = nullptr; // the $scopeinfo cell, null for the top module
	Scope *parent = nullptr;
	std::map<CellStatus, int> own;
	int mentions = 0; // merged or ambiguous cells listing this scope as a candidate

	const char *status() const
	{
		int placed = 0;
		for (auto s : {CellStatus::Unique, CellStatus::Inferred})
			placed += own.count(s) ? own.at(s) : 0;
		if (placed > 0)
			return mentions > 0 ? "partial" : "complete";
		return mentions > 0 ? "absorbed" : "empty";
	}
};

// Orders scopes by path so that every result is independent of memory layout.
struct ByPath {
	bool operator()(const Scope *a, const Scope *b) const { return a->path < b->path; }
};
using ScopeSet = std::set<Scope *, ByPath>;

struct CellInfo {
	RTLIL::Cell *cell;
	std::string name;

	// Each group lists the instances one origin of the cell may lie in: several entries are several
	// instances of the same module, several groups are logic merged from several places.
	std::vector<ScopeSet> groups;
	std::vector<const ScopeSet *> visible; // per named connected bit, the scopes that can see it
	std::vector<RTLIL::SigBit> unnamed;    // connected bits without a reliable name
	std::vector<RTLIL::SigBit> outputs;
	std::set<std::string> evidence;

	// Instances the cell can lie in as a whole. 'any' means nothing restricts it, an empty domain
	// means no single instance fits and the cell holds logic of several.
	bool any = false;
	ScopeSet domain;
	bool from_neighbours = false; // nothing but its neighbours restricted the domain

	CellStatus status = CellStatus::Unknown;
	Scope *scope = nullptr;
	std::vector<Scope *> candidates; // set for merged and ambiguous cells
};

bool is_within(const Path &inner, const Path &outer);
Path common_ancestor(const Path &a, const Path &b);
std::string path_str(const Path &p);
std::vector<SrcRange> parse_src(const std::string &src);
const char *status_name(CellStatus s);
ScopeSet intersect(const ScopeSet &a, const ScopeSet &b);
std::string json_str(const std::string &s);
std::string json_path(const Path &p);
std::string join(const std::set<std::string> &items, const std::string &sep);

} // namespace hier_restore
