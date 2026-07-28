"""Generate fluidnc-bob-revD.kicad_sch from design.py — stub+label edition.

Unlike rev C's fully-drawn schematic, rev D uses global labels: every
connected pin gets a 2.54 mm stub wire ending in a global label named for
its design.py net. Connectivity therefore comes straight from design.py
with zero coordinate planning, `kicad-cli sch erc` validates it, and
`verify_netlist.py` diffs the exported netlist against design.py by NAME
(global labels preserve net names exactly).

Placement is a coarse grouped grid purely for human reading during hand
layout; TEXT banners mark the groups.
"""
import uuid, sys, os, json

sys.path.insert(0, os.path.dirname(__file__))
from design import COMPONENTS, DEVKIT_LEFT, DEVKIT_RIGHT

U = lambda: str(uuid.uuid4())
ROOT_UUID = str(uuid.uuid5(uuid.NAMESPACE_DNS, "fluidnc-bob-revD-root"))
PROJECT = "fluidnc-bob-revD"

# ---------------------------------------------------------------- symbols
def rect(x1, y1, x2, y2):
    return (f'(rectangle (start {x1} {y1}) (end {x2} {y2}) '
            '(stroke (width 0.254) (type default)) (fill (type none)))')
def line(pts, w=0.254):
    p = ' '.join(f'(xy {x} {y})' for x, y in pts)
    return (f'(polyline (pts {p}) (stroke (width {w}) (type default)) '
            '(fill (type none)))')
def circle(x, y, r):
    return (f'(circle (center {x} {y}) (radius {r}) '
            '(stroke (width 0.254) (type default)) (fill (type none)))')

SYMS = {}
SYMS["R"] = dict(shapes=[rect(-1.016, -2.54, 1.016, 2.54)],
                 pins=[("1", "~", 0, 5.08, 270, 2.54),
                       ("2", "~", 0, -5.08, 90, 2.54)])
SYMS["R_H"] = dict(shapes=[rect(-2.54, -1.016, 2.54, 1.016)],
                   pins=[("1", "~", -5.08, 0, 0, 2.54),
                         ("2", "~", 5.08, 0, 180, 2.54)])
SYMS["C"] = dict(shapes=[line([(-1.905, -0.762), (1.905, -0.762)]),
                         line([(-1.905, 0.762), (1.905, 0.762)])],
                 pins=[("1", "~", 0, 3.81, 270, 3.048),
                       ("2", "~", 0, -3.81, 90, 3.048)])
SYMS["C_1206"] = SYMS["C"]
SYMS["C_Pol"] = dict(shapes=SYMS["C"]["shapes"] + [
                         line([(2.286, 1.778), (3.556, 1.778)]),
                         line([(2.921, 1.143), (2.921, 2.413)])],
                     pins=SYMS["C"]["pins"])
# D_V: pin1 = K at TOP, pin2 = A at BOTTOM
SYMS["D_V"] = dict(shapes=[line([(-1.27, -1.27), (1.27, -1.27), (0, 1.27),
                                 (-1.27, -1.27)]),
                           line([(-1.27, 1.27), (1.27, 1.27)])],
                   pins=[("1", "K", 0, 5.08, 270, 3.81),
                         ("2", "A", 0, -5.08, 90, 3.81)])
# LED_V: pin2 = A at TOP, pin1 = K at BOTTOM
SYMS["LED_V"] = dict(shapes=[line([(-1.27, 1.27), (1.27, 1.27), (0, -1.27),
                                   (-1.27, 1.27)]),
                             line([(-1.27, -1.27), (1.27, -1.27)]),
                             line([(1.778, 0.254), (2.794, 1.27)]),
                             line([(2.286, -0.508), (3.302, 0.508)])],
                     pins=[("2", "A", 0, 5.08, 270, 3.81),
                           ("1", "K", 0, -5.08, 90, 3.81)])
SYMS["Fuse_H"] = dict(shapes=[rect(-2.54, -1.016, 2.54, 1.016),
                              line([(-2.54, 0), (2.54, 0)])],
                      pins=[("1", "~", -5.08, 0, 0, 2.54),
                            ("2", "~", 5.08, 0, 180, 2.54)])
SYMS["Q_NPN"] = dict(shapes=[line([(0, -2.286), (0, 2.286)]),
                             line([(0, 0.762), (2.54, 2.54)]),
                             line([(0, -0.762), (2.54, -2.54)]),
                             line([(2.54, -2.54), (1.4, -2.36)]),
                             line([(2.54, -2.54), (2.16, -1.45)])],
                     pins=[("2", "E", 2.54, -5.08, 90, 2.54),
                           ("1", "B", -5.08, 0, 0, 5.08),
                           ("3", "C", 2.54, 5.08, 270, 2.54)])
# terminal blocks: pins face RIGHT (blocks sit at the sheet's left edge)
for n in (2, 5):
    body = [rect(-3.81, -(n - 1) * 2.54 - 2.54, 3.81, 2.54)] + \
           [circle(0, -2.54 * i, 0.889) for i in range(n)]
    SYMS[f"TB_0{n}"] = dict(
        shapes=body,
        pins=[(str(i + 1), "~", 7.62, -2.54 * i, 180, 3.81)
              for i in range(n)])
# JST-XH: pins face LEFT (connectors sit toward the sheet's right edge)
for n in (2, 6):
    body = [rect(-3.81, -(n - 1) * 2.54 - 2.54, 3.81, 2.54)] + \
           [rect(-1.27, -2.54 * i - 0.635, 0, -2.54 * i + 0.635)
            for i in range(n)]
    SYMS[f"XH_0{n}"] = dict(
        shapes=body,
        pins=[(str(i + 1), "~", -7.62, -2.54 * i, 0, 3.81)
              for i in range(n)])
# TSR 1-4850WI: 1=Vin left, 2=GND bottom, 3=Vout right
SYMS["TSR1"] = dict(shapes=[rect(-8.89, -6.35, 8.89, 6.35)],
                    pins=[("1", "VIN", -13.97, 2.54, 0, 5.08),
                          ("2", "GND", 0, -11.43, 90, 5.08),
                          ("3", "VOUT", 13.97, 2.54, 180, 5.08)])
# 3-pin jumper header, pins face LEFT
SYMS["HDR_03"] = dict(
    shapes=[rect(-3.81, -7.62, 3.81, 2.54)] +
           [circle(0, -2.54 * i, 0.635) for i in range(3)],
    pins=[(str(i + 1), "~", -7.62, -2.54 * i, 0, 3.81) for i in range(3)])
SYMS["HOLE"] = dict(shapes=[circle(0, 0, 1.6), circle(0, 0, 0.8)], pins=[])
# 74AHCT541: inputs on the RIGHT (facing devkit), outputs LEFT
_in = [(str(k + 2), f"A{k + 1}", 10.16 - 2.54 * k) for k in range(8)]
_out = [(str(18 - k), f"Y{k + 1}", 10.16 - 2.54 * k) for k in range(8)]
SYMS["74AHCT541"] = dict(
    shapes=[rect(-7.62, -17.78, 7.62, 17.78)],
    pins=[(nu, nm, 12.7, y, 180, 5.08) for nu, nm, y in _in] +
         [(nu, nm, -12.7, y, 0, 5.08) for nu, nm, y in _out] +
         [("1", "OE1", -2.54, -22.86, 90, 5.08),
          ("19", "OE2", 2.54, -22.86, 90, 5.08),
          ("20", "VCC", 0, 22.86, 270, 5.08),
          ("10", "GND", 0, -22.86, 90, 5.08)])
# devkit sockets: CONN_1x22 pins LEFT (A1), CONN_1x22R pins RIGHT (A2)
SYMS["CONN_1x22"] = dict(
    shapes=[rect(-10.16, -30.48, 10.16, 30.48)] +
           [circle(-6.35, 26.67 - 2.54 * i, 0.635) for i in range(22)],
    pins=[(str(i + 1), "~", -15.24, 26.67 - 2.54 * i, 0, 5.08)
          for i in range(22)])
SYMS["CONN_1x22R"] = dict(
    shapes=[rect(-10.16, -30.48, 10.16, 30.48)] +
           [circle(6.35, 26.67 - 2.54 * i, 0.635) for i in range(22)],
    pins=[(str(i + 1), "~", 15.24, 26.67 - 2.54 * i, 180, 5.08)
          for i in range(22)])

# ------------------------------------------------------------- placement
PL = {
    # POWER (top-left band)
    "J1": (40.64, 45.72), "F1": (63.5, 43.18), "D1": (80.01, 48.26),
    "C1": (92.71, 48.26), "C2": (105.41, 48.26), "U2": (127.0, 45.72),
    "JP1": (172.72, 43.18), "J12": (195.58, 45.72),
    "C3": (218.44, 48.26), "C4": (231.14, 48.26),
    "R50": (243.84, 48.26), "LED1": (256.54, 48.26),
    # BUFFER (centre-left)
    "U1": (152.4, 129.54),
    **{f"R1{k}": (99.06 + 10.16 * k, 175.26) for k in range(8)},
    **{f"R6{k}": (208.28, 106.68 + 11.43 * k) for k in range(7)},
    # DRIVER TERMINALS (left edge)
    "J2": (41.91, 116.84), "J3": (41.91, 149.86), "J4": (41.91, 182.88),
    # EN BLOCK (bottom-centre-left)
    "R42": (91.44, 218.44), "R43": (104.14, 218.44), "Q2": (118.11, 220.98),
    "R51": (132.08, 218.44), "LED2": (144.78, 218.44),
    # RELAY (bottom-centre)
    "R40": (167.64, 218.44), "R41": (180.34, 218.44), "Q1": (194.31, 220.98),
    "D2": (208.28, 218.44), "J10": (224.79, 220.98),
    # DEVKIT (centre-right)
    "A1": (269.24, 116.84), "A2": (327.66, 116.84),
    # INPUTS (right band, below devkit)
    **{f"R2{k}": (287.02, 175.26 + 17.78 * k) for k in range(5)},
    **{f"R3{k}": (307.34, 177.8 + 17.78 * k) for k in range(5)},
    **{f"C2{k}": (325.12, 180.34 + 17.78 * k) for k in range(5)},
    "J5": (367.03, 177.8), "J6": (367.03, 195.58), "J7": (367.03, 213.36),
    "J8": (367.03, 231.14), "J9": (367.03, 248.92),
    # SD + AUX (bottom-left band)
    "J13": (63.5, 231.14), "J11": (63.5, 254.0),
    # mounting holes (bottom-right corner)
    **{f"H{i}": (386.08 + 7.62 * ((i - 1) % 2), 38.1 + 7.62 * ((i - 1) // 2))
       for i in range(1, 5)},
    # power/misc caps get spots near their owners
    "C5": (175.26, 106.68),
}
# A2 uses the right-handed socket symbol
SYM_OVERRIDE = {"A2": "CONN_1x22R"}

TEXTS = [
    ("POWER: 36V IN -> FUSE -> TVS -> BUCK -> 5V-SRC JUMPER", 40.64, 27.94),
    ("DRIVER TERMINALS: STEP DIR GND +5V(EN+) EN-", 33.02, 99.06),
    ("74AHCT541 @ 5V, 100R series out", 127.0, 99.06),
    ("ESP32-S3 DEVKIT SOCKETS (pin 1 = 3V3, antenna end)", 261.62, 72.39),
    ("INPUTS: 10k pullup / 1k series / 100n  (limits, probe, e-stop)",
     279.4, 162.56),
    ("EN SINK: GPIO8 high = disabled", 91.44, 205.74),
    ("SPINDLE RELAY", 167.64, 205.74),
    ("SD (3V3!) + AUX", 55.88, 220.98),
    ("R10-R17: 100k boot pulldowns", 99.06, 189.23),
]

def sym_of(ref):
    name = SYM_OVERRIDE.get(ref, COMPONENTS[ref]["sym"])
    return name, SYMS[name]

def pin_pos(ref, num):
    name, sym = sym_of(ref)
    for nu, nm, px, py, ang, ln in sym["pins"]:
        if nu == num:
            X, Y = PL[ref]
            return (round(X + px, 2), round(Y - py, 2)), ang
    raise KeyError((ref, num))

# outward direction on the SHEET for a pin drawn at `ang`
OUT = {0: (-1, 0), 180: (1, 0), 270: (0, -1), 90: (0, 1)}

W, LABELS = [], []   # wires: [(x1,y1),(x2,y2)]; labels: (net,x,y,ang)

STUB = 2.54
for ref, c in COMPONENTS.items():
    for num, net in c["pins"].items():
        (x, y), ang = pin_pos(ref, num)
        dx, dy = OUT[ang]
        ex, ey = round(x + dx * STUB, 2), round(y + dy * STUB, 2)
        W.append(((x, y), (ex, ey)))
        lang = {(1, 0): 0, (-1, 0): 180, (0, -1): 90, (0, 1): 270}[(dx, dy)]
        LABELS.append((net, ex, ey, lang))

# overlap sanity check: no two symbol bodies may collide
def bbox(ref):
    name, sym = sym_of(ref)
    X, Y = PL[ref]
    xs, ys = [], []
    for nu, nm, px, py, ang, ln in sym["pins"]:
        xs += [X + px]; ys += [Y - py]
    xs += [X - 4.5, X + 4.5]; ys += [Y - 5, Y + 5]
    return min(xs), min(ys), max(xs), max(ys)

refs = [r for r in COMPONENTS
        if COMPONENTS[r]["sym"] and COMPONENTS[r]["sym"] != "HOLE"]
for i, a in enumerate(refs):
    for b in refs[i + 1:]:
        ax1, ay1, ax2, ay2 = bbox(a)
        bx1, by1, bx2, by2 = bbox(b)
        if ax1 < bx2 and bx1 < ax2 and ay1 < by2 and by1 < ay2:
            print(f"OVERLAP: {a} {b}")
            sys.exit(1)

# ------------------------------------------------------------- emission
NO_PINNUM = {"R", "R_H", "C", "C_Pol", "C_1206", "D_V", "LED_V", "Fuse_H",
             "Q_NPN", "HOLE"}

def emit_lib_symbols(prefix=True):
    out = []
    for name, s in SYMS.items():
        full = f"bobD:{name}" if prefix else name
        out.append(f'    (symbol "{full}" '
                   + ('(pin_numbers hide) ' if name in NO_PINNUM else '')
                   + '(exclude_from_sim no) (in_bom yes) (on_board yes)')
        out.append(f'      (property "Reference" "X" (at 0 3.81 0) '
                   f'(effects (font (size 1.27 1.27)) hide))')
        out.append(f'      (property "Value" "{name}" (at 0 -3.81 0) '
                   '(effects (font (size 1.27 1.27))))')
        out.append('      (property "Footprint" "" (at 0 0 0) '
                   '(effects (font (size 1.27 1.27)) hide))')
        out.append('      (property "Datasheet" "" (at 0 0 0) '
                   '(effects (font (size 1.27 1.27)) hide))')
        out.append(f'      (symbol "{name}_0_1"')
        for sh in s["shapes"]:
            out.append("        " + sh)
        out.append("      )")
        out.append(f'      (symbol "{name}_1_1"')
        for nu, nm, x, y, ang, ln in s["pins"]:
            out.append(f'        (pin passive line (at {x} {y} {ang}) '
                       f'(length {ln}) '
                       f'(name "{nm}" (effects (font (size 1.27 1.27)))) '
                       f'(number "{nu}" (effects (font (size 1.27 1.27)))))')
        out.append("      )")
        out.append("    )")
    return "\n".join(out)

body = []
for ref, c in COMPONENTS.items():
    if c["sym"] is None:
        continue
    name, sym = sym_of(ref)
    X, Y = PL[ref]
    iu = U()
    body.append(f'  (symbol (lib_id "bobD:{name}") (at {X} {Y} 0) '
                '(unit 1) (exclude_from_sim no) (in_bom yes) '
                f'(on_board yes) (dnp no) (uuid "{iu}")')
    tall = name in ("74AHCT541", "CONN_1x22", "CONN_1x22R", "TSR1")
    ry = Y - 33.02 if tall else Y - 8.89
    vy = Y + 33.02 if tall else Y + 8.89
    body.append(f'    (property "Reference" "{ref}" (at {X} {ry} 0) '
                '(effects (font (size 1.27 1.27))))')
    body.append(f'    (property "Value" "{c["value"]}" (at {X} {vy} 0) '
                '(effects (font (size 1.27 1.27))))')
    body.append(f'    (property "Footprint" "{c["fp"]}" (at {X} {Y} 0) '
                '(effects (font (size 1.27 1.27)) hide))')
    body.append(f'    (property "Datasheet" "" (at {X} {Y} 0) '
                '(effects (font (size 1.27 1.27)) hide))')
    for nu, *_ in sym["pins"]:
        body.append(f'    (pin "{nu}" (uuid "{U()}"))')
    body.append(f'    (instances (project "{PROJECT}" '
                f'(path "/{ROOT_UUID}" (reference "{ref}") (unit 1))))')
    body.append("  )")
    for nu, nm, px, py, ang, ln in sym["pins"]:
        if nu not in c["pins"]:
            body.append(f'  (no_connect (at {round(X + px, 2)} '
                        f'{round(Y - py, 2)}) (uuid "{U()}"))')

for (a, b) in W:
    body.append(f'  (wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) '
                f'(stroke (width 0) (type default)) (uuid "{U()}"))')
for txt, x, y in TEXTS:
    body.append(f'  (text "{txt}" (exclude_from_sim no) (at {x} {y} 0) '
                f'(effects (font (size 1.75 1.75) bold) (justify left)) (uuid "{U()}"))')
for name, x, y, ang in LABELS:
    just = {0: "left", 180: "right", 90: "left", 270: "right"}[ang]
    body.append(
        f'  (global_label "{name}" (shape passive) (at {x} {y} {ang}) '
        f'(effects (font (size 1.27 1.27)) (justify {just})) (uuid "{U()}") '
        f'(property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {x} {y} 0) '
        f'(effects (font (size 1.27 1.27)) hide)))')

doc = f'''(kicad_sch (version 20231120) (generator "gen_sch.py")
  (uuid "{ROOT_UUID}")
  (paper "A3")
  (title_block (title "fluidnc-bob rev D — ESP32-S3 FluidNC breakout, 36V single-supply")
    (date "2026-07-27") (rev "D") (company "SharkCNC"))
  (lib_symbols
{emit_lib_symbols()}
  )
{chr(10).join(body)}
  (sheet_instances (path "/" (page "1")))
)
'''

here = os.path.join(os.path.dirname(__file__), "..")
with open(os.path.join(here, "fluidnc-bob-revD.kicad_sch"), "w") as f:
    f.write(doc)
with open(os.path.join(here, "bobD.kicad_sym"), "w") as f:
    f.write('(kicad_symbol_lib (version 20231120) (generator "gen_sch.py")\n'
            + emit_lib_symbols(prefix=False) + '\n)\n')
with open(os.path.join(here, "sym-lib-table"), "w") as f:
    f.write('(sym_lib_table (version 7)\n  (lib (name "bobD")(type "KiCad")'
            '(uri "${KIPRJMOD}/bobD.kicad_sym")(options "")(descr ""))\n)\n')
with open(os.path.join(here, "fp-lib-table"), "w") as f:
    f.write('(fp_lib_table (version 7)\n)\n')
with open(os.path.join(os.path.dirname(__file__), "sch_uuids.json"), "w") as f:
    json.dump({"root": ROOT_UUID}, f, indent=1)
print(f"wrote fluidnc-bob-revD.kicad_sch "
      f"({len(COMPONENTS)} components, {len(W)} stubs, no overlaps)")
