"""Generate fluidnc-bob.kicad_sch from design.py — drawn-schematic edition.

Real wires, junction dots, and power symbols; zero net labels. Sections:
  top-left: power entry          centre: devkit <-> buffer bus + pulldowns
  left: output wire pads         bottom-left: relay driver
  right: input conditioning, SD pads

All coordinates are hand-planned on a 1.27 mm grid. A self-check verifies
every design.py pin lands on a wire endpoint or power-symbol attach point,
and `verify_netlist.py` diffs KiCad's exported netlist against design.py.
"""
import uuid, sys, os, json
sys.path.insert(0, os.path.dirname(__file__))
from design import COMPONENTS, DEVKIT_LEFT, DEVKIT_RIGHT

U = lambda: str(uuid.uuid4())
ROOT_UUID = str(uuid.uuid5(uuid.NAMESPACE_DNS, "fluidnc-bob-root"))

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
# passives -----------------------------------------------------------------
SYMS["R"] = dict(shapes=[rect(-1.016, -2.54, 1.016, 2.54)],
                 pins=[("1", "~", 0, 5.08, 270, 2.54),
                       ("2", "~", 0, -5.08, 90, 2.54)])
SYMS["R_H"] = dict(shapes=[rect(-2.54, -1.016, 2.54, 1.016)],
                   pins=[("1", "~", 5.08, 0, 180, 2.54),
                         ("2", "~", -5.08, 0, 0, 2.54)])
SYMS["C"] = dict(shapes=[line([(-1.905, -0.762), (1.905, -0.762)]),
                         line([(-1.905, 0.762), (1.905, 0.762)])],
                 pins=[("1", "~", 0, 3.81, 270, 3.048),
                       ("2", "~", 0, -3.81, 90, 3.048)])
SYMS["C_Pol"] = dict(shapes=SYMS["C"]["shapes"] + [
                         line([(2.286, 1.778), (3.556, 1.778)]),
                         line([(2.921, 1.143), (2.921, 2.413)])],
                     pins=SYMS["C"]["pins"])
_dio_v = [line([(-1.27, 1.27), (1.27, 1.27), (0, -1.27), (-1.27, 1.27)]),
          line([(-1.27, -1.27), (1.27, -1.27)])]  # pointing down: K bottom?
# D_V: pin1 = K at TOP, pin2 = A at BOTTOM; triangle points up toward K
SYMS["D_V"] = dict(shapes=[line([(-1.27, -1.27), (1.27, -1.27), (0, 1.27),
                                 (-1.27, -1.27)]),
                           line([(-1.27, 1.27), (1.27, 1.27)])],
                   pins=[("1", "K", 0, 5.08, 270, 3.81),
                         ("2", "A", 0, -5.08, 90, 3.81)])
# D (horizontal): pin1 = K left, pin2 = A right; triangle points left
SYMS["D"] = dict(shapes=[line([(1.27, 1.27), (1.27, -1.27), (-1.27, 0),
                               (1.27, 1.27)]),
                         line([(-1.27, 1.27), (-1.27, -1.27)])],
                 pins=[("1", "K", -3.81, 0, 0, 2.54),
                       ("2", "A", 3.81, 0, 180, 2.54)])
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
SYMS["Polyfuse"] = SYMS["Fuse_H"]  # alias, unused now but harmless
SYMS["Q_NMOS"] = dict(shapes=[line([(0, 2.54), (2.54, 2.54)]),
                              line([(0, -2.54), (2.54, -2.54)]),
                              line([(0, -3.302), (0, 3.302)]),
                              line([(-0.762, -2.54), (-0.762, 2.54)])],
                      pins=[("1", "S", 2.54, -5.08, 90, 2.54),
                            ("2", "G", -5.08, 0, 0, 4.318),
                            ("3", "D", 2.54, 5.08, 270, 2.54)])
SYMS["Q_NPN"] = dict(shapes=[line([(0, -2.286), (0, 2.286)]),      # base bar
                             line([(0, 0.762), (2.54, 2.54)]),     # collector
                             line([(0, -0.762), (2.54, -2.54)]),   # emitter
                             line([(2.54, -2.54), (1.4, -2.36)]),  # arrow
                             line([(2.54, -2.54), (2.16, -1.45)])],
                     pins=[("2", "E", 2.54, -5.08, 90, 2.54),   # SOT-23 numbers
                           ("1", "B", -5.08, 0, 0, 5.08),
                           ("3", "C", 2.54, 5.08, 270, 2.54)])
# wire-pad strips: PadS_0N pins face LEFT (for right edge of sheet),
# PadS_R_0N pins face RIGHT (for left edge of sheet)
for n in (2, 3, 6):
    body = [rect(-3.81, -(n - 1) * 2.54 - 2.54, 3.81, 2.54)] + \
           [circle(0, -2.54 * i, 0.889) for i in range(n)]
    SYMS[f"PadS_0{n}"] = dict(
        shapes=body,
        pins=[(str(i + 1), "~", -7.62, -2.54 * i, 0, 3.81) for i in range(n)])
    SYMS[f"PadS_R_0{n}"] = dict(
        shapes=body,
        pins=[(str(i + 1), "~", 7.62, -2.54 * i, 180, 3.81) for i in range(n)])

# 74HC541, mirrored: inputs on the RIGHT (facing devkit), outputs LEFT
_in = [(str(k + 2), f"A{k + 1}", 10.16 - 2.54 * k) for k in range(8)]
_out = [(str(18 - k), f"Y{k + 1}", 10.16 - 2.54 * k) for k in range(8)]
SYMS["74HC541"] = dict(
    shapes=[rect(-7.62, -17.78, 7.62, 17.78)],
    pins=[(nu, nm, 12.7, y, 180, 5.08) for nu, nm, y in _in] +
         [(nu, nm, -12.7, y, 0, 5.08) for nu, nm, y in _out] +
         [("1", "OE1", -2.54, -22.86, 90, 5.08),
          ("19", "OE2", 2.54, -22.86, 90, 5.08),
          ("20", "VCC", 0, 22.86, 270, 5.08),
          ("10", "GND", 0, -22.86, 90, 5.08)])
# NOTE: pins 1/19/10 all at y=-22.86 with x -2.54/+2.54/0 — distinct points.

SYMS["ESP32_S3_DevKit_44"] = dict(
    shapes=[rect(-17.78, -30.48, 17.78, 30.48)],
    pins=[(str(p), nm, -22.86, 26.67 - 2.54 * (p - 1), 0, 5.08)
          for p, nm, _ in DEVKIT_LEFT] +
         [(str(p), nm, 22.86, 26.67 - 2.54 * (p - 23), 180, 5.08)
          for p, nm, _ in DEVKIT_RIGHT])

# power symbols: GND/+5V/+3V3/PWR_FLAG come verbatim from KiCad's official
# power library; only VCC_BUF (project-specific rail) stays custom.
SYMS["VCC_BUF"] = dict(shapes=[line([(0, 0), (0, 1.524)]),
                               line([(-1.016, 1.524), (1.016, 1.524)])],
                       power=True,
                       pins=[("1", "VCC_BUF", 0, 0, 90, 0)])

def official_power_symbol(name):
    """Extract a symbol block verbatim from KiCad's power library."""
    txt = open("/usr/share/kicad/symbols/power.kicad_sym").read()
    key = f'(symbol "{name}"'
    i = txt.index(key)
    depth, j = 0, i
    while True:
        if txt[j] == '(':
            depth += 1
        elif txt[j] == ')':
            depth -= 1
            if depth == 0:
                break
        j += 1
    block = txt[i:j + 1]
    return block.replace(key, f'(symbol "power:{name}"', 1)

OFFICIAL_POWER = {n: official_power_symbol(n)
                  for n in ("GND", "+5V", "+3V3", "PWR_FLAG")}

# ------------------------------------------------------------- placement
# ref -> (x, y).  All hand-planned; see coordinate ledger in comments.
PL = {
    "A1": (238.76, 139.70),
    "U1": (172.72, 130.81),
    # pulldowns hang from the input bus, lanes x = 187.96 + 3.81k
    **{f"R1{k}": (187.96 + 3.81 * k, 152.40) for k in range(8)},
    # output pad groups, left edge
    "J2": (114.30, 120.65), "J3": (114.30, 133.35), "J4": (114.30, 146.05),
    "J8": (114.30, 158.75),
    # relay block
    "J7": (114.30, 175.26), "D1": (146.05, 172.72), "R40": (154.94, 185.42),
    "R41": (154.94, 195.58), "Q1": (162.56, 190.50),
    # power entry
    "J1": (35.56, 96.52), "F1": (53.34, 96.52), "D2": (66.04, 101.60),
    "C1": (73.66, 100.33), "C3": (81.28, 100.33), "R50": (88.90, 101.60),
    "LED1": (88.90, 114.30),
    # buffer VCC drop
    "D3": (176.53, 105.41), "C2": (152.40, 109.22),
    # input conditioning channels, right of J9, at y = 152.40 + 20.32k
    **{f"R3{k}": (330.20, 152.40 + 20.32 * k) for k in range(4)},
    **{f"R2{k}": (337.82, 147.32 + 20.32 * k) for k in range(4)},
    **{f"C2{k}": (342.90, 156.21 + 20.32 * k) for k in range(4)},
    "J5": (355.60, 152.40), "J6": (355.60, 172.72),
    "J10": (355.60, 193.04), "J11": (355.60, 213.36),
    # SD pads, tucked under the devkit's SD pin cluster
    "J9": (285.75, 149.86),
}

def pin_pos(ref, num):
    sym = SYMS[COMPONENTS[ref]["sym"]]
    for nu, nm, px, py, ang, ln in sym["pins"]:
        if nu == num:
            X, Y = PL[ref]
            return (round(X + px, 2), round(Y - py, 2))
    raise KeyError((ref, num))

def P(ref, num):  # shorthand
    return pin_pos(ref, num)

# ------------------------------------------------------------------ wires
W = []   # list of point-lists (polylines)
JUNC = []  # explicit junction dots
POWER = []  # (symname, x, y)
FLAGS = []  # PWR_FLAG points
LABELS = []  # (netname, x, y, angle) global labels

def w(*pts):
    W.append([(round(x, 2), round(y, 2)) for x, y in pts])

def gnd(x, y):
    POWER.append(("GND", round(x, 2), round(y, 2)))
def p5(x, y):
    POWER.append(("+5V", round(x, 2), round(y, 2)))
def p33(x, y):
    POWER.append(("+3V3", round(x, 2), round(y, 2)))
def vbuf(x, y):
    POWER.append(("VCC_BUF", round(x, 2), round(y, 2)))
def jd(x, y):
    JUNC.append((round(x, 2), round(y, 2)))
def lbl(name, x, y, ang):
    LABELS.append((name, round(x, 2), round(y, 2), ang))

# --- devkit <-> buffer input bus (straight wires, pads 4..11 = A1..A8) ---
for k in range(8):
    dk = P("A1", str(4 + k))          # (215.9, 120.65 + 2.54k)
    u = P("U1", str(2 + k))           # (185.42, same y)
    w(u, dk)
    # pulldown tap
    lane_x = 187.96 + 3.81 * k
    rtop = P(f"R1{k}", "1")           # (lane_x, 147.32)
    w((lane_x, dk[1]), rtop)
    jd(lane_x, dk[1])
# pulldown GND rail
rail_y = 158.75
for k in range(8):
    lane_x = 187.96 + 3.81 * k
    w(P(f"R1{k}", "2"), (lane_x, rail_y))   # (lane_x,157.48) -> rail
w((187.96, rail_y), (214.63, rail_y))
for k in range(1, 7):
    jd(187.96 + 3.81 * k, rail_y)
w((201.93, rail_y), (201.93, 161.29))
jd(201.93, rail_y)
gnd(201.93, 161.29)

# --- U1 housekeeping: OEs + GND to a common point, VCC_BUF network -------
oe1, oe2, g10 = P("U1", "1"), P("U1", "19"), P("U1", "10")
w(g10, (g10[0], 158.75), (g10[0], 161.29))
w(oe1, (oe1[0], 158.75), (g10[0], 158.75))
w(oe2, (oe2[0], 158.75), (g10[0], 158.75))
jd(g10[0], 158.75)
gnd(g10[0], 161.29)
vcc = P("U1", "20")                       # (172.72, 107.95)
w(vcc, (vcc[0], 105.41))
w((vcc[0], 105.41), (152.40, 105.41))     # to C2
jd(vcc[0], 105.41)                        # T: D3.K also lands here
c2_1, c2_2 = P("C2", "1"), P("C2", "2")   # (152.4, 105.41)/(152.4, 113.03)
w(c2_2, (c2_2[0], 115.57))
gnd(c2_2[0], 115.57)
d3k, d3a = P("D3", "1"), P("D3", "2")     # K=(172.72,105.41) A=(180.34,105.41)
w(d3a, (182.88, 105.41), (182.88, 102.87))
p5(182.88, 102.87)
vbuf(167.64, 102.87)
w((167.64, 105.41), (167.64, 102.87))
jd(167.64, 105.41)
FLAGS.append((170.18, 105.41))
jd(170.18, 105.41)

# --- U1 outputs -> pad groups: net labels both ends (no crossing lanes) --
OUT_NETS = ["XSTEP", "XDIR", "YSTEP", "YDIR", "ZSTEP", "ZDIR",
            "SPIN5", "AUX1"]

def out_pin(k):  # Y1..Y8 = pins 18..11
    return P("U1", str(18 - k))

for k, net in enumerate(OUT_NETS):
    src = out_pin(k)
    w(src, (156.21, src[1]))
    lbl(net, 156.21, src[1], 180)
for ref, num, net in [("J2", "1", "XSTEP"), ("J2", "2", "XDIR"),
                      ("J3", "1", "YSTEP"), ("J3", "2", "YDIR"),
                      ("J4", "1", "ZSTEP"), ("J4", "2", "ZDIR"),
                      ("J8", "1", "AUX1")]:
    px, py = P(ref, num)
    w((px, py), (125.73, py))
    lbl(net, 125.73, py, 0)
# pad-group GND stubs
for ref, num in [("J2", "3"), ("J3", "3"), ("J4", "3"), ("J8", "2")]:
    px, py = P(ref, num)
    w((px, py), (124.46, py), (124.46, py + 2.54))
    gnd(124.46, py + 2.54)

# --- relay block (fed via SPIN5 label) -----------------------------------
w(P("R40", "1"), (157.48, 180.34))
lbl("SPIN5", 157.48, 180.34, 0)
ngate = P("R40", "2")                                   # (154.94, 190.5)
w(ngate, P("Q1", "1"))  # base
w(ngate, P("R41", "1"))
jd(*ngate)
w(P("R41", "2"), (154.94, 203.20))
gnd(154.94, 203.20)
w(P("Q1", "2"), (165.10, 198.12))  # emitter
gnd(165.10, 198.12)
qd = P("Q1", "3")                                       # (165.1, 185.42)
w(qd, (165.10, 177.80), P("J7", "2"))                   # RLY_N rail
d1a = P("D1", "2")                                      # (146.05, 177.8)
jd(*d1a)
d1k = P("D1", "1")                                      # (146.05, 167.64)
w(d1k, (146.05, 165.10))
p5(146.05, 165.10)
j7_5v = P("J7", "1")                                    # (121.92, 175.26)
w(j7_5v, (124.46, 175.26), (124.46, 172.72))
p5(124.46, 172.72)

# --- power entry ---------------------------------------------------------
w(P("J1", "1"), P("F1", "1"))
w(P("F1", "2"), (66.04, 96.52), (73.66, 96.52), (81.28, 96.52),
  (88.90, 96.52), (93.98, 96.52), (93.98, 93.98))
p5(93.98, 93.98)
FLAGS.append((60.96, 96.52))
jd(60.96, 96.52)
for x in (66.04, 73.66, 81.28, 88.90):
    jd(x, 96.52)
w(P("J1", "2"), (43.18, 101.60), (45.72, 101.60))
gnd(45.72, 101.60)
FLAGS.append((43.18, 101.60))
jd(43.18, 101.60)
# D2 K is already on the +5V rail point (66.04, 96.52)
w(P("D2", "2"), (66.04, 109.22))
gnd(66.04, 109.22)
for ref in ("C1", "C3"):
    w(P(ref, "2"), (P(ref, "2")[0], 106.68))
    gnd(P(ref, "2")[0], 106.68)
w(P("R50", "2"), P("LED1", "2"))
w(P("LED1", "1"), (88.90, 119.38))
gnd(88.90, 119.38)

# --- devkit power pins ---------------------------------------------------
w(P("A1", "1"), (210.82, 113.03), (210.82, 110.49))
p33(210.82, 110.49)
FLAGS.append((212.09, 113.03))
jd(212.09, 113.03)
w(P("A1", "21"), (210.82, 163.83), (210.82, 161.29))
p5(210.82, 161.29)
w(P("A1", "22"), (213.36, 166.37), (213.36, 168.91))
gnd(213.36, 168.91)
w(P("A1", "23"), (266.70, 113.03), (266.70, 115.57))
gnd(266.70, 115.57)
w(P("A1", "43"), (262.89, 163.83), (262.89, 166.37))
w(P("A1", "44"), (262.89, 166.37))
jd(262.89, 166.37)
w((262.89, 166.37), (262.89, 168.91))
gnd(262.89, 168.91)

# --- input conditioning (far right, past J9) -----------------------------
# devkit pins LIMX 26 / LIMY 27 / LIMZ 28 / PROBE 29 run east ABOVE J9,
# then fan down lanes ordered rightmost-first => zero crossings.
lanes = {0: 320.04, 1: 317.50, 2: 314.96, 3: 312.42}
for k, pin in enumerate(["26", "27", "28", "29"]):
    dk = P("A1", pin)
    ch_y = 152.40 + 20.32 * k
    w(dk, (lanes[k], dk[1]), (lanes[k], ch_y), P(f"R3{k}", "2"))
    node = (337.82, ch_y)
    w(P(f"R3{k}", "1"), node, (347.98, ch_y))
    jd(*node)
    # pullup up to +3V3 (R2 pin2 is at the node, pin1 on top)
    r2_top = P(f"R2{k}", "1")
    w(r2_top, (337.82, ch_y - 12.70))
    p33(337.82, ch_y - 12.70)
    # filter cap below the wire at x 342.90
    c_top = P(f"C2{k}", "1")   # (342.90, ch_y)
    jd(*c_top)
    w(P(f"C2{k}", "2"), (342.90, ch_y + 10.16))
    gnd(342.90, ch_y + 10.16)

for k, ref in enumerate(["J5", "J6", "J10", "J11"]):
    gpin = P(ref, "2")
    w(gpin, (345.44, gpin[1]), (345.44, gpin[1] + 2.54))
    gnd(345.44, gpin[1] + 2.54)

# --- SD pads: straight drops from the devkit's SD pin cluster ------------
w(P("A1", "30"), (269.24, 130.81), (269.24, 149.86), P("J9", "1"))   # SCK
w(P("A1", "31"), (266.70, 133.35), (266.70, 152.40), P("J9", "2"))   # MISO
w(P("A1", "32"), (264.16, 135.89), (264.16, 154.94), P("J9", "3"))   # MOSI
w(P("A1", "40"), (274.32, 156.21), (274.32, 157.48), P("J9", "4"))   # CS
w(P("J9", "5"), (275.59, 160.02), (275.59, 158.75))                  # 5V
p5(275.59, 158.75)
w(P("J9", "6"), (273.05, 162.56), (273.05, 165.10))                  # GND
gnd(273.05, 165.10)

# ------------------------------------------------------- self-check
attach_pts = set()
for pl in W:
    attach_pts.update(pl)
    # points along horizontal/vertical segments also connect
for _, x, y in POWER:
    attach_pts.add((x, y))

def on_any_wire(pt):
    if pt in attach_pts:
        return True
    for pl in W:
        for a, b in zip(pl, pl[1:]):
            if a[0] == b[0] == pt[0] and min(a[1], b[1]) <= pt[1] <= max(a[1], b[1]):
                return True
            if a[1] == b[1] == pt[1] and min(a[0], b[0]) <= pt[0] <= max(a[0], b[0]):
                return True
    return False

missing = []
for ref, c in COMPONENTS.items():
    if c["sym"] is None:
        continue
    for num in c["pins"]:
        if not on_any_wire(pin_pos(ref, num)):
            missing.append((ref, num, pin_pos(ref, num)))
if missing:
    print("SELF-CHECK FAILED, unattached pins:")
    for m in missing:
        print("  ", m)
    sys.exit(1)

# ---------------------------------------------------- label positioning
# Family defaults keep ref/value snug beside each symbol; overrides where
# geometry demands. Entries: (x, y) or (x, y, justify).
LEFT_PADS = {"J1", "J2", "J3", "J4", "J7", "J8"}
RIGHT_PADS = {"J5", "J6", "J10", "J11", "J9"}
REF_POS, VAL_POS, VAL_HIDE = {}, {}, set()
REF_POS["A1"] = (238.76, 106.68); VAL_POS["A1"] = (238.76, 172.72)
REF_POS["U1"] = (163.83, 110.49); VAL_POS["U1"] = (172.72, 152.40)
for k in range(8):  # pulldowns: staggered refs, values replaced by one note
    x = 187.96 + 3.81 * k
    REF_POS[f"R1{k}"] = (x, 146.05 if k % 2 == 0 else 143.51)
    VAL_HIDE.add(f"R1{k}")
TEXTS = [("R10-R17: 100k", 201.93, 167.64)]

def label_pos(ref, sym, X, Y):
    """-> ((rx, ry, rjust), (vx, vy, vjust)) placed tight to the body."""
    def norm(p, dflt):
        p = p or dflt
        return p if len(p) == 3 else (p[0], p[1], "c")
    rp, vp = REF_POS.get(ref), VAL_POS.get(ref)
    if ref in LEFT_PADS:      # body left of pins: labels just left of body
        dr, dv = (X - 5.08, Y - 1.27, "r"), (X - 5.08, Y + 1.27, "r")
    elif ref in RIGHT_PADS:   # labels just right of body
        dr, dv = (X + 5.08, Y - 1.27, "l"), (X + 5.08, Y + 1.27, "l")
    elif sym in ("R", "C", "C_Pol", "D_V"):     # vertical two-pin: right side
        dr, dv = (X + 1.905, Y - 1.27, "l"), (X + 1.905, Y + 1.27, "l")
    elif sym == "LED_V":                        # arrows on right: go left
        dr, dv = (X - 2.54, Y - 1.27, "r"), (X - 2.54, Y + 1.27, "r")
    elif sym in ("R_H", "Fuse_H", "D"):         # horizontal: above/below
        dr, dv = (X, Y - 2.54, "c"), (X, Y + 2.54, "c")
    elif sym in ("Q_NMOS", "Q_NPN"):
        dr, dv = (X + 5.08, Y - 1.27, "l"), (X + 5.08, Y + 1.27, "l")
    else:
        dr, dv = (X, Y - 6.35, "c"), (X, Y + 6.35, "c")
    return norm(rp, dr), norm(vp, dv)

# ------------------------------------------------------------- emission
NO_PINNUM = {"R", "R_H", "C", "C_Pol", "D", "D_V", "LED_V", "Fuse_H",
             "Polyfuse", "Q_NMOS", "Q_NPN"}

def emit_lib_symbols(prefix=True):
    out = []
    for name, s in SYMS.items():
        full = f"bob:{name}" if prefix else name
        pw = ' (power)' if s.get("power") else ''
        out.append(f'    (symbol "{full}"{pw} '
                   + ('(pin_numbers hide) ' if name in NO_PINNUM else '')
                   + '(exclude_from_sim no) (in_bom yes) (on_board yes)')
        hide_v = ' hide' if s.get("power") else ''
        out.append(f'      (property "Reference" '
                   f'"{"#PWR" if s.get("power") else "X"}" (at 0 3.81 0) '
                   f'(effects (font (size 1.27 1.27)) hide))')
        out.append(f'      (property "Value" "{name}" (at 0 -3.81 0) '
                   f'(effects (font (size 1.27 1.27)){hide_v}))')
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
            ptype = ("power_out" if s.get("power_out")
                     else "power_in" if s.get("power") else "passive")
            hide = ' hide' if s.get("power") else ''
            out.append(f'        (pin {ptype} line (at {x} {y} {ang}) '
                       f'(length {ln}){hide} '
                       f'(name "{nm}" (effects (font (size 1.27 1.27)))) '
                       f'(number "{nu}" (effects (font (size 1.27 1.27)))))')
        out.append("      )")
        out.append("    )")
    return "\n".join(out)

SIDE = {}
body = []

for ref, c in COMPONENTS.items():
    if c["sym"] is None:
        continue
    X, Y = PL[ref]
    sym = SYMS[c["sym"]]
    iu = U()
    SIDE[ref] = {"uuid": iu,
                 "nc": {nu: nm for nu, nm, *_ in sym["pins"]
                        if nu not in c["pins"]}}
    body.append(f'  (symbol (lib_id "bob:{c["sym"]}") (at {X} {Y} 0) '
                '(unit 1) (exclude_from_sim no) (in_bom yes) '
                f'(on_board yes) (dnp no) (uuid "{iu}")')
    (rx, ry, rj), (vx, vy, vj) = label_pos(ref, c["sym"], X, Y)
    JUST = {"l": " (justify left)", "r": " (justify right)", "c": ""}
    vhide = ' hide' if ref in VAL_HIDE else ''
    body.append(f'    (property "Reference" "{ref}" (at {rx} {ry} 0) '
                f'(effects (font (size 1.27 1.27)){JUST[rj]}))')
    body.append(f'    (property "Value" "{c["value"]}" (at {vx} {vy} 0) '
                f'(effects (font (size 1.27 1.27)){JUST[vj]}{vhide}))')
    body.append(f'    (property "Footprint" "{c["fp"]}" (at {X} {Y} 0) '
                '(effects (font (size 1.27 1.27)) hide))')
    body.append(f'    (property "Datasheet" "" (at {X} {Y} 0) '
                '(effects (font (size 1.27 1.27)) hide))')
    for nu, *_ in sym["pins"]:
        body.append(f'    (pin "{nu}" (uuid "{U()}"))')
    body.append(f'    (instances (project "fluidnc-bob" '
                f'(path "/{ROOT_UUID}" (reference "{ref}") (unit 1))))')
    body.append("  )")
    # no-connects
    for nu, nm, px, py, ang, ln in sym["pins"]:
        if nu not in c["pins"]:
            body.append(f'  (no_connect (at {round(X + px, 2)} '
                        f'{round(Y - py, 2)}) (uuid "{U()}"))')

pwr_n = 0
for name, x, y in POWER:
    pwr_n += 1
    lib = "bob:VCC_BUF" if name == "VCC_BUF" else f"power:{name}"
    vy = y + 5.08 if name == "GND" else y - 5.08  # value beyond the graphic
    body.append(f'  (symbol (lib_id "{lib}") (at {x} {y} 0) (unit 1) '
                '(exclude_from_sim no) (in_bom no) (on_board yes) (dnp no) '
                f'(uuid "{U()}")')
    body.append(f'    (property "Reference" "#PWR{pwr_n:03d}" (at {x} {y} 0) '
                '(effects (font (size 1.27 1.27)) hide))')
    body.append(f'    (property "Value" "{name}" (at {x} {vy} 0) '
                '(effects (font (size 1.02 1.02))))')
    body.append(f'    (property "Footprint" "" (at {x} {y} 0) '
                '(effects (font (size 1.27 1.27)) hide))')
    body.append(f'    (property "Datasheet" "" (at {x} {y} 0) '
                '(effects (font (size 1.27 1.27)) hide))')
    body.append(f'    (pin "1" (uuid "{U()}"))')
    body.append(f'    (instances (project "fluidnc-bob" '
                f'(path "/{ROOT_UUID}" (reference "#PWR{pwr_n:03d}") (unit 1))))')
    body.append("  )")
for x, y in FLAGS:
    pwr_n += 1
    body.append(f'  (symbol (lib_id "power:PWR_FLAG") (at {x} {y} 0) (unit 1) '
                '(exclude_from_sim no) (in_bom no) (on_board yes) (dnp no) '
                f'(uuid "{U()}")')
    body.append(f'    (property "Reference" "#FLG{pwr_n:03d}" (at {x} {y} 0) '
                '(effects (font (size 1.27 1.27)) hide))')
    body.append(f'    (property "Value" "PWR_FLAG" (at {x} {y - 5.08} 0) '
                '(effects (font (size 1.02 1.02)) hide))')
    body.append(f'    (property "Footprint" "" (at {x} {y} 0) '
                '(effects (font (size 1.27 1.27)) hide))')
    body.append(f'    (property "Datasheet" "" (at {x} {y} 0) '
                '(effects (font (size 1.27 1.27)) hide))')
    body.append(f'    (pin "1" (uuid "{U()}"))')
    body.append(f'    (instances (project "fluidnc-bob" '
                f'(path "/{ROOT_UUID}" (reference "#FLG{pwr_n:03d}") (unit 1))))')
    body.append("  )")

for pl in W:
    for a, b in zip(pl, pl[1:]):
        if a == b:
            continue
        body.append(f'  (wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) '
                    f'(stroke (width 0) (type default)) (uuid "{U()}"))')
for x, y in JUNC:
    body.append(f'  (junction (at {x} {y}) (diameter 0) (color 0 0 0 0) '
                f'(uuid "{U()}"))')
for txt, x, y in TEXTS:
    body.append(f'  (text "{txt}" (exclude_from_sim no) (at {x} {y} 0) '
                f'(effects (font (size 1.27 1.27))) (uuid "{U()}"))')
for name, x, y, ang in LABELS:
    just = "right" if ang == 180 else "left"
    body.append(
        f'  (global_label "{name}" (shape passive) (at {x} {y} {ang}) '
        f'(effects (font (size 1.27 1.27)) (justify {just})) (uuid "{U()}") '
        f'(property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {x} {y} 0) '
        f'(effects (font (size 1.27 1.27)) hide)))')

doc = f'''(kicad_sch (version 20231120) (generator "gen_sch.py")
  (uuid "{ROOT_UUID}")
  (paper "A3")
  (title_block (title "fluidnc-bob rev C — ESP32-S3 FluidNC breakout")
    (date "2026-07-18") (rev "C") (company "SharkCNC"))
  (lib_symbols
{emit_lib_symbols()}
{chr(10).join('    ' + OFFICIAL_POWER[n].replace(chr(10), chr(10) + '    ')
              for n in OFFICIAL_POWER)}
  )
{chr(10).join(body)}
  (sheet_instances (path "/" (page "1")))
)
'''

here = os.path.join(os.path.dirname(__file__), "..")
with open(os.path.join(here, "fluidnc-bob.kicad_sch"), "w") as f:
    f.write(doc)
with open(os.path.join(here, "bob.kicad_sym"), "w") as f:
    f.write('(kicad_symbol_lib (version 20231120) (generator "gen_sch.py")\n'
            + emit_lib_symbols(prefix=False) + '\n)\n')
with open(os.path.join(here, "sym-lib-table"), "w") as f:
    f.write('(sym_lib_table (version 7)\n  (lib (name "bob")(type "KiCad")'
            '(uri "${KIPRJMOD}/bob.kicad_sym")(options "")(descr ""))\n)\n')
with open(os.path.join(here, "fp-lib-table"), "w") as f:
    f.write('(fp_lib_table (version 7)\n  (lib (name "fluidnc-bob")(type "KiCad")'
            '(uri "${KIPRJMOD}/fluidnc-bob.pretty")(options "")(descr ""))\n)\n')
with open(os.path.join(os.path.dirname(__file__), "sch_uuids.json"), "w") as f:
    json.dump({"root": ROOT_UUID, "symbols": SIDE}, f, indent=1)
print("wrote fluidnc-bob.kicad_sch (self-check passed)")
