# easy_spi_master

## Source

- https://github.com/janschiefer/verilog_spi at commit `b7ecd170f107e060fb57e267755bde7228b71fe9`
- Files `spi2.v`, `spi_module.v`, `clock_divider.v`, `pos_edge_det.v`, `neg_edge_det.v` are saved unmodified under the same names.
- `testbench.v` is not included.

## License

LGPL-2.1 (repository license field).

## Top module

`fpga_top` (defined in `spi2.v`)

## Files to read, in order

1. `spi2.v`
2. `spi_module.v`
3. `clock_divider.v`
4. `pos_edge_det.v`
5. `neg_edge_det.v`

`spi2.v` also contains the `` `define `` macros used by the other files.

## Module tree

```
fpga_top
  clkdiv : clock_divider
  spi_master : spi_module
    spi_edge_pos : pos_edge_det
    spi_edge_neg : neg_edge_det
  spi_slave : spi_module
    spi_edge_pos : pos_edge_det
    spi_edge_neg : neg_edge_det
```

`spi_module` is instantiated twice and `pos_edge_det` / `neg_edge_det` are instantiated twice each (inside each `spi_module`).

## Yosys results

- `hierarchy -check -top fpga_top`: passes.
- `synth_ice40 -top fpga_top`: passes, 7 `$scopeinfo` objects.
- `stat` cell count after `synth_ice40`: 189 (includes the 7 `$scopeinfo` objects).
