"""Generate fluidnc-bob.pretty/ESP32_S3_DevKit_44_Socket.kicad_mod.

Two 1x22 female-header rows, 2.54 mm pitch, ROWSP row spacing, for the
dual-USB-C 44-pin ESP32-S3-WROOM-1 devkit.

!! MEASURE YOUR DEVKIT'S ROW SPACING (pin-center to pin-center across the
!! board) BEFORE FABRICATION and set ROWSP below. 22.86 (0.9") and 25.4
!! (1.0") both exist in the wild.

Pad 1 = top-left = 3V3 end (away from USB). Pads 23-44 top-to-bottom right.
"""
import os

PITCH, NPINS = 2.54, 22
ROWSP = 22.86  # <-- VERIFY against the physical devkit before ordering!

def pad(num, x, y):
    shape = "rect" if num == 1 else "circle"
    return (f'  (pad "{num}" thru_hole {shape} (at {x:.2f} {y:.2f}) '
            '(size 1.6 1.6) (drill 1.0) (layers "*.Cu" "*.Mask"))')

def silk_line(x1, y1, x2, y2, layer="F.SilkS", w=0.15):
    return (f'  (fp_line (start {x1:.2f} {y1:.2f}) (end {x2:.2f} {y2:.2f}) '
            f'(stroke (width {w}) (type solid)) (layer "{layer}"))')

def text(s, x, y, layer="F.SilkS"):
    return (f'  (fp_text user "{s}" (at {x:.2f} {y:.2f}) (layer "{layer}") '
            '(effects (font (size 1 1) (thickness 0.15))))')

L = []
L.append('(footprint "ESP32_S3_DevKit_44_Socket" (version 20240108) '
         '(generator "gen_fp.py") (layer "F.Cu")')
L.append(f'  (descr "Socket for 44-pin dual-USB-C ESP32-S3 devkit: 2x 1x22 '
         f'female headers, {ROWSP}mm row spacing — MEASURE YOUR DEVKIT")')
L.append('  (attr through_hole)')
L.append(f'  (fp_text reference "REF**" (at {ROWSP/2:.2f} -6.5) '
         '(layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))')
L.append(f'  (fp_text value "ESP32-S3-DevKit-44" (at {ROWSP/2:.2f} 60.5) '
         '(layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))')

for i in range(NPINS):
    L.append(pad(i + 1, 0, i * PITCH))            # left row, 1..22
    L.append(pad(i + 23, ROWSP, i * PITCH))       # right row, 23..44

# body outline: ~70mm devkit, pin1 ~3mm from the antenna end, USB end long
x0, x1 = -1.25, ROWSP + 1.25
y0, y1 = -3.2, 66.0
for a, b, c, d in [(x0, y0, x1, y0), (x1, y0, x1, y1),
                   (x1, y1, x0, y1), (x0, y1, x0, y0)]:
    L.append(silk_line(a, b, c, d))
L.append(text("ANT / pin1=3V3", ROWSP / 2, -1.6))
L.append(text("USB-C x2", ROWSP / 2, 63.5))
L.append(silk_line(-2.2, -1.27, -2.2, 1.27, w=0.3))  # pin-1 marker

# courtyard: strips over the header rows only — space between is usable
# for low-profile SMD parts under the socketed devkit
for rx in (0.0, ROWSP):
    cx0, cx1 = rx - 1.6, rx + 1.6
    cy0, cy1 = -1.6, (NPINS - 1) * PITCH + 1.6
    L.append(silk_line(cx0, cy0, cx1, cy0, "F.CrtYd", 0.05))
    L.append(silk_line(cx1, cy0, cx1, cy1, "F.CrtYd", 0.05))
    L.append(silk_line(cx1, cy1, cx0, cy1, "F.CrtYd", 0.05))
    L.append(silk_line(cx0, cy1, cx0, cy0, "F.CrtYd", 0.05))
L.append(')')

out = os.path.join(os.path.dirname(__file__), "..", "fluidnc-bob.pretty")
os.makedirs(out, exist_ok=True)
path = os.path.join(out, "ESP32_S3_DevKit_44_Socket.kicad_mod")
with open(path, "w") as f:
    f.write("\n".join(L) + "\n")
print("wrote", os.path.normpath(path))

# --- solder wire-pad strips (no connectors: wires solder straight in) ----
for n in (2, 3, 6):
    P = []
    P.append(f'(footprint "WirePads_1x0{n}_P5.08mm" (version 20240108) '
             '(generator "gen_fp.py") (layer "F.Cu")')
    P.append(f'  (descr "{n} solder wire pads, 5.08mm pitch, 1.3mm drill")')
    P.append('  (attr through_hole)')
    P.append(f'  (fp_text reference "REF**" (at {(n - 1) * 2.54:.2f} -3.3) '
             '(layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))')
    P.append(f'  (fp_text value "WirePads_1x0{n}" (at {(n - 1) * 2.54:.2f} 3.3) '
             '(layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))')
    for i in range(n):
        shape = "rect" if i == 0 else "circle"
        P.append(f'  (pad "{i + 1}" thru_hole {shape} (at {i * 5.08:.2f} 0) '
                 '(size 3.0 3.0) (drill 1.3) (layers "*.Cu" "*.Mask"))')
    e = (n - 1) * 5.08 + 2.2
    for a, b, c, d in [(-2.2, -2.2, e, -2.2), (e, -2.2, e, 2.2),
                       (e, 2.2, -2.2, 2.2), (-2.2, 2.2, -2.2, -2.2)]:
        P.append(f'  (fp_line (start {a:.2f} {b:.2f}) (end {c:.2f} {d:.2f}) '
                 '(stroke (width 0.05) (type solid)) (layer "F.CrtYd"))')
    P.append(')')
    path = os.path.join(out, f"WirePads_1x0{n}_P5.08mm.kicad_mod")
    with open(path, "w") as f:
        f.write("\n".join(P) + "\n")
    print("wrote", os.path.normpath(path))
