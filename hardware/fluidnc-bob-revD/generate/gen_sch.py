"""Generate fluidnc-bob-revD.kicad_sch from design.py — drawn-wire edition.

Rev C style: real wires, junction dots, and official power symbols
(GND/+5V/+3V3/+36V from KiCad's power library). Global labels are used
only for genuine long-haul nets: GPIO nets at the devkit (SD, EN, E-stop,
limits, probe), buffer outputs to the driver terminals (XSTEP...),
SPIN5, EN_SINK, and the input-conditioning channel returns.

A self-check verifies every connected design.py pin lands on a wire or a
power-symbol attach point; `verify_netlist.py` diffs KiCad's exported
netlist against design.py by connectivity partition + power-net names.

Each non-passive component carries hidden "Digikey" and "Datasheet"
URL properties from design.py's info fields.
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
# terminal blocks: TB_0n pins RIGHT; TB_02L pins LEFT
for n in (2, 5):
    tb_body = [rect(-3.81, -(n - 1) * 2.54 - 2.54, 3.81, 2.54)] + \
              [circle(0, -2.54 * i, 0.889) for i in range(n)]
    SYMS[f"TB_0{n}"] = dict(
        shapes=tb_body,
        pins=[(str(i + 1), "~", 7.62, -2.54 * i, 180, 3.81)
              for i in range(n)])
SYMS["TB_02L"] = dict(
    shapes=SYMS["TB_02"]["shapes"],
    pins=[(str(i + 1), "~", -7.62, -2.54 * i, 0, 3.81) for i in range(2)])
SYMS["TB_03L"] = dict(
    shapes=[rect(-3.81, -7.62, 3.81, 2.54)] +
           [circle(0, -2.54 * i, 0.889) for i in range(3)],
    pins=[(str(i + 1), "~", -7.62, -2.54 * i, 0, 3.81) for i in range(3)])
# G5LE-14 SPDT relay: coil 2(+)/5(-) on the left, COM 1 / NO 3 / NC 4 right
SYMS["RELAY_G5LE"] = dict(
    shapes=[rect(-7.62, -7.62, 7.62, 7.62),
            rect(-6.35, -3.81, -3.175, 3.81),           # coil box
            line([(-3.175, 0), (0.635, 0)]),            # actuator dash
            line([(3.81, -5.08), (2.54, 2.54)]),        # armature
            line([(2.54, 5.08), (2.54, 3.81)]),         # NO stub
            line([(5.08, 5.08), (5.08, 2.54), (4.445, 3.175), (5.08, 3.81)])],
    pins=[("2", "COIL+", -12.7, 2.54, 0, 5.08),
          ("5", "COIL-", -12.7, -2.54, 0, 5.08),
          ("1", "COM", 12.7, -5.08, 180, 5.08),
          ("3", "NO", 12.7, 5.08, 180, 5.08),
          ("4", "NC", 12.7, 0, 180, 5.08)])
# Molex KK-254 2-pos: pins face LEFT
SYMS["KK_02"] = dict(
    shapes=[rect(-3.81, -5.08, 3.81, 2.54)] +
           [rect(-1.27, -2.54 * i - 0.635, 0, -2.54 * i + 0.635)
            for i in range(2)],
    pins=[(str(i + 1), "~", -7.62, -2.54 * i, 0, 3.81) for i in range(2)])
# 2x6 pin header (SD module, rev C pad map): odd pins left, even right
SYMS["CONN_2x6"] = dict(
    shapes=[rect(-3.81, -15.24, 3.81, 2.54)] +
           [circle(-1.905, -2.54 * r, 0.635) for r in range(6)] +
           [circle(1.905, -2.54 * r, 0.635) for r in range(6)],
    pins=[(str(2 * r + 1), "~", -7.62, -2.54 * r, 0, 3.81)
          for r in range(6)] +
         [(str(2 * r + 2), "~", 7.62, -2.54 * r, 180, 3.81)
          for r in range(6)])
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
# single 44-pin devkit symbol (rev C geometry): left pads 1-22, right 23-44
SYMS["ESP32_S3_DevKit_44"] = dict(
    shapes=[rect(-17.78, -30.48, 17.78, 30.48)],
    pins=[(str(p), nm, -22.86, 26.67 - 2.54 * (p - 1), 0, 5.08)
          for p, nm, _ in DEVKIT_LEFT] +
         [(str(i + 23), nm, 22.86, 26.67 - 2.54 * i, 180, 5.08)
          for i, (_, nm, _) in enumerate(DEVKIT_RIGHT)])

def official_power_symbol(name):
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
    return txt[i:j + 1].replace(key, f'(symbol "power:{name}"', 1)

OFFICIAL_POWER = {n: official_power_symbol(n)
                  for n in ("GND", "+5V", "+3V3", "PWR_FLAG")}

# ------------------------------------------------------------- placement
PL = {
    # POWER (top band)
    "J1": (40.64, 45.72), "F1": (63.5, 43.18), "D1": (80.01, 48.26),
    "C3": (176.53, 46.99), "C4": (186.69, 46.99),
    "R50": (196.85, 48.26), "LED1": (196.85, 58.42),
    # DEVKIT + BUFFER (rev C geometry)
    "A1": (238.76, 139.7), "U1": (172.72, 130.81), "C5": (163.83, 109.22),
    **{f"R1{k}": (187.96 + 2.54 * k, 146.05) for k in range(8)},
    "R60": (147.32, 138.43), "R61": (133.35, 135.89),
    "R62": (147.32, 133.35), "R63": (133.35, 130.81),
    "R64": (147.32, 128.27), "R65": (133.35, 125.73),
    "R66": (147.32, 120.65),
    # DRIVER TERMINALS (left edge)
    "J2": (41.91, 116.84), "J3": (41.91, 149.86), "J4": (41.91, 182.88),
    # EN BLOCK
    "R42": (100.33, 220.98), "R43": (109.22, 226.06), "Q2": (118.11, 220.98),
    "R51": (120.65, 198.12), "LED2": (120.65, 208.28),
    # RELAY
    "R40": (180.34, 228.6), "R41": (187.96, 233.68),
    "D2": (190.5, 218.44), "Q1": (198.12, 228.6),
    "RLY1": (215.9, 220.98), "J10": (242.57, 223.52),
    # INPUT CHANNELS (right band): ch_y = 167.64 + 17.78k
    **{f"R3{k}": (317.5, 167.64 + 17.78 * k) for k in range(5)},
    **{f"R2{k}": (330.2, 162.56 + 17.78 * k) for k in range(5)},
    **{f"C2{k}": (337.82, 171.45 + 17.78 * k) for k in range(5)},
    "J5": (368.3, 167.64), "J6": (368.3, 185.42), "J7": (368.3, 203.2),
    "J8": (368.3, 220.98), "J9": (368.3, 238.76),
    # SD + AUX (bottom-left)
    "J13": (63.5, 231.14), "J11": (63.5, 256.54),
    # mounting holes
    **{f"H{i}": (386.08 + 7.62 * ((i - 1) % 2), 38.1 + 7.62 * ((i - 1) // 2))
       for i in range(1, 5)},
}

TEXTS = [
    ("POWER: EXT 5V IN -> FUSE(2A) -> TVS(SMBJ5.0A) -> +5V RAIL", 38.1, 30.48),
    ("DRIVER TERMINALS: STEP DIR GND +5V(EN+) EN-", 33.02, 105.41),
    ("74AHCT541 @ 5V", 154.94, 100.33),
    ("R10-R17: 100k boot pulldowns", 185.42, 168.91),
    ("ESP32-S3 DEVKIT (pin 1 = 3V3, antenna end)", 226.06, 102.87),
    ("INPUTS: 10k pullup / 1k series / 100n", 302.26, 156.21),
    ("EN SINK: GPIO8 high = disabled", 92.71, 243.84),
    ("SPINDLE RELAY", 158.75, 248.92),
    ("SD (3V3!) + AUX", 43.18, 219.71),
    ("R60-R66: 100R", 127.0, 144.78),
]

def pin_pos(ref, num):
    sym = SYMS[COMPONENTS[ref]["sym"]]
    for nu, nm, px, py, ang, ln in sym["pins"]:
        if nu == num:
            X, Y = PL[ref]
            return (round(X + px, 2), round(Y - py, 2))
    raise KeyError((ref, num))

def P(ref, num):
    return pin_pos(ref, num)

# ------------------------------------------------------------------ wires
W, JUNC, POWER, FLAGS, LABELS = [], [], [], [], []

def w(*pts):
    W.append([(round(x, 2), round(y, 2)) for x, y in pts])
def gnd(x, y):  POWER.append(("GND", round(x, 2), round(y, 2)))
def p5(x, y):   POWER.append(("+5V", round(x, 2), round(y, 2)))
def p33(x, y):  POWER.append(("+3V3", round(x, 2), round(y, 2)))
def jd(x, y):   JUNC.append((round(x, 2), round(y, 2)))
def lbl(name, x, y, ang):
    LABELS.append((name, round(x, 2), round(y, 2), ang))

# --- power entry: external 5V -> fuse -> TVS -> +5V rail -----------------
w(P("J1", "1"), (53.34, 45.72), (53.34, 43.18), P("F1", "1"))
w(P("J1", "2"), (50.8, 48.26), (50.8, 50.8))
gnd(50.8, 50.8)
FLAGS.append((50.8, 48.26)); jd(50.8, 48.26)
# fuse output IS the +5V rail; D1 (SMBJ5.0A) clamps it / crowbars reverse
w(P("F1", "2"), P("D1", "1"))
jd(73.66, 43.18);  p5(73.66, 43.18)
jd(76.2, 43.18);   FLAGS.append((76.2, 43.18))
w(P("D1", "2"), (80.01, 55.88));   gnd(80.01, 55.88)
# 5V housekeeping: C3, C4, PWR LED
for ref in ("C3", "C4"):
    x = PL[ref][0]
    w(P(ref, "1"), (x, 40.64)); p5(x, 40.64)
    w(P(ref, "2"), (x, 53.34)); gnd(x, 53.34)
w(P("R50", "1"), (196.85, 40.64)); p5(196.85, 40.64)
# R50.2 tip touches LED1.A tip directly (same point)
w(P("LED1", "1"), (196.85, 66.04)); gnd(196.85, 66.04)

# --- devkit <-> buffer input bus (pads 4..11 = A1..A8) -------------------
for k in range(8):
    dk = P("A1", str(4 + k))
    u = P("U1", str(2 + k))
    w(u, dk)
    lane_x = 187.96 + 2.54 * k
    w((lane_x, dk[1]), P(f"R1{k}", "1"))
    jd(lane_x, dk[1])
# pulldown GND rail
for k in range(8):
    lane_x = 187.96 + 2.54 * k
    w(P(f"R1{k}", "2"), (lane_x, 152.4))
w((187.96, 152.4), (205.74, 152.4))
for k in range(1, 8):
    jd(187.96 + 2.54 * k, 152.4)
w((196.85, 152.4), (196.85, 154.94))
jd(196.85, 152.4)
gnd(196.85, 154.94)

# --- U1 housekeeping -----------------------------------------------------
g10, oe1, oe2 = P("U1", "10"), P("U1", "1"), P("U1", "19")
w(g10, (g10[0], 156.21), (g10[0], 158.75))
w(oe1, (oe1[0], 156.21), (g10[0], 156.21))
w(oe2, (oe2[0], 156.21), (g10[0], 156.21))
jd(g10[0], 156.21)
gnd(g10[0], 158.75)
vcc = P("U1", "20")
w(vcc, (vcc[0], 102.87))
p5(vcc[0], 102.87)
w(P("C5", "1"), (172.72, 105.41))
jd(172.72, 105.41)
w(P("C5", "2"), (163.83, 115.57))
gnd(163.83, 115.57)

# --- buffer outputs -> series R -> labels (to driver terminals) ----------
# outputs (top->bottom): 18=AUX5 17=SPIN5 16=ZDIR5 ... 11=XSTEP5
for k, (pin, lab) in enumerate([("11", "XSTEP"), ("12", "XDIR"),
                                ("13", "YSTEP"), ("14", "YDIR"),
                                ("15", "ZSTEP"), ("16", "ZDIR")]):
    w(P(f"R6{k}", "2"), P("U1", pin))
    r1 = P(f"R6{k}", "1")
    w(r1, (r1[0] - 2.54, r1[1]))
    lbl(lab, r1[0] - 2.54, r1[1], 180)
spin = P("U1", "17")                     # SPIN5, no series R
w(spin, (spin[0] - 2.54, spin[1]))
lbl("SPIN5", spin[0] - 2.54, spin[1], 180)
w(P("R66", "2"), P("U1", "18"))          # AUX5 -> series R
a1 = P("R66", "1")
w(a1, (a1[0] - 2.54, a1[1]))
lbl("AUX", a1[0] - 2.54, a1[1], 180)

# --- devkit left column: EN, E-stop, SD (labels), power pins -------------
for pad, net in [("12", "EN_G"), ("15", "ESTOP_G"), ("16", "SD_CS"),
                 ("17", "SD_MOSI"), ("18", "SD_SCK"), ("19", "SD_MISO")]:
    t = P("A1", pad)
    w(t, (t[0] - 2.54, t[1]))
    lbl(net, t[0] - 2.54, t[1], 180)
w(P("A1", "21"), (210.82, 163.83), (210.82, 161.29))
p5(210.82, 161.29)
w(P("A1", "22"), (213.36, 166.37), (213.36, 168.91))
gnd(213.36, 168.91)
t = P("A1", "1")
w(t, (210.82, 113.03), (210.82, 110.49))
p33(210.82, 110.49)
FLAGS.append((212.09, 113.03)); jd(212.09, 113.03)

# --- devkit right column: limits/probe labels + grounds ------------------
for pad, net in [("26", "LIMZ_G"), ("27", "LIMY_G"),
                 ("28", "LIMX_G"), ("29", "PROBE_G")]:
    t = P("A1", pad)
    w(t, (t[0] + 2.54, t[1]))
    lbl(net, t[0] + 2.54, t[1], 0)
w(P("A1", "23"), (266.7, 113.03), (266.7, 115.57))
gnd(266.7, 115.57)
w(P("A1", "43"), (262.89, 163.83), (262.89, 166.37))
w(P("A1", "44"), (262.89, 166.37))
jd(262.89, 166.37)
w((262.89, 166.37), (262.89, 168.91))
gnd(262.89, 168.91)

# --- driver terminals ----------------------------------------------------
for jref, ax in [("J2", "X"), ("J3", "Y"), ("J4", "Z")]:
    t1, t2 = P(jref, "1"), P(jref, "2")
    w(t1, (t1[0] + 2.54, t1[1])); lbl(f"{ax}STEP", t1[0] + 2.54, t1[1], 0)
    w(t2, (t2[0] + 2.54, t2[1])); lbl(f"{ax}DIR", t2[0] + 2.54, t2[1], 0)
    t3 = P(jref, "3")            # GND: east then down, clear of the pins
    w(t3, (66.04, t3[1]), (66.04, t3[1] + 11.43))
    gnd(66.04, t3[1] + 11.43)
    t4 = P(jref, "4")            # +5V (EN+): east then up over the block
    w(t4, (53.34, t4[1]), (53.34, t4[1] - 10.16))
    p5(53.34, t4[1] - 10.16)
    t5 = P(jref, "5")            # EN- sink bus
    w(t5, (t5[0] + 2.54, t5[1])); lbl("EN_SINK", t5[0] + 2.54, t5[1], 0)

# --- EN sink block -------------------------------------------------------
r42a = P("R42", "1")
w(r42a, (r42a[0] - 2.54, r42a[1]))
lbl("EN_G", r42a[0] - 2.54, r42a[1], 180)
w(P("R42", "2"), P("Q2", "1"))
jd(109.22, 220.98)               # R43 top sits on the base wire
w(P("R43", "2"), (109.22, 233.68))
gnd(109.22, 233.68)
w(P("Q2", "2"), (120.65, 228.6))
gnd(120.65, 228.6)
w(P("Q2", "3"), P("LED2", "1"))  # collector up to LED2 cathode
jd(120.65, 214.63)
w((120.65, 214.63), (127.0, 214.63))
lbl("EN_SINK", 127.0, 214.63, 0)
# LED2.A tip touches R51.2 tip directly
w(P("R51", "1"), (120.65, 190.5))
p5(120.65, 190.5)

# --- spindle relay -------------------------------------------------------
r40a = P("R40", "1")
w(r40a, (r40a[0] - 2.54, r40a[1]))
lbl("SPIN5", r40a[0] - 2.54, r40a[1], 180)
w(P("R40", "2"), P("Q1", "1"))
jd(187.96, 228.6)                # R41 top sits on the base wire
w(P("R41", "2"), (187.96, 241.3))
gnd(187.96, 241.3)
w(P("Q1", "2"), (200.66, 236.22))
gnd(200.66, 236.22)
# RLY_N: flyback anode -> Q1 collector -> coil- (one rail)
w(P("D2", "2"), P("Q1", "3"), P("RLY1", "5"))
jd(*P("Q1", "3"))
w(P("D2", "1"), (190.5, 210.82))
p5(190.5, 210.82)
w(P("RLY1", "2"), (203.2, 215.9), (203.2, 213.36))   # coil+ -> +5V
p5(203.2, 213.36)
# contacts -> spindle switch terminal; distinct lanes, crossings only
w(P("RLY1", "1"), (229.87, 226.06), (229.87, 223.52), P("J10", "1"))
w(P("RLY1", "3"), (233.68, 215.9), (233.68, 226.06), P("J10", "2"))
w(P("RLY1", "4"), (232.41, 220.98), (232.41, 228.6), P("J10", "3"))

# --- input conditioning channels ----------------------------------------
CHN = ["LIMX", "LIMY", "LIMZ", "PROBE", "ESTOP"]
for k, (name, jref) in enumerate(zip(CHN, ["J5", "J6", "J7", "J8", "J9"])):
    ch_y = 167.64 + 17.78 * k
    g = P(f"R3{k}", "1")
    w(g, (g[0] - 2.54, g[1]))
    lbl(f"{name}_G", g[0] - 2.54, g[1], 180)
    # node: R3k.2 east to the connector pin 1
    w(P(f"R3{k}", "2"), P(jref, "1"))
    jd(330.2, ch_y)              # R2k pullup tap
    jd(337.82, ch_y)             # C2k tap
    w(P(f"R2{k}", "1"), (330.2, ch_y - 12.7))
    p33(330.2, ch_y - 12.7)
    w(P(f"C2{k}", "2"), (337.82, ch_y + 10.16))
    gnd(337.82, ch_y + 10.16)
    t2 = P(jref, "2")
    w(t2, (355.6, t2[1]), (355.6, t2[1] + 2.54))
    gnd(355.6, t2[1] + 2.54)

# --- SD + AUX ------------------------------------------------------------
w(P("J13", "5"), (53.34, 236.22), (53.34, 233.68))
p33(53.34, 233.68)
for pad, net in [("7", "SD_MISO"), ("9", "SD_SCK")]:
    t = P("J13", pad)
    w(t, (48.26, t[1]))
    lbl(net, 48.26, t[1], 180)
for pad, net in [("8", "SD_MOSI"), ("10", "SD_CS")]:
    t = P("J13", pad)
    w(t, (73.66, t[1]))
    lbl(net, 73.66, t[1], 0)
w(P("J13", "11"), (53.34, 243.84), (53.34, 246.38))
gnd(53.34, 246.38)
w(P("J13", "12"), (71.12, 243.84), (71.12, 246.38))
gnd(71.12, 246.38)
t = P("J11", "1")
w(t, (48.26, t[1]))
lbl("AUX", 48.26, t[1], 180)
w(P("J11", "2"), (53.34, 259.08), (53.34, 261.62))
gnd(53.34, 261.62)

# ------------------------------------------------------- self-check
attach_pts = set()
for pl in W:
    attach_pts.update(pl)
for _, x, y in POWER:
    attach_pts.add((x, y))
attach_pts.update(JUNC)
# two pin tips at the same point connect directly (no wire needed)
from collections import Counter
tip_count = Counter()
for ref, c in COMPONENTS.items():
    if c["sym"] == "HOLE":
        continue
    for num in c["pins"]:
        tip_count[pin_pos(ref, num)] += 1
attach_pts.update(pt for pt, n in tip_count.items() if n > 1)

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
    if c["sym"] == "HOLE":
        continue
    for num in c["pins"]:
        if not on_any_wire(pin_pos(ref, num)):
            missing.append((ref, num, pin_pos(ref, num)))
if missing:
    print("SELF-CHECK FAILED, unattached pins:")
    for m in missing:
        print("  ", m)
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

# component label placement
LEFT_BODIES = {"J2", "J3", "J4", "J1"}      # pins east: refs west
RIGHT_BODIES = {"J5", "J6", "J7", "J8", "J9", "J13", "J11", "J10"}

def label_pos(ref, sym, X, Y):
    if ref == "J13":
        return (X, Y - 5.08, "c"), (X, Y + 20.32, "c")
    if ref == "A1":
        return (X, Y - 33.02, "c"), (X, Y + 33.02, "c")
    if ref == "U1":
        return (X - 5.08, Y - 20.32, "r"), (X, Y + 25.4, "c")
    if ref == "U2":
        return (X, Y - 8.89, "c"), (X, Y + 10.16, "c")
    if sym in ("R", "C", "C_Pol", "C_1206", "D_V", "LED_V"):
        return (X + 1.91, Y - 1.27, "l"), (X + 1.91, Y + 1.27, "l")
    if sym in ("R_H", "Fuse_H"):
        return (X, Y - 2.54, "c"), (X, Y + 2.54, "c")
    if sym == "Q_NPN":
        return (X + 5.08, Y - 1.27, "l"), (X + 5.08, Y + 1.27, "l")
    if ref in LEFT_BODIES:
        return (X - 5.08, Y - 1.27, "r"), (X - 5.08, Y + 1.27, "r")
    if ref in RIGHT_BODIES:
        return (X + 5.08, Y - 1.27, "l"), (X + 5.08, Y + 1.27, "l")
    return (X, Y - 6.35, "c"), (X, Y + 6.35, "c")

VAL_HIDE = ({f"R1{k}" for k in range(8)} | {f"R6{k}" for k in range(7)})
REF_HIDE = {f"R1{k}" for k in range(8)}
JUST = {"l": " (justify left)", "r": " (justify right)", "c": ""}

body = []
for ref, c in COMPONENTS.items():
    X, Y = PL[ref]
    sym = SYMS[c["sym"]]
    iu = U()
    body.append(f'  (symbol (lib_id "bobD:{c["sym"]}") (at {X} {Y} 0) '
                '(unit 1) (exclude_from_sim no) (in_bom yes) '
                f'(on_board yes) (dnp no) (uuid "{iu}")')
    (rx, ry, rj), (vx, vy, vj) = label_pos(ref, c["sym"], X, Y)
    vhide = ' hide' if ref in VAL_HIDE else ''
    rhide = ' hide' if ref in REF_HIDE else ''
    body.append(f'    (property "Reference" "{ref}" (at {rx} {ry} 0) '
                f'(effects (font (size 1.27 1.27)){JUST[rj]}{rhide}))')
    body.append(f'    (property "Value" "{c["value"]}" (at {vx} {vy} 0) '
                f'(effects (font (size 1.27 1.27)){JUST[vj]}{vhide}))')
    body.append(f'    (property "Footprint" "{c["fp"]}" (at {X} {Y} 0) '
                '(effects (font (size 1.27 1.27)) hide))')
    info = c.get("info") or {}
    ds = info.get("datasheet", "")
    body.append(f'    (property "Datasheet" "{ds}" (at {X} {Y} 0) '
                '(effects (font (size 1.27 1.27)) hide))')
    if info.get("digikey"):
        body.append(f'    (property "Digikey" "{info["digikey"]}" '
                    f'(at {X} {Y} 0) '
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

pwr_n = 0
for name, x, y in POWER:
    pwr_n += 1
    vy = y + 5.08 if name == "GND" else y - 5.08
    body.append(f'  (symbol (lib_id "power:{name}") (at {x} {y} 0) (unit 1) '
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
    body.append(f'    (instances (project "{PROJECT}" '
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
    body.append(f'    (instances (project "{PROJECT}" '
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
                f'(effects (font (size 1.75 1.75) bold) (justify left)) '
                f'(uuid "{U()}"))')
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
  (title_block (title "fluidnc-bob rev D — ESP32-S3 FluidNC breakout, external 5V logic supply")
    (date "2026-07-28") (rev "D") (company "SharkCNC"))
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
with open(os.path.join(here, "fluidnc-bob-revD.kicad_sch"), "w") as f:
    f.write(doc)
with open(os.path.join(here, "bobD.kicad_sym"), "w") as f:
    f.write('(kicad_symbol_lib (version 20231120) (generator "gen_sch.py")\n'
            + emit_lib_symbols(prefix=False) + '\n)\n')
with open(os.path.join(here, "sym-lib-table"), "w") as f:
    f.write('(sym_lib_table (version 7)\n  (lib (name "bobD")(type "KiCad")'
            '(uri "${KIPRJMOD}/bobD.kicad_sym")(options "")(descr ""))\n)\n')
with open(os.path.join(here, "fp-lib-table"), "w") as f:
    f.write('(fp_lib_table (version 7)\n  (lib (name "fluidnc-bob-revD")'
            '(type "KiCad")(uri "${KIPRJMOD}/fluidnc-bob-revD.pretty")'
            '(options "")(descr ""))\n)\n')
with open(os.path.join(os.path.dirname(__file__), "sch_uuids.json"), "w") as f:
    json.dump({"root": ROOT_UUID}, f, indent=1)
print(f"wrote fluidnc-bob-revD.kicad_sch ({len(COMPONENTS)} components, "
      f"{len(LABELS)} labels, {sum(len(p)-1 for p in W)} wire segments; "
      "self-check passed)")
