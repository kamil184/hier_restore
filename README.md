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
| `src/scopes.cc` | instances from `$scopeinfo` cells and the instances each signal bit is visible in |
| `src/cells.cc` | assigning cells to instances and deciding their status |
| `src/worker.cc` | the order of the steps, writing the attributes, placing wires |
| `src/report.cc` | log output and JSON report |
| `src/pass.cc` | the `hier_restore` command, its help and options |

## Requirements on the input

The design must be flattened with `$scopeinfo` cells.
`flatten` and the synth scripts that flatten (`synth_ice40`, `synth_ecp5`, `synth_gowin`, `synth -flatten`, `synth_xilinx -flatten`) create them by default.
If the design was flattened with `-noscopeinfo`, the pass stops with an error.
Instances of modules that were not flattened are skipped with a warning.

## Output format

The netlist itself is not changed; the pass only adds attributes, which `write_json` and `write_rtlil` carry along.

Instance paths follow the convention of the `hdlname` attribute: instance names from the top down, separated by spaces.
The top module is the empty string.
`write_json` appends a blank to strings of the form `/[01xz]* */`, so the top module appears as `" "` there and readers have to strip that blank.

### Cell attributes

| Attribute | Present on | Content |
|---|---|---|
| `hier_scope` | every cell | instance the cell is placed in |
| `hier_status` | every cell | `unique`, `inferred`, `merged`, `ambiguous` or `unknown` |
| `hier_candidates` | `merged` and `ambiguous` cells | instance paths involved, separated by `&` for `merged` (all of them) and by `\|` for `ambiguous` (one of them) |
| `hier_evidence` | every cell | what the result is based on: comma separated subset of `hdlname`, `src`, `net`, `name` |

| `hier_status` | Meaning | `hier_scope` |
|---|---|---|
| `unique` | source location and connected nets point to exactly one instance | that instance |
| `inferred` | no source location in the design; placed by the connected nets, or by the cell name if no net is known | that instance |
| `merged` | synthesis combined logic of several instances into this cell | innermost instance containing all candidates |
| `ambiguous` | the cell comes from one of several instances, which one is unknown | innermost instance containing all candidates |
| `unknown` | no usable information | top module |

### Wire attributes

Every wire gets a `hier_scope` with the instance it lies in.
A wire whose name still tells its instance (`hdlname`, or a plain name in the top module) stays in that instance.
Any other wire lies in the innermost instance that contains all cells connected to it, so that a viewer can route it to each of them.

### Instance attributes

Every `$scopeinfo` cell gets a `hier_status`:

| Value | Meaning |
|---|---|
| `complete` | cells were placed here and none of its logic is shared |
| `partial` | cells were placed here and some logic was merged with other instances |
| `absorbed` | no cell of its own, all known logic was merged into cells shared with other instances |
| `empty` | no cell left: the logic was optimized away or merged without a trace |

### JSON report

`-o <file>` writes the same result as JSON, sorted for stable diffs:

```json
{
  "top": "top",
  "scopes": [
    {"path": ["u_alu", "u_add"], "module": "adder", "status": "partial", "cells": {"unique": 8}, "mentions": 8}
  ],
  "cells": [
    {"name": "y_SB_LUT4_O", "type": "SB_LUT4", "status": "merged", "scope": ["u_alu"],
     "candidates": [["u_alu"], ["u_alu", "u_add"]], "evidence": ["net", "src"]}
  ]
}
```

## How cells are assigned

1. The `src` attribute of a cell lists the source locations it was built from.
   Each location lies in a module definition, which gives the instances of that module as alternatives.
2. Named wires tell in which instances a net is visible: in the instance the name belongs to and, for ports, in the instantiating one.
   A cell can only lie in an instance that sees all its named nets.
3. Wires without a reliable name are local to one instance, so cells connected by them must lie in the same instance.
   Each cell keeps the alternatives most of its neighbours agree with, repeated until nothing changes.
   If all neighbours agree, this is the intersection of their alternatives; a neighbour behind a boundary wire that lost its name is outvoted instead of blocking the others.
4. If exactly one instance remains, the cell is `unique` (or `inferred` without source location).
   If none remains, logic of several instances was merged and the cell is `merged`.
   If several remain, the cell is `ambiguous`, with two exceptions that make it `merged`:
   it drives an equally named wire in several instances of the same module, which means synthesis kept one copy of duplicated logic;
   or it is known only through its neighbours, such as a constant driver, and is wired to placed cells in each remaining instance, which means synthesis shared it between them.

The cell name is only used when nothing else is known, because synth scripts name cells after an arbitrary connected wire, which may lie on the other side of an instance boundary.

## Tests

```sh
make test                                   # all cases with all flows
python3 tests/run.py --case demo --show     # one case, with the status of every instance
```

Each directory in `tests/cases` (hand-written designs) and `tests/examples` (open-source designs) holds a design and an `expect.json`; `tests/run.py` describes the format.
Every case runs with `synth_ice40`, `synth -flatten`, `synth_ecp5`, `synth_gowin` and `synth_xilinx -flatten`.
Besides the expectations, every run checks that all cells are annotated consistently, that every wire is placed in a known instance, that the netlist is unchanged apart from the `hier_*` attributes and that a second run yields the identical report.
The second run sees the cells and wires in a different order, so it also catches results that depend on that order.
yosys itself names `$display` cells after memory addresses, so for designs with such cells (`mid_axi_crossbar`) the netlists of two runs differ and reproducibility is not checked.

The hand-written cases:

| Case | What it covers |
|---|---|
| `demo` | logic of `alu`, `adder` and `logic_unit` merged by ABC |
| `deep` | four levels, registered boundaries, nothing may be merged |
| `multi_inst` | three instances of the same module, told apart by their nets |
| `dup_inst` | two instances with identical inputs, synthesis keeps one copy |
| `generate` | instances from a generate loop, names with `[i]` and `.` |
| `top_logic` | logic in the top module and an instance that leaves no cells |

The examples are unmodified open-source designs; each `SOURCE.md` names origin, commit, license and module tree.
Their `expect.json` only checks the invariants so far.

| Example | Top | Depth | Repeated instances | License |
|---|---|---|---|---|
| `easy_uart_fpga_core` | `fpga_core` | 2 | no | MIT |
| `easy_spi_master` | `fpga_top` | 2 | yes | LGPL-2.1 |
| `easy_sevseg_counter` | `de0_nano_soc_baseline` | 3 | no | MIT |
| `easy_wb_arbiter` | `wb_arbiter_2` | 2 | yes | MIT |
| `easy_apb_uart_rx_fifo` | `FIFO_UART_RX` | 2 | no | MIT |
| `mid_serv_synth` | `serv_synth_wrapper` | 2 | no | ISC |
| `mid_axi_crossbar` | `axi_crossbar` | 3 | yes | MIT |
