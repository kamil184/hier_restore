# easy_uart_fpga_core

## Source

- https://github.com/alexforencich/verilog-uart at commit `1b867e53af738e4a8bc7c839ca2f1c07f40382dc`
- `example/Arty/fpga/rtl/fpga_core.v` is saved as `fpga_core.v`.
- `rtl/uart.v`, `rtl/uart_tx.v` and `rtl/uart_rx.v` are saved under the same names.

## License

MIT (repository license; copyright notice in each file header).

## Top module

`fpga_core`

## Files to read, in order

1. `fpga_core.v`
2. `uart.v`
3. `uart_tx.v`
4. `uart_rx.v`

## Module tree

```
fpga_core
  uart_inst : uart
    uart_tx_inst : uart_tx
    uart_rx_inst : uart_rx
```

## Yosys results

- `hierarchy -check -top fpga_core`: passes.
- `synth_ice40 -top fpga_core`: passes, 3 `$scopeinfo` objects.
- `stat` cell count after `synth_ice40`: 240 (includes the 3 `$scopeinfo` objects).

## Notes

The Arty wrapper `fpga.v` is not included because it instantiates Xilinx primitives (IBUFG, MMCME2_BASE, BUFG).
`fpga_core.v` itself contains no vendor primitives.
