#!/usr/bin/env python3
"""Runs hier_restore on every test design with every synthesis flow and checks the result.

Each directory below tests/cases (hand-written designs) and tests/examples (open-source designs, see their
SOURCE.md) holds the design and an expect.json:

    {
      "top": "top",                      # top module, default "top"
      "files": ["design.v"],             # Verilog files in read order, default ["design.v"]
      "flows": ["ice40", "generic"],     # flows to run, default all of FLOWS
      "expect": [                        # each entry may be limited with "flows": [...]
        {"scope": ["u_a", "u_b"], "status": ["complete"]},
        {"status": "ambiguous", "max": 0},
        {"status": "merged", "min": 1},
        {"merged_with": [["u_a"], ["u_b"]]}
      ]
    }

Independent of expect.json, every run is checked for these invariants:
  - every cell carries the hier_* attributes and they match the JSON report,
  - every wire carries hier_scope with a known instance,
  - statuses are consistent with candidates and the placed scope,
  - the netlist is unchanged apart from the hier_* attributes,
  - a second run yields the identical report.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
CASES = [HERE / "cases", HERE / "examples"]
PLUGIN = HERE.parent / "hier_restore.so"

FLOWS = {
    "ice40": "synth_ice40 -top {top}",
    "generic": "synth -flatten -top {top}",
    "ecp5": "synth_ecp5 -top {top}",
    "gowin": "synth_gowin -top {top}",
    "xilinx": "synth_xilinx -flatten -top {top}",
}

CELL_STATUSES = {"unique", "inferred", "merged", "ambiguous", "unknown"}


def yosys(script, log):
    exe = os.environ.get("YOSYS", "yosys")
    proc = subprocess.run([exe, "-q", "-m", str(PLUGIN), "-p", script], capture_output=True, text=True)
    log.write_text(proc.stdout + proc.stderr)
    if proc.returncode != 0:
        raise RuntimeError(f"yosys failed, see {log}:\n{proc.stderr[-2000:]}")


def run_flow(case, flow, cfg, work):
    files = " ".join(f'"{case / f}"' for f in cfg.get("files", ["design.v"]))
    synth = FLOWS[flow].format(top=cfg.get("top", "top"))
    base, annotated, report = work / "base.json", work / "annotated.json", work / "report.json"
    yosys(f"read_verilog {files}; {synth}; write_json {base}; hier_restore -o {report}; write_json {annotated}", work / "run.log")
    # The write_json before the pass changes the order of cells and wires the pass sees, so the second run
    # also catches results that depend on the order of objects in the design.
    again, base_again = work / "report2.json", work / "base2.json"
    yosys(f"read_verilog {files}; {synth}; hier_restore -o {again}; write_json {base_again}", work / "run2.log")
    return base, annotated, report, again, base_again


def top_module(netlist):
    return next(m for m in netlist["modules"].values() if m.get("attributes", {}).get("top"))


def strip_hier(netlist):
    for module in netlist["modules"].values():
        for item in [*module.get("cells", {}).values(), *module.get("netnames", {}).values()]:
            item["attributes"] = {k: v for k, v in item.get("attributes", {}).items() if not k.startswith("hier_")}
    return netlist


def is_within(inner, outer):
    return inner[: len(outer)] == outer


def common_ancestor(paths):
    out = paths[0]
    for p in paths[1:]:
        n = 0
        while n < len(out) and n < len(p) and out[n] == p[n]:
            n += 1
        out = out[:n]
    return out


def path_str(p):
    return " ".join(p)


def json_string(value):
    """Undoes write_json's blank appended to strings of the form /[01xz]* */."""
    if value is not None and value.endswith(" ") and re.fullmatch(r"[01xz]* +", value):
        return value[:-1]
    return value


def check_invariants(base, annotated, report, again, annotated_again):
    errors, notes = [], []
    rep = json.loads(report.read_text())

    # yosys itself names some cells after memory addresses ($display$0x...), which then differ between runs
    if strip_hier(json.loads(annotated_again.read_text())) != json.loads(base.read_text()):
        notes.append("synthesis is not deterministic for this design, reproducibility not checked")
    elif report.read_text() != again.read_text():
        errors.append("second run produced a different report")

    if strip_hier(json.loads(annotated.read_text())) != json.loads(base.read_text()):
        errors.append("netlist differs from the one before hier_restore apart from hier_* attributes")

    top = top_module(json.loads(annotated.read_text()))
    by_name = {c["name"]: c for c in rep["cells"]}
    scopes = {tuple(s["path"]) for s in rep["scopes"]}
    for name, cell in top["cells"].items():
        attrs = cell.get("attributes", {})
        if cell["type"] == "$scopeinfo":
            if "hier_status" not in attrs:
                errors.append(f"$scopeinfo {name} has no hier_status")
            continue
        r = by_name.get(name)
        if r is None:
            errors.append(f"cell {name} missing from the report")
            continue
        if attrs.get("hier_status") != r["status"] or json_string(attrs.get("hier_scope")) != path_str(r["scope"]):
            errors.append(f"cell {name}: attributes do not match the report")
        sep = "&" if r["status"] == "merged" else "|"
        if json_string(attrs.get("hier_candidates", "")) != sep.join(path_str(p) for p in r["candidates"]):
            errors.append(f"cell {name}: hier_candidates does not match the report")
    if len(by_name) != sum(1 for c in top["cells"].values() if c["type"] != "$scopeinfo"):
        errors.append("report and netlist disagree on the number of cells")

    for name, net in top["netnames"].items():
        scope = json_string(net.get("attributes", {}).get("hier_scope"))
        if scope is None:
            errors.append(f"wire {name} has no hier_scope")
        elif tuple(scope.split()) not in scopes:
            errors.append(f"wire {name}: placed in unknown scope {scope!r}")

    for c in rep["cells"]:
        status, cands = c["status"], c["candidates"]
        if status not in CELL_STATUSES:
            errors.append(f"cell {c['name']}: unknown status {status}")
        if tuple(c["scope"]) not in scopes:
            errors.append(f"cell {c['name']}: placed in unknown scope {c['scope']}")
        if status in ("merged", "ambiguous"):
            if len(cands) < 2 or c["scope"] != common_ancestor(cands):
                errors.append(f"cell {c['name']}: {status} needs >= 2 candidates and placement at their common ancestor")
        elif cands:
            errors.append(f"cell {c['name']}: {status} must not list candidates")
        if status == "unknown" and c["scope"]:
            errors.append(f"cell {c['name']}: unknown cells belong in the top module")

    for s in rep["scopes"]:
        placed = s["cells"].get("unique", 0) + s["cells"].get("inferred", 0)
        mentioned = s["mentions"] > 0
        want = ("partial" if mentioned else "complete") if placed else ("absorbed" if mentioned else "empty")
        if s["status"] != want:
            errors.append(f"scope {s['path']}: status {s['status']}, counts say {want}")
    return rep, errors, notes


def check_expectations(rep, expectations, flow):
    errors = []
    statuses = Counter(c["status"] for c in rep["cells"])
    scopes = {tuple(s["path"]): s for s in rep["scopes"]}
    for e in expectations:
        if flow not in e.get("flows", [flow]):
            continue
        if "scope" in e:
            s = scopes.get(tuple(e["scope"]))
            if s is None:
                errors.append(f"scope {e['scope']} not found")
            elif s["status"] not in e["status"]:
                errors.append(f"scope {e['scope']} is {s['status']}, expected one of {e['status']}")
        elif "merged_with" in e:
            want = [list(p) for p in e["merged_with"]]
            if not any(c["status"] == "merged" and all(p in c["candidates"] for p in want) for c in rep["cells"]):
                errors.append(f"no merged cell lists all of {want}")
        elif "status" in e:
            n = statuses.get(e["status"], 0)
            if n < e.get("min", 0) or n > e.get("max", n):
                errors.append(f"{n} {e['status']} cells, expected [{e.get('min', 0)}, {e.get('max', 'inf')}]")
    return errors


def summary(rep):
    counts = Counter(c["status"] for c in rep["cells"])
    return " ".join(f"{s}={counts[s]}" for s in ("unique", "inferred", "merged", "ambiguous", "unknown") if counts[s])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--case", action="append", help="run only these cases")
    ap.add_argument("--flow", action="append", choices=sorted(FLOWS), help="run only these flows")
    ap.add_argument("--show", action="store_true", help="print the status of every scope")
    ap.add_argument("--keep", action="store_true", help="keep the work directories")
    args = ap.parse_args()

    if not PLUGIN.exists():
        sys.exit(f"{PLUGIN} not found, run make first")

    failures = 0
    root = Path(tempfile.mkdtemp(prefix="hier_restore_tests_"))
    for case in sorted(p for d in CASES for p in d.iterdir() if (p / "expect.json").exists()):
        if args.case and case.name not in args.case:
            continue
        cfg = json.loads((case / "expect.json").read_text())
        for flow in cfg.get("flows", list(FLOWS)):
            if args.flow and flow not in args.flow:
                continue
            work = root / case.name / flow
            work.mkdir(parents=True)
            try:
                rep, errors, notes = check_invariants(*run_flow(case, flow, cfg, work))
                errors += check_expectations(rep, cfg.get("expect", []), flow)
            except Exception as ex:  # a broken run is reported like any other failure
                rep, errors, notes = None, [str(ex)], []
            failures += bool(errors)
            print(f"{'FAIL' if errors else 'ok  '} {case.name:<24} {flow:<8} {summary(rep) if rep else ''}")
            for e in errors:
                print(f"       {e}")
            for n in notes:
                print(f"       note: {n}")
            if args.show and rep:
                for s in rep["scopes"]:
                    print(f"       {' '.join(s['path']) or '<top>':<40} {s['status']:<9} {s['cells']}")
    if args.keep or failures:
        print(f"\nwork directories: {root}")
    else:
        shutil.rmtree(root)
    print(f"\n{failures} failed" if failures else "\nall passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
