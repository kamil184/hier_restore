#!/usr/bin/env python3
"""Runs hier_restore on every test design with every synthesis flow and checks the result.

Each directory below tests/cases (hand-written designs) and tests/examples (open-source designs, see their
SOURCE.md) holds the design and an expect.json:

    {
      "top": "top",                      # top module, default "top"
      "files": ["design.v"],             # Verilog files in read order, default ["design.v"]
      "flows": ["ice40"],              # flows to run, default all of FLOWS
      "expect": [                        # each entry may be limited with "flows": [...]
        {"scope": ["u_a", "u_b"], "cells": true}   # whether cells are placed there
      ]
    }

Independent of expect.json, every run is checked for these invariants:
  - every cell and every wire carries hier_scope with a known instance,
  - no cell is placed in an instance its output signal does not exist in, found by simulation (simcheck.py),
  - the netlist is unchanged apart from the hier_* attributes,
  - a second run places every cell and wire the same way.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import simcheck

HERE = Path(__file__).resolve().parent
CASES = [HERE / "cases", HERE / "examples"]
PLUGIN = HERE.parent / "hier_restore.so"

FLOWS = {
    "ice40": "synth_ice40 -top {top}",
    # as OneWare runs it for GateMate boards, but without the final 'opt_clean -purge', which would remove
    # the $scopeinfo cells; the NetlistViewer synthesizes the same way for its restored view
    "gatemate": "synth_gatemate -top {top} -scopename -luttree -nomx8 -run :vlog",
}
# simulation models of the device cells, for the error check by simulation
MODELS = {"ice40": "+/ice40/cells_sim.v", "gatemate": "+/gatemate/cells_sim.v"}

def yosys(script, log):
    exe = os.environ.get("YOSYS", "yosys")
    proc = subprocess.run([exe, "-q", "-m", str(PLUGIN), "-p", script], capture_output=True, text=True)
    log.write_text(proc.stdout + proc.stderr)
    if proc.returncode != 0:
        raise RuntimeError(f"yosys failed, see {log}:\n{proc.stderr[-2000:]}")


def run_flow(case, flow, cfg, work):
    files = " ".join(f'"{case / f}"' for f in cfg.get("files", ["design.v"]))
    synth = FLOWS[flow].format(top=cfg.get("top", "top"))
    base, annotated, again = work / "base.json", work / "annotated.json", work / "annotated2.json"
    yosys(f"read_verilog {files}; {synth}; write_json {base}; hier_restore; write_json {annotated}", work / "run.log")
    # The write_json before the pass changes the order of cells and wires the pass sees, so the second run
    # also catches results that depend on the order of objects in the design.
    yosys(f"read_verilog {files}; {synth}; hier_restore; write_json {again}", work / "run2.log")
    return base, annotated, again


def placement_errors(case, flow, cfg, work, base, annotated):
    files = [case / f for f in cfg.get("files", ["design.v"])]
    errors = simcheck.find_errors(work, files, cfg.get("top", "top"), MODELS[flow], base, annotated)
    return [f"cell {c} placed in {s}, but its output {p} exists only in {', '.join(w)}" for c, s, p, w in errors]


def top_module(netlist):
    return next(m for m in netlist["modules"].values() if m.get("attributes", {}).get("top"))


def strip_hier(netlist):
    for module in netlist["modules"].values():
        for item in [*module.get("cells", {}).values(), *module.get("netnames", {}).values()]:
            item["attributes"] = {k: v for k, v in item.get("attributes", {}).items() if not k.startswith("hier_")}
    return netlist


def path_str(p):
    return " ".join(p)


def json_string(value):
    """Undoes write_json's blank appended to strings of the form /[01xz]* */."""
    if value is not None and value.endswith(" ") and re.fullmatch(r"[01xz]* +", value):
        return value[:-1]
    return value


def instances(top):
    """The paths of all instances, from the $scopeinfo cells; the top module is the empty path."""
    paths = {()}
    for name, cell in top["cells"].items():
        if cell["type"] == "$scopeinfo" and cell.get("parameters", {}).get("TYPE") == "module":
            hdlname = cell.get("attributes", {}).get("hdlname")
            paths.add(tuple(hdlname.split()) if hdlname else (name,))
    return paths


def placement(top):
    """{("cell" or "wire", name): instance path} from the hier_scope attributes."""
    def scope(item):
        value = json_string(item.get("attributes", {}).get("hier_scope"))
        return None if value is None else tuple(value.split())

    out = {("cell", name): scope(c) for name, c in top["cells"].items() if c["type"] != "$scopeinfo"}
    out.update({("wire", name): scope(w) for name, w in top["netnames"].items()})
    return out


def check_invariants(base, annotated, again):
    errors, notes = [], []
    top = top_module(json.loads(annotated.read_text()))
    placed = placement(top)

    # yosys itself names some cells after memory addresses ($display$0x...), which then differ between runs
    if strip_hier(json.loads(again.read_text())) != json.loads(base.read_text()):
        notes.append("synthesis is not deterministic for this design, reproducibility not checked")
    elif placement(top_module(json.loads(again.read_text()))) != placed:
        errors.append("second run placed cells or wires differently")

    if strip_hier(json.loads(annotated.read_text())) != json.loads(base.read_text()):
        errors.append("netlist differs from the one before hier_restore apart from hier_* attributes")

    for name, cell in top["cells"].items():
        if cell["type"] == "$scopeinfo" and any(k.startswith("hier_") for k in cell.get("attributes", {})):
            errors.append(f"$scopeinfo {name} got hier_* attributes")
    known = instances(top)
    for (kind, name), scope in placed.items():
        if scope is None:
            errors.append(f"{kind} {name} has no hier_scope")
        elif scope not in known:
            errors.append(f"{kind} {name}: placed in unknown instance {' '.join(scope)!r}")
    return top, errors, notes


def cell_counts(top):
    """{instance path: number of cells placed there}, for every instance."""
    counts = {p: 0 for p in instances(top)}
    for (kind, _), scope in placement(top).items():
        if kind == "cell" and scope in counts:
            counts[scope] += 1
    return counts


def check_expectations(counts, expectations, flow):
    errors = []
    for e in expectations:
        if flow not in e.get("flows", [flow]):
            continue
        n = counts.get(tuple(e["scope"]))
        if n is None:
            errors.append(f"scope {e['scope']} not found")
        elif (n > 0) != e["cells"]:
            errors.append(f"scope {e['scope']}: cells={n}, expected {'some' if e['cells'] else 'none'}")
    return errors


def summary(counts):
    return f"cells={sum(counts.values())} in_top={counts[()]}"


def run_case(case, flow, cfg, work):
    work.mkdir(parents=True)
    try:
        outputs = run_flow(case, flow, cfg, work)
        top, errors, notes = check_invariants(*outputs)
        errors += placement_errors(case, flow, cfg, work, outputs[0], outputs[1])
        counts = cell_counts(top)
        errors += check_expectations(counts, cfg.get("expect", []), flow)
    except Exception as ex:  # a broken run is reported like any other failure
        counts, errors, notes = None, [str(ex)], []
    return counts, errors, notes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--case", action="append", help="run only these cases")
    ap.add_argument("--flow", action="append", choices=sorted(FLOWS), help="run only these flows")
    ap.add_argument("--show", action="store_true", help="print the cell counts of every scope")
    ap.add_argument("--keep", action="store_true", help="keep the work directories")
    args = ap.parse_args()

    if not PLUGIN.exists():
        sys.exit(f"{PLUGIN} not found, run make first")

    root = Path(tempfile.mkdtemp(prefix="hier_restore_tests_"))
    jobs = []
    for case in sorted(p for d in CASES for p in d.iterdir() if (p / "expect.json").exists()):
        if args.case and case.name not in args.case:
            continue
        cfg = json.loads((case / "expect.json").read_text())
        jobs += [(case, flow, cfg) for flow in cfg.get("flows", list(FLOWS)) if not args.flow or flow in args.flow]

    # the runs are independent and spend their time in yosys, so they run side by side
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        results = list(pool.map(lambda job: run_case(*job, root / job[0].name / job[1]), jobs))

    failures = 0
    for (case, flow, _), (counts, errors, notes) in zip(jobs, results):
        failures += bool(errors)
        print(f"{'FAIL' if errors else 'ok  '} {case.name:<24} {flow:<8} {summary(counts) if counts else ''}")
        for e in errors:
            print(f"       {e}")
        for n in notes:
            print(f"       note: {n}")
        if args.show and counts:
            for path, n in sorted(counts.items()):
                print(f"       {' '.join(path) or '<top>':<40} cells={n}")
    if args.keep or failures:
        print(f"\nwork directories: {root}")
    else:
        shutil.rmtree(root)
    print(f"\n{failures} failed" if failures else "\nall passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
