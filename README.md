# hier_restore

A yosys pass that runs after synthesis and assigns every cell of the flattened netlist to the module instance it came from.
It never guesses: a cell is placed only as deep as the netlist proves, and where that is not enough, it stays in an enclosing instance or in the top module.

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
| `src/cells.cc` | assigning cells to instances |
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

Every cell gets a `hier_scope` with the instance it is placed in.

### Wire attributes

Every wire gets a `hier_scope` with the instance it lies in.
A wire whose name still tells its instance (`hdlname`, or a plain name in the top module) stays in that instance.
Any other wire lies in the innermost instance that contains all cells connected to it, so that a viewer can route it to each of them.

### JSON report

`-o <file>` writes the same result as JSON, sorted for stable diffs.
For every instance it counts the cells placed in it, and every cell has a status:

| Status | Meaning | Placed in |
|---|---|---|
| `unique` | the cell comes from exactly one instance | that instance |
| `ambiguous` | the cell comes from one or more of several instances: synthesis merged their logic into it, or nothing tells them apart | innermost instance containing all of them |
| `unknown` | nothing tells where the cell comes from | top module |

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
   Each location lies in a module definition, and the cell holds logic of an instance of that module.
   Of several instances of the module none is chosen: all of them count.
2. Synthesis does not always list every source location of merged logic.
   Named wires tell in which instances a net is visible: in the instance the name belongs to and, for ports, in the instantiating one.
   A named net that none of the instances from step 1 can see shows that the cell holds logic of a further instance, so the deepest instance that sees the net counts as well.
3. The cell is placed in the innermost instance that contains all instances that count.
   It is `unique` if that is a single instance and `ambiguous` otherwise.
   A cell without a source location in the design is `unknown` and stays in the top module, since nothing proves where it comes from.

The place can only become coarser by these rules, never be chosen among alternatives.
Cells that synthesis created without a source location, such as most LUTs from ABC, therefore stay in the top module.

## Checking for errors

`tests/simcheck.py` looks for cells placed in a wrong instance.
The design before synthesis and the synthesized netlist are simulated with the same pseudo-random inputs for 3000 cycles.
Before synthesis every wire still has its name, so the instance of every signal is known, and a net of the netlist whose values equal those of such a signal carries it.
A cell placed in an instance in which the signal at its output exists neither there nor below is placed wrongly.
Only outputs are checked: synthesis may feed a cell from an equal signal it found in another instance.
The check proves errors, but it cannot prove their absence: outputs that toggle too rarely cannot be matched.

It found no error on the test designs below and on the two designs of the course, the address decoder and the neorv32 RISC-V SoC (with `synth_ice40`; the GateMate block RAM model cannot be simulated by yosys).
The earlier version of the pass, which also chose instances by majority of neighbours and by the fewest port crossings, placed 2 of 469 cells of `mid_serv_synth` and 13 of 5878 cells of neorv32 wrongly.

## Tests

```sh
make test                                   # all cases with all flows
python3 tests/run.py --case demo --show     # one case, with the cell counts of every instance
```

Each directory in `tests/cases` (hand-written designs) and `tests/examples` (open-source designs) holds a design and an `expect.json`; `tests/run.py` describes the format.
Every case runs with `synth_ice40` and with `synth_gatemate -run :vlog`, the board of the course.
Besides the expectations, every run checks with `tests/simcheck.py` that no cell is placed wrongly, that all cells are annotated consistently, that every wire is placed in a known instance, that the netlist is unchanged apart from the `hier_*` attributes and that a second run yields the identical report.
The second run sees the cells and wires in a different order, so it also catches results that depend on that order.

The hand-written cases:

| Case | What it covers |
|---|---|
| `demo` | logic of `alu`, `adder` and `logic_unit` merged by ABC |
| `deep` | four levels, registered boundaries, each level keeps cells of its own |
| `multi_inst` | three instances of the same module, never told apart, so their cells stay in the top module |
| `dup_inst` | two instances with identical inputs, synthesis keeps one copy |
| `generate` | instances from a generate loop, names with `[i]` and `.`; like `multi_inst`, they are not told apart |
| `top_logic` | logic in the top module and an instance that leaves no cells |
| `addressing` | the address decoder of the course, VHDL translated by GHDL |

The examples are unmodified open-source designs; each `SOURCE.md` names origin, commit, license and module tree.
Their `expect.json` only checks the invariants so far.

| Example | Top | Depth | Repeated instances | License |
|---|---|---|---|---|
| `easy_spi_master` | `fpga_top` | 2 | yes | LGPL-2.1 |
| `easy_sevseg_counter` | `de0_nano_soc_baseline` | 3 | no | MIT |
| `mid_serv_synth` | `serv_synth_wrapper` | 2 | no | ISC |
