"""Compare KiCad's exported netlist against design.py — the authoritative
check that the drawn schematic wires exactly the intended circuit.

Compares CONNECTIVITY PARTITIONS (sets of pads that must be joined), since
KiCad auto-names unlabeled nets. Power nets are additionally checked by name.
Line-based parse of KiCad 10's pretty-printed netlist format.
"""
import subprocess, sys, os, re

here = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, os.path.dirname(__file__))
from design import nets

out = os.path.join(here, "generate", "_netlist.net")
subprocess.run(["kicad-cli", "sch", "export", "netlist", "-o", out,
                os.path.join(here, "fluidnc-bob.kicad_sch")],
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

kicad = {n: frozenset(p) for n, p in kicad.items()}
want = {net: frozenset(pads) for net, pads in nets().items()}

ok = True
want_parts = {p for p in want.values() if len(p) > 1}
kicad_parts = {p for p in kicad.values() if len(p) > 1}
for p in sorted(want_parts - kicad_parts, key=sorted):
    net = next(n for n, pp in want.items() if pp == p)
    print(f"PARTITION MISMATCH for {net} (want {sorted(p)}):")
    for n, pp in kicad.items():
        if pp & p:
            print(f"   kicad {n}: symmetric diff {sorted(pp ^ p)}")
    ok = False
for p in sorted(kicad_parts - want_parts, key=sorted):
    net = next(n for n, pp in kicad.items() if pp == p)
    print(f"UNEXPECTED kicad net {net}: {sorted(p)}")
    ok = False
for pn in ("+5V", "+3V3", "GND", "VCC_BUF"):
    if kicad.get(pn) != want[pn]:
        print(f"NAME MISMATCH {pn}: diff "
              f"{sorted(kicad.get(pn, frozenset()) ^ want[pn])}")
        ok = False
print("NETLIST VERIFIED OK" if ok else "NETLIST ERRORS", flush=True)
sys.exit(0 if ok else 1)
