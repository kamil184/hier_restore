# easy_apb_uart_rx_fifo

## Source

- https://github.com/Divyajyotipatra10/APB_UART_SLAVE at commit `3a847b43eb5873cf4b9e4b45f1128a9ad6eddd7e`
- `rtl/FIFO_UART_RX.v`, `rtl/FIFO_ALL.v`, `rtl/FIFO_BUFF.v`, `rtl/FIFO_RD.v`, `rtl/FIFO_WR.v` and `rtl/UART_RX.v` are saved under their file names.
- Only the receive side of the UART is used, so `UART_TX.v`, `UART_CLK.v`, `UART_Module.v`, `APB_UART.v` and `APB_Slave.v` are not included.

## License

MIT (repository license).

## Top module

`FIFO_UART_RX`

## Files to read, in order

1. `FIFO_UART_RX.v`
2. `FIFO_ALL.v`
3. `FIFO_BUFF.v`
4. `FIFO_WR.v`
5. `FIFO_RD.v`
6. `UART_RX.v`

## Module tree

```
FIFO_UART_RX
  RX_FIFO_INST : FIFO_ALL
    Buffer : FIFO_BUFF
    Write_module : FIFO_WR
    Read_module : FIFO_RD
  RX_Module : UART_RX
```

## Yosys results

- `hierarchy -check -top FIFO_UART_RX`: passes.
- `synth_ice40 -top FIFO_UART_RX`: passes, 5 `$scopeinfo` objects.
- `stat` cell count after `synth_ice40`: 99 (includes the 5 `$scopeinfo` objects).

## Notes

About 343 non-comment lines in total, slightly above the 300-line guideline for the easy category.
The FIFO storage is a register array that `synth_ice40` maps to flip-flops.
