# mid_axi_crossbar

## Source

- https://github.com/alexforencich/verilog-axi at commit `516bd5dadc3365b7f9e225d2af8fe0b8d804fe53`
- `rtl/axi_crossbar.v`, `rtl/axi_crossbar_rd.v`, `rtl/axi_crossbar_wr.v`, `rtl/axi_crossbar_addr.v`, `rtl/axi_register_rd.v`, `rtl/axi_register_wr.v`, `rtl/arbiter.v` and `rtl/priority_encoder.v` are saved under their file names.

## License

MIT (repository license).

## Top module

`axi_crossbar`

## Files to read, in order

1. `axi_crossbar.v`
2. `axi_crossbar_wr.v`
3. `axi_crossbar_rd.v`
4. `axi_crossbar_addr.v`
5. `axi_register_wr.v`
6. `axi_register_rd.v`
7. `arbiter.v`
8. `priority_encoder.v`

## Module tree

```
axi_crossbar
  axi_crossbar_wr_inst : axi_crossbar_wr
    addr_inst : axi_crossbar_addr
    b_arb_inst : arbiter
      priority_encoder_inst : priority_encoder
      priority_encoder_masked : priority_encoder
    a_arb_inst : arbiter
      priority_encoder_inst : priority_encoder
      priority_encoder_masked : priority_encoder
    reg_inst : axi_register_wr
  axi_crossbar_rd_inst : axi_crossbar_rd
    addr_inst : axi_crossbar_addr
    r_arb_inst : arbiter
      priority_encoder_inst : priority_encoder
      priority_encoder_masked : priority_encoder
    a_arb_inst : arbiter
      priority_encoder_inst : priority_encoder
      priority_encoder_masked : priority_encoder
    reg_inst : axi_register_rd
```

Instances inside `generate` loops appear once per source instantiation, so instance names such as `reg_inst` and `addr_inst` are used by several loop bodies.

## Yosys results

- `hierarchy -check -top axi_crossbar`: passes.
- `synth_ice40 -top axi_crossbar`: passes, 74 `$scopeinfo` objects.
- `stat` cell count after `synth_ice40`: 7121 (includes the 74 `$scopeinfo` objects).
