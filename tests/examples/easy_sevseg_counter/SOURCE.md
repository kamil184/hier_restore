# easy_sevseg_counter

## Source

- https://github.com/lomishvilim/fpga-seven-segment-counter at commit `443f6efe2de44747c586fc462773bbe56029bfd9`
- `src/de0_nano_soc_baseline.v` is saved as `de0_nano_soc_baseline.v`.
- `src/counters_sevseg.v`, `src/display_sevseg.v` and `src/tick_1hz.v` are saved under the same names.

## License

MIT (repository license).

## Top module

`de0_nano_soc_baseline`

## Files to read, in order

1. `de0_nano_soc_baseline.v`
2. `tick_1hz.v`
3. `counters_sevseg.v`
4. `display_sevseg.v`

## Module tree

```
de0_nano_soc_baseline
  u_tick_1hz : tick_1hz
  u_countdown_hex : countdown_hex
  u_sevseg : sevseg_display
    digits : formatter_4dig
      dec_conv : bcd_converter
```

`counters_sevseg.v` also defines `counter_bcd` and `counter_bin_9999`, which the top does not instantiate (the `counter_bcd` instance sits inside a block comment).

## Yosys results

- `hierarchy -check -top de0_nano_soc_baseline`: passes.
- `synth_ice40 -top de0_nano_soc_baseline`: passes, 5 `$scopeinfo` objects.
- `stat` cell count after `synth_ice40`: 178 (includes the 5 `$scopeinfo` objects).

## Notes

The top is a DE0-Nano board wrapper with many board pins (HPS DDR3, ADC, and so on) that are unused by the logic.
The file contains no vendor primitives.
