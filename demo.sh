#!/bin/sh
# Shows the hierarchy before and after hier_restore. Usage: ./demo.sh [synth command]
cd "$(dirname "$0")"
SYNTH="${1:-synth_ice40 -top top}"
make -s hier_restore.so

echo "=== Исходный дизайн (иерархия) ==="
echo "top"
echo "  u_cnt  [counter]"
echo "  u_alu  [alu]"
echo "    u_alu.u_add    [adder]"
echo "    u_alu.u_logic  [logic_unit]"

echo
echo "=== После '$SYNTH': одна плоская простыня ==="
N=$(yosys -m ./hier_restore.so -p "read_verilog tests/cases/demo/design.v; $SYNTH; select -count t:* t:\$scopeinfo %d" 2>&1 \
  | grep -E "objects" | tail -1 | awk '{print $1}')
echo "top  ($N ячеек: SB_LUT4, SB_DFFSR, SB_CARRY, ... без вложенных модулей)"

echo
echo "=== После hier_restore: иерархия восстановлена ==="
yosys -m ./hier_restore.so -p "read_verilog tests/cases/demo/design.v; $SYNTH; hier_restore; write_json hier.json" 2>&1 \
  | sed -n '/Recovered hierarchy/,/^$/p' | grep -v '^$\|Recovered'
echo
echo "JSON: $(pwd)/hier.json"
