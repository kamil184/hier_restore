# easy_wb_arbiter

## Source

- https://github.com/alexforencich/verilog-wishbone at commit `d1fa24a0e5634160536c199eab42f1af720d14ff`
- `rtl/wb_arbiter_2.v`, `rtl/arbiter.v` and `rtl/priority_encoder.v` are saved under their file names.

## License

MIT (repository license).

## Top module

`wb_arbiter_2`

## Files to read, in order

1. `wb_arbiter_2.v`
2. `arbiter.v`
3. `priority_encoder.v`

## Module tree

```
wb_arbiter_2
  arb_inst : arbiter
    priority_encoder_inst : priority_encoder
    priority_encoder_masked : priority_encoder
```

## Yosys results

- `hierarchy -check -top wb_arbiter_2`: passes.
- `synth_ice40 -top wb_arbiter_2`: passes, 3 `$scopeinfo` objects.
- `stat` cell count after `synth_ice40`: 158 (includes the 3 `$scopeinfo` objects).
