# addressing

The address decoder example of the course (`addressierungsbeispiel`: `addressing` with one `addr_master` and one `addr_slave`), written in VHDL.
`design.v` is GHDL's translation of it, as OneWare and the NetlistViewer produce it before synthesis:

```sh
ghdl -i --std=93c --workdir=build addr_master.vhd addr_slave.vhd addressing.vhd
ghdl -m --std=93c --workdir=build addressing
ghdl --synth --std=93c --workdir=build --out=verilog -o=design.v addressing
```

With `synth_ice40`, one flip-flop of the slave absorbs the inverted reset from the top module and is ambiguous between both.
