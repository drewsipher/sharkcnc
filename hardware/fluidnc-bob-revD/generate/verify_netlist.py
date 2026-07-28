"""Diff KiCad's exported netlist against design.py — the authoritative
check that the schematic wires exactly the intended circuit.

Rev D uses global labels everywhere, so every multi-pin net keeps its
design.py NAME in the export — compare both names and pad sets directly.
"""
import subprocess, sys, os, re

here = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, os.path.dirname(__file__))
from design import nets

out = os.path.join(here, "generate", "_netlist.net")
subprocess.run(["kicad-cli", "sch", "export", "netlist", "-o", out,
                os.path.join(here, "fluidnc-bob-revD.kicad_sch")],
               check=True, capture_output=True)

kicad = {}          # name -> set((ref, pin))
in_nets = cur = ref = None
for raw in open(out):
    s = raw.strip()
    if s.startswith("(nets"):
        in_nets = True
    if not in_nets:
        continue
    m = re.match(r'\(name "(.*)"\)', s)
    if m:
        cur = m.group(1)
        kicad[cur] = set()
        continue
    m = re.match(r'\(ref "(.*)"\)', s)
    if m:
        ref = m.group(1)
        continue
    m = re.match(r'\(pin "(.*)"\)', s)
    if m and cur and ref:
        kicad[cur].add((ref, m.group(1)))
        ref = None

want = {net: set(pads) for net, pads in nets().items()}
ok = True
for net, pads in sorted(want.items()):
    got = kicad.get(net)
    if got is None:
        print(f"MISSING net {net} (want {sorted(pads)})")
        ok = False
    elif got != pads:
        print(f"MISMATCH {net}: diff {sorted(got ^ pads)}")
        ok = False
extra = {n: p for n, p in kicad.items()
         if n not in want and len(p) > 1}
for n, p in sorted(extra.items()):
    print(f"UNEXPECTED kicad net {n}: {sorted(p)}")
    ok = False
print("NETLIST VERIFIED OK" if ok else "NETLIST ERRORS", flush=True)
sys.exit(0 if ok else 1)
