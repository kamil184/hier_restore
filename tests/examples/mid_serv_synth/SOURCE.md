# mid_serv_synth

## Source

- https://github.com/olofk/serv at commit `f200eb2ed7b69ac1c6b8eddd47654522aeee5ce8`
- All 18 files of `rtl/` are saved under their file names.

## License

ISC (repository license).

## Top module

`serv_synth_wrapper`

## Files to read, in order

1. `serv_synth_wrapper.v`
2. `serv_top.v`
3. `serv_rf_ram_if.v`
4. `serv_ctrl.v`
5. `serv_state.v`
6. `serv_decode.v`
7. `serv_immdec.v`
8. `serv_alu.v`
9. `serv_bufreg.v`
10. `serv_bufreg2.v`
11. `serv_csr.v`
12. `serv_mem_if.v`
13. `serv_rf_if.v`
14. `serv_aligner.v`
15. `serv_compdec.v`
16. `serv_debug.v`
17. `serv_rf_ram.v`
18. `serv_rf_top.v`

## Module tree (elaborated with default parameters)

```
serv_synth_wrapper
  rf_ram_if : serv_rf_ram_if
  cpu : serv_top
    ctrl : serv_ctrl
    state : serv_state
    decode : serv_decode
    immdec : serv_immdec
    alu : serv_alu
    bufreg : serv_bufreg
    bufreg2 : serv_bufreg2
    csr : serv_csr
    mem_if : serv_mem_if
    rf_if : serv_rf_if
```

`serv_aligner` (parameter ALIGN), `serv_compdec` (parameter COMPRESSED) and `serv_debug` (parameter DEBUG) sit in `generate` blocks that are off by default, so yosys drops them.
`serv_rf_ram` and `serv_rf_top` are not instantiated by this top with default parameters.

## Yosys results

- `hierarchy -check -top serv_synth_wrapper`: passes.
- `synth_ice40 -top serv_synth_wrapper`: passes, 12 `$scopeinfo` objects.
- `stat` cell count after `synth_ice40`: 479 (includes the 12 `$scopeinfo` objects).
