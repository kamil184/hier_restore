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
  - every instance became a module, and every cell lies in the module of the instance in its hier_scope,
  - flattened again, the netlist has the same cells and computes the same outputs as before (simcheck.py),
  - no cell is placed in an instance its output signal does not exist in, found by simulation (simcheck.py),
  - a second run places every cell the same way.
"""

import argparse
import json
from collections import Counter
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
    """Writes the netlist before the pass, after it, and after it flattened again; the cells keep the instance
    they were placed in as hier_scope through 'flatten'."""
    files = " ".join(f'"{case / f}"' for f in cfg.get("files", ["design.v"]))
    synth = FLOWS[flow].format(top=cfg.get("top", "top"))
    base, restored, flat, again = (work / f for f in ("base.json", "restored.json", "flat.json", "flat2.json"))
    yosys(f"read_verilog {files}; {synth}; write_json {base}; hier_restore; check -assert; write_json {restored}; "
          f"flatten; write_json {flat}", work / "run.log")
    # The write_json before the pass changes the order of cells and wires the pass sees, so the second run
    # also catches results that depend on the order of objects in the design.
    yosys(f"read_verilog {files}; {synth}; hier_restore; flatten; write_json {again}", work / "run2.log")
    return base, restored, flat, again


def simulation_errors(case, flow, cfg, work, base, flat):
    files = [case / f for f in cfg.get("files", ["design.v"])]
    top, models = cfg.get("top", "top"), MODELS[flow]
    errors = simcheck.find_errors(work, files, top, models, flat, flat)
    errors = [f"cell {c} placed in {s}, but its output {p} exists only in {', '.join(w)}" for c, s, p, w in errors]
    if not simcheck.same_outputs(work, top, models, base, flat):
        errors.append("the restored netlist computes other outputs than the one before hier_restore")
    return errors


def top_module(netlist):
    return next(m for m in netlist["modules"].values() if m.get("attributes", {}).get("top"))


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
    """{cell name: instance path} from the hier_scope attributes of the netlist flattened again."""
    return {name: tuple((json_string(c.get("attributes", {}).get("hier_scope")) or "").split())
            for name, c in top["cells"].items() if c["type"] != "$scopeinfo"}


def cell_types(top):
    return Counter(c["type"] for c in top["cells"].values() if c["type"] != "$scopeinfo")


def module_errors(restored, known):
    """Walks the instance tree of the restored netlist: every instance must be a module, and every cell of a
    module must carry the instance of the module as hier_scope."""
    modules = restored["modules"]
    errors, found = [], set()

    def walk(module, path):
        found.add(path)
        for name, cell in module["cells"].items():
            child = modules.get(cell["type"])
            if child is not None and not child.get("attributes", {}).get("blackbox"):
                walk(child, path + (name,))
            elif placement({"cells": {name: cell}})[name] != path:
                errors.append(f"cell {name} lies in {' '.join(path) or '<top>'}, but its hier_scope says otherwise")

    walk(top_module(restored), ())
    errors += [f"instance {' '.join(p)} is not a module" for p in sorted(known - found)]
    errors += [f"module of unknown instance {' '.join(p)}" for p in sorted(found - known)]
    return errors


def check_invariants(base, restored, flat, again):
    errors, notes = [], []
    base_top, flat_top = top_module(json.loads(base.read_text())), top_module(json.loads(flat.read_text()))
    placed = placement(flat_top)
    known = instances(base_top)

    # yosys itself names some cells after memory addresses ($display$0x...), which then differ between runs
    again_placed = placement(top_module(json.loads(again.read_text())))
    if set(again_placed) != set(placed):
        notes.append("synthesis is not deterministic for this design, reproducibility not checked")
    elif again_placed != placed:
        errors.append("second run placed cells differently")

    if cell_types(flat_top) != cell_types(base_top):
        errors.append("the restored netlist has other cells than the one before hier_restore")
    errors += module_errors(json.loads(restored.read_text()), known)
    return placed, known, errors, notes


def cell_counts(placed, known):
    """{instance path: number of cells placed there}, for every instance."""
    counts = {p: 0 for p in known}
    for scope in placed.values():
        if scope in counts:
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
        base, restored, flat, again = run_flow(case, flow, cfg, work)
        placed, known, errors, notes = check_invariants(base, restored, flat, again)
        errors += simulation_errors(case, flow, cfg, work, base, flat)
        counts = cell_counts(placed, known)
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
