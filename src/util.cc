#include "types.h"

namespace hier_restore {

bool is_within(const Path &inner, const Path &outer)
{
	return inner.size() >= outer.size() && std::equal(outer.begin(), outer.end(), inner.begin());
}

Path common_ancestor(const Path &a, const Path &b)
{
	size_t n = 0;
	while (n < a.size() && n < b.size() && a[n] == b[n])
		n++;
	return Path(a.begin(), a.begin() + n);
}

// Same convention as the 'hdlname' attribute: instance names joined by spaces.
std::string path_str(const Path &p)
{
	std::string s;
	for (auto &name : p)
		s += (s.empty() ? "" : " ") + name;
	return s;
}

// Parses one "file:l1.c1-l2.c2", "file:l.c", "file:l1-l2" or "file:l" entry.
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

// yosys joins the origins of merged objects with '|'
std::vector<SrcRange> parse_src(const std::string &src)
{
	std::vector<SrcRange> out;
	for (auto &part : split_tokens(src, "|")) {
		SrcRange r;
		if (parse_range(part, r))
			out.push_back(r);
	}
	return out;
}

const char *status_name(CellStatus s)
{
	switch (s) {
	case CellStatus::Unique:
		return "unique";
	case CellStatus::Inferred:
		return "inferred";
	case CellStatus::Merged:
		return "merged";
	case CellStatus::Ambiguous:
		return "ambiguous";
	default:
		return "unknown";
	}
}

ScopeSet intersect(const ScopeSet &a, const ScopeSet &b)
{
	ScopeSet out;
	std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::inserter(out, out.end()), ByPath());
	return out;
}

std::string json_str(const std::string &s)
{
	std::string o = "\"";
	for (unsigned char c : s) {
		if (c == '"' || c == '\\')
			o += std::string("\\") + (char)c;
		else if (c < 0x20)
			o += stringf("\\u%04x", c);
		else
			o += c;
	}
	return o + "\"";
}

std::string json_path(const Path &p)
{
	std::string o = "[";
	for (size_t i = 0; i < p.size(); i++)
		o += (i ? ", " : "") + json_str(p[i]);
	return o + "]";
}

std::string join(const std::set<std::string> &items, const std::string &sep)
{
	std::string o;
	for (auto &i : items)
		o += (o.empty() ? "" : sep) + i;
	return o;
}

} // namespace hier_restore
