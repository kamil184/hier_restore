# hier_restore

A yosys pass that runs after synthesis and assigns every cell of the flattened netlist to the module instance it came from.
Where synthesis merged logic across instance boundaries, the pass does not guess but marks the cell and names the instances involved.

## Build and run

```sh
export PATH="$HOME/OneWareStudio/Packages/NativeTools/osscadsuite/oss-cad-suite/bin:$PATH"
make
yosys -m ./hier_restore.so -p "read_verilog design.v; synth_ice40 -top top; hier_restore -o hier.json; write_json netlist.json"
```

`yosys-config --build` does not work with the OSS CAD Suite on macOS, so the Makefile takes only the flags from `yosys-config` and compiles with the local `clang++`.
The plugin is tied to the yosys version it was built against and must be rebuilt after updating the OSS CAD Suite.

## Source layout

| File | Content |
|---|---|
| `src/types.h`, `src/util.cc` | paths, source locations, cell and instance records, JSON helpers |
| `src/worker.h` | the state of one run and its steps |
| `src/scopes.cc` | instances from `$scopeinfo` cells, and the instances each signal bit is visible in |
| `src/cells.cc` | assigning cells to instances and deciding their status |
| `src/worker.cc` | the order of the steps, writing the attributes, placing wires |
| `src/report.cc` | log output and JSON report |
| `src/pass.cc` | the `hier_restore` command, its help and options |

## Requirements on the input

The design must be flattened.
`flatten` and the synth scripts that flatten (`synth_ice40`, `synth_ecp5`, `synth_gowin`, `synth -flatten`, `synth_xilinx -flatten`) leave a `$scopeinfo` cell for every instance, which names its module and the module's source text.
Instances of modules that were not flattened are skipped with a warning.

`synth_gatemate` ends with `opt_clean -purge`, which removes the `$scopeinfo` cells and all but one name of each net, and changes nothing else.
Run it with `-run :vlog` to stop before that step; the pass reports an error if the `$scopeinfo` cells are missing.
The NetlistViewer synthesizes its restored view this way.

The hierarchy separator is the one `flatten` used, taken from the `flatten.separator` scratchpad variable (default `.`).

## Output format

The netlist itself is not changed; the pass only adds attributes, which `write_json` and `write_rtlil` carry along.

Instance paths follow the convention of the `hdlname` attribute: instance names from the top down, separated by spaces.
The top module is the empty string.
`write_json` appends a blank to strings of the form `/[01xz]* */`, so the top module appears as `" "` there and readers have to strip that blank.

### Cell attributes

| Attribute | Present on | Content |
|---|---|---|
| `hier_scope` | every cell | instance the cell is placed in |
| `hier_status` | every cell | `unique`, `ambiguous` or `unknown` |

| `hier_status` | Meaning | `hier_scope` |
|---|---|---|
| `unique` | the cell comes from exactly one instance | that instance |
| `ambiguous` | the cell comes from one or more of several instances: synthesis merged their logic into it, or nothing tells them apart | innermost instance containing all of them |
| `unknown` | nothing tells where the cell comes from | top module |

### Wire attributes

Every wire gets a `hier_scope` with the instance it lies in.
A wire whose name still tells its instance (`hdlname`, or a plain name in the top module) stays in that instance.
Any other wire lies in the innermost instance that contains all cells connected to it, so that a viewer can route it to each of them.

### JSON report

`-o <file>` writes the same result as JSON, sorted for stable diffs.
For every instance it counts the cells placed in it:

```json
{
  "top": "top",
  "scopes": [
    {"path": ["u_alu", "u_add"], "module": "adder", "cells": 8}
  ],
  "cells": [
    {"name": "y_SB_LUT4_O", "type": "SB_LUT4", "status": "ambiguous", "scope": ["u_alu"]}
  ]
}
```

## How cells are assigned

1. The `src` attribute of a cell lists the source locations it was built from.
   Each location lies in a module definition, which gives the instances of that module as alternatives.
   A cell that kept its name has an `hdlname`, which names its instance directly.
2. Named wires tell in which instances a net is visible: in the instance the name belongs to and, for ports, in the instantiating one.
   A cell can only lie in an instance that sees all its named nets.
   Of those, it lies in the instances that need the fewest port crossings: none where a net is named, one in the parent of such an instance.
   A net named in several instances runs through their ports, so all instances that see it count as named.
3. Wires without a reliable name are local to one instance, so cells connected by them must lie in the same instance.
   Each cell keeps the alternatives most of its neighbours agree with, repeated until nothing changes.
   If all neighbours agree, this is the intersection of their alternatives; a neighbour behind a boundary wire that lost its name is outvoted instead of blocking the others.
4. If exactly one instance remains, the cell is `unique`; if several remain, it is `ambiguous`.
   If none remains, synthesis merged logic of several instances into the cell, and it is `ambiguous` among the instances its source locations point to, plus, for each named net none of them sees, the innermost instance that does.
   An `ambiguous` cell is placed in the innermost instance containing all remaining ones.

## Accuracy

Measured on two VHDL designs of the course, converted to Verilog by GHDL and synthesized with `synth_gatemate -run :vlog` and the options OneWare uses.
The reference is the same design synthesized with `-noflatten`, where every cell stays in its module.
That synthesis cannot optimize across instance boundaries, so it has somewhat more cells and 100% is out of reach for designs where such optimizations happen.
The number is the share of reference cells that the pass placed in the right instance, counted per instance.

| Design | Cells | Placed correctly |
|---|---|---|
| address decoder (master, slave) | 162 | 100% |
| neorv32 RISC-V SoC with peripherals | 11461 | 91.2% |

## Tests

```sh
make test                                   # all cases with all flows
python3 tests/run.py --case demo --show     # one case, with the cell counts of every instance
```

Each directory in `tests/cases` (hand-written designs) and `tests/examples` (open-source designs) holds a design and an `expect.json`; `tests/run.py` describes the format.
Every case runs with `synth_ice40` and with `synth_gatemate -run :vlog`, the board of the course.
Besides the expectations, every run checks that all cells are annotated consistently, that every wire is placed in a known instance, that the netlist is unchanged apart from the `hier_*` attributes and that a second run yields the identical report.
The second run sees the cells and wires in a different order, so it also catches results that depend on that order.

The hand-written cases:

| Case | What it covers |
|---|---|
| `demo` | logic of `alu`, `adder` and `logic_unit` merged by ABC |
| `deep` | four levels, registered boundaries, nothing may be merged |
| `multi_inst` | three instances of the same module, told apart by their nets |
| `dup_inst` | two instances with identical inputs, synthesis keeps one copy |
| `generate` | instances from a generate loop, names with `[i]` and `.` |
| `top_logic` | logic in the top module and an instance that leaves no cells |
| `addressing` | the address decoder of the course, VHDL translated by GHDL |

The examples are unmodified open-source designs; each `SOURCE.md` names origin, commit, license and module tree.
Their `expect.json` only checks the invariants so far.

| Example | Top | Depth | Repeated instances | License |
|---|---|---|---|---|
| `easy_spi_master` | `fpga_top` | 2 | yes | LGPL-2.1 |
| `easy_sevseg_counter` | `de0_nano_soc_baseline` | 3 | no | MIT |
| `mid_serv_synth` | `serv_synth_wrapper` | 2 | no | ISC |
