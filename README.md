# hier_restore

A yosys pass that runs after synthesis, assigns every cell of the flattened netlist to the module instance it came from and rebuilds the modules of the instances.
It never guesses: a cell is placed only as deep as the netlist proves, and where that is not enough, it stays in an enclosing instance or in the top module.

## Build and run

```sh
export PATH="$HOME/OneWareStudio/Packages/NativeTools/osscadsuite/oss-cad-suite/bin:$PATH"
make
yosys -m ./hier_restore.so -p "read_verilog design.v; synth_ice40 -top top; hier_restore; write_json netlist.json"
```

`yosys-config --build` does not work with the OSS CAD Suite on macOS, so the Makefile takes only the flags from `yosys-config` and compiles with the local `clang++`.
The plugin is tied to the yosys version it was built against and must be rebuilt after updating the OSS CAD Suite.

## Source layout

The whole pass is one file, `src/hier_restore.cc`, to be read from top to bottom.
Its first comment describes the algorithm.
Then come small helpers, the algorithm itself in the order it runs, and at the end the registration of the `hier_restore` command in yosys.

## Requirements on the input

The design must be flattened.
`flatten` and the synth scripts that flatten (`synth_ice40`, `synth_ecp5`, `synth_gowin`, `synth -flatten`, `synth_xilinx -flatten`) leave a `$scopeinfo` cell for every instance, which names its module and the module's source text.
Instances of modules that were not flattened are skipped with a warning.

`synth_gatemate` ends with `opt_clean -purge`, which removes the `$scopeinfo` cells and all but one name of each net, and changes nothing else.
Run it with `-run :vlog` to stop before that step; the pass reports an error if the `$scopeinfo` cells are missing.
The NetlistViewer synthesizes its restored view this way.

The hierarchy separator is the one `flatten` used, taken from the `flatten.separator` scratchpad variable (default `.`).

## Output format

The pass turns the flattened netlist into a hierarchical one again; the logic itself is not changed.
`write_json` writes it like any other hierarchical netlist, so viewers such as the NetlistReaderBackend draw it without changes.

Every instance becomes a module of its own, named `<module>$<instance>`, for example `adder$u_alu.u_add`, with the `hdlname` of the original module.
It holds the cells placed in the instance, named without the instance path, and a cell for each of its child instances.
The top module keeps its name and ports.

A signal that is driven outside an instance and read inside becomes an input port of its module, one driven inside and read outside an output port.
A port is named after a net of its signals, preferring the nets of the instance itself, then visible names, names from the source text and wider nets.
Every module gets the nets of its instance that it uses, and for signals still without a net, a net of another instance that carries them.
The `$scopeinfo` cells are dropped, the hierarchy itself now holds what they described.

Every cell keeps the instance it was placed in as attribute `hier_scope`, so that it stays known after a `flatten`.
Instance paths follow the convention of the `hdlname` attribute: instance names from the top down, separated by spaces.
The top module is the empty string.
`write_json` appends a blank to strings of the form `/[01xz]* */`, so the top module appears as `" "` there and readers have to strip that blank.

A wire whose name still tells its instance (`hdlname`, or a plain name in the top module) lies in that instance.
Any other wire lies in the innermost instance that contains all cells connected to it.

## How cells are assigned

1. The `src` attribute of a cell lists the source locations it was built from.
   Each location lies in a module definition, and the cell holds logic of an instance of that module.
   Of several instances of the module none is chosen: all of them count.
2. Synthesis does not always list every source location of merged logic.
   Named wires tell in which instances a net is visible: in the instance the name belongs to and, for ports, in the instantiating one.
   A named net that none of the instances from step 1 can see shows that the cell holds logic of a further instance, so the deepest instance that sees the net counts as well.
3. The cell is placed in the innermost instance that contains all instances that count.
   A cell without a source location in the design stays in the top module, since nothing proves where it comes from.

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
Besides the expectations, every run checks that every instance became a module holding the cells placed in it, and that a second run places every cell the same way.
The restored netlist is flattened again for the other checks: it must have the same cells as before and, simulated with `tests/simcheck.py`, compute the same outputs, and no cell may be placed wrongly.
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
