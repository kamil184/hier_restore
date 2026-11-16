"""Finds cells that hier_restore placed in a wrong instance, by simulation.

The design before synthesis (RTL) and the synthesized netlist are simulated with the same pseudo-random
inputs. In the RTL every wire still has its name, so the instance of every signal is known. A net of the
netlist whose values equal those of an RTL signal, or their inverse, carries that signal, so it exists in
the instances that signal exists in.

A cell computes the signal at its output. If the cell is placed in instance X but that signal exists
neither in X nor in an instance below X, the cell cannot come from X: the placement is wrong. Inputs are
not checked, since synthesis may legally feed a cell from an equal signal it found in another instance.

The check only proves errors. Outputs that toggle too rarely in the simulation cannot be matched and are
not checked, so finding no error does not prove that there is none.
"""

import json
import re
import subprocess
from collections import defaultdict

CYCLES = 3000
MIN_ACTIVE = 0.02  # a signal must have this share of both values, else matches are not meaningful


def _yosys(script, log):
    proc = subprocess.run(["yosys", "-q", "-l", str(log), "-p", script], capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"simulation failed, see {log}:\n{proc.stderr[-1500:]}")


def _top(netlist):
    return next(m for m in netlist["modules"].values() if m.get("attributes", {}).get("top"))


def _ident(name):
    return name if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_$]*", name) else "\\" + name + " "


def _testbench(ports, path):
    """Drives every input from an xorshift generator. Inputs named like a clock get the clock, inputs
    named like a reset are active for the first cycles and then rarely."""
    lines = ["module tb(input clk);",
             "  reg [63:0] s = 64'h9E3779B97F4A7C15;",
             "  reg [31:0] n = 0;",
             "  always @(posedge clk) begin s <= s ^ (s << 13) ^ (s >> 7) ^ (s << 17) ^ n; n <= n + 1; end"]
    conns, k = [], 0
    for name, port in ports.items():
        width = len(port["bits"])
        low = name.lower()
        if port["direction"] == "input":
            if "clk" in low or "clock" in low:
                value = "{%d{clk}}" % width
            elif "rst" in low or "reset" in low:
                inverted = low.endswith("n") or "_n_" in low
                value = "{%d{%s(n < 4 || s[63:57] == 0)}}" % (width, "~" if inverted else "")
            else:
                value = "{" + ", ".join(f"s[{(k + i) % 64}] ^ n[{(k + i) % 7}]" for i in reversed(range(width))) + "}"
                k += width
        elif port["direction"] == "output":
            lines.append(f"  wire [{width - 1}:0] out_{k};")
            value = f"out_{k}"
            k += 1
        else:
            continue
        conns.append(f".{_ident(name)}({value})")
    lines += [f"  DUT dut({', '.join(conns)});", "endmodule"]
    path.write_text("\n".join(lines) + "\n")


def _signatures(vcd_path, wanted):
    """{(var name, bit): values per cycle} of the variables named in 'wanted' whose bits are 0 or 1 in
    every cycle and active enough, sampled at every falling clock edge, when all signals have settled."""
    ids, cur, samples, scope = defaultdict(list), {}, defaultdict(list), []
    clk = None
    with open(vcd_path) as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            if t[0] == "$scope":
                scope.append(t[2])
            elif t[0] == "$upscope":
                scope.pop()
            elif t[0] == "$var":
                name = ".".join(scope[1:] + [t[4]])
                if name == "clk":
                    clk = t[3]
                elif name in wanted:
                    ids[t[3]].append((name, int(t[2])))
            elif t[0] == "$enddefinitions":
                break
        cycles, prev = 0, "0"
        for line in f:
            line = line.strip()
            if not line or line[0] in "#$":
                continue
            if line[0] in "bB":
                value, ident = line[1:].split()
            else:
                value, ident = line[0], line[1:]
            if ident != clk:
                if ident in ids:
                    cur[ident] = value
                continue
            if prev == "1" and value == "0":
                for i, v in cur.items():
                    samples[i].extend([None] * (cycles - len(samples[i])))
                    samples[i].append(v)
                cycles += 1
            prev = value
    sigs = {}
    for ident, values in samples.items():
        if len(values) < cycles or None in values:
            continue
        for name, width in ids[ident]:
            # VCD leaves out leading bits: they repeat an x or z, else they are 0
            padded = [v.rjust(width, v[0] if v[0] in "xz" else "0") for v in values]
            for b in range(width):
                col = "".join(v[width - 1 - b] for v in padded)
                ones = col.count("1")
                if col.count("0") + ones == len(col) and min(ones, len(col) - ones) >= MIN_ACTIVE * len(col):
                    sigs[(name, b)] = col
    return sigs


def _inverse(sig):
    return sig.translate(str.maketrans("01", "10"))


def find_errors(work, files, top, models, netlist, annotated):
    """Returns the cells of 'annotated' (the netlist after hier_restore) placed in an instance their output
    signal does not exist in, as (cell, instance, port, instances of the signal)."""
    read = " ".join(f'"{f}"' for f in files)
    _testbench(_top(json.loads(netlist.read_text()))["ports"], work / "tb.v")
    _yosys(f"read_verilog {read}; hierarchy -top {top}; proc; memory -nomap; flatten; write_json {work / 'rtl.json'}; "
           f"rename {top} DUT; read_verilog {work / 'tb.v'}; hierarchy -top tb; proc; flatten; memory_nordff; "
           f"sim -clock clk -zinit -n {CYCLES} -vcd {work / 'rtl.vcd'} tb", work / "sim_rtl.log")
    # write_json adds the blackbox definitions of the device cells; they must give way to the models,
    # which are only elaborated where used (-defer), since some take long, such as block RAM
    _yosys(f"read_json {netlist}; delete t:$scopeinfo; delete =A:blackbox; rename {top} DUT; "
           f"read_verilog -defer -noblackbox {models}; read_verilog {work / 'tb.v'}; hierarchy -top tb; proc; flatten; "
           f"tribuf -logic; sim -clock clk -zinit -a -n {CYCLES} -vcd {work / 'net.vcd'} tb", work / "sim_net.log")

    # the instances every RTL signal exists in, from the hdlname of its wires
    instance = {}
    for name, wire in _top(json.loads((work / "rtl.json").read_text()))["netnames"].items():
        hdlname = wire.get("attributes", {}).get("hdlname")
        instance["dut." + name] = tuple(hdlname.split()[:-1]) if hdlname else ()
    exists_in = defaultdict(set)
    for (name, _), sig in _signatures(work / "rtl.vcd", instance).items():
        if name in instance:
            exists_in[sig].add(instance[name])

    module = _top(json.loads(annotated.read_text()))
    names = defaultdict(list)
    for name, wire in module["netnames"].items():
        for i, bit in enumerate(wire["bits"]):
            if isinstance(bit, int):
                names[bit].append(("dut." + name, i))
    outputs = {bit for cell in module["cells"].values() for port, bits in cell["connections"].items()
               if cell.get("port_directions", {}).get(port) == "output" for bit in bits if isinstance(bit, int)}
    net_sigs = _signatures(work / "net.vcd", {name for bit in outputs for name, _ in names[bit]})

    def instances_of(bit):
        for key in names[bit]:
            sig = net_sigs.get(key)
            if sig is not None:
                return exists_in[sig] | exists_in[_inverse(sig)]
        return set()

    errors = []
    for cname, cell in module["cells"].items():
        if cell["type"] == "$scopeinfo":
            continue
        scope = tuple((cell["attributes"].get("hier_scope") or "").split())
        for port, bits in cell["connections"].items():
            if cell.get("port_directions", {}).get(port) != "output":
                continue
            found = None
            for bit in bits:
                where = instances_of(bit) if isinstance(bit, int) else set()
                if where and not any(w[: len(scope)] == scope for w in where):
                    found = (cname, " ".join(scope) or "<top>", port, sorted(" ".join(w) or "<top>" for w in where))
                    break
            if found:
                errors.append(found)
                break
    return errors
