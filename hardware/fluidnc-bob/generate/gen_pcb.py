"""Generate fluidnc-bob.kicad_pcb from design.py using pcbnew.

Placement is explicit; routing is a small two-layer A* grid router
(0.635 mm cells). GND is not routed — it's poured on both layers.
Run:  python3 gen_pcb.py            (place + route + zones + save)
"""
import os, sys, heapq, math
sys.path.insert(0, os.path.dirname(__file__))
import pcbnew
from pcbnew import VECTOR2I, FromMM
from design import COMPONENTS, nets

HERE = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(HERE, "fluidnc-bob.kicad_pcb")
FP_OFFICIAL = "/usr/share/kicad/footprints"
BOARD_W, BOARD_H = 120.65, 95.25
GRID = 0.635
TRACK_W = {"default": 0.4, "+5V": 0.8, "+3V3": 0.5, "VIN5": 0.8,
           "RLY_N": 0.5}
CLEARANCE = 0.2
VIA_D, VIA_DRILL = 0.8, 0.4

# ref -> (x_mm, y_mm, rot_deg)  (position of footprint origin = pad 1)
PLACE = {
    # devkit rotated 180: signal row (pads 1-22) faces east toward U1;
    # USB-C end lands at board top, pin 1 (3V3) at the bottom
    "A1": (63.50, 71.12, 180),
    "U1": (80.01, 40.64, 0),
    "J1": (8.89, 13.97, 270),
    "F1": (16.51, 24.13, 0),
    "D2": (16.51, 30.48, 0),
    "C1": (27.94, 11.43, 0),
    "C2": (88.90, 35.56, 0),
    "C3": (55.88, 15.24, 0),   # 5V decoupling under the devkit (0805, low)
    "D3": (82.55, 33.02, 0),   # GS1A VCC drop for U1
    "R50": (81.28, 6.35, 0),
    "LED1": (87.63, 6.35, 0),
    # pulldowns: 0805 on the BOTTOM side, each beside its U1 input pin —
    # net leg reaches the pin on B.Cu, GND leg sits in the open B pour
    "R10": (77.47, 60.96, 0), "R11": (77.47, 58.42, 0),
    "R12": (77.47, 55.88, 0), "R13": (77.47, 53.34, 0),
    "R14": (77.47, 50.80, 0), "R15": (77.47, 48.26, 0),
    "R16": (77.47, 45.72, 0), "R17": (77.47, 43.18, 0),
    "J2": (111.76, 13.97, 270), "J3": (111.76, 27.94, 270),
    "J4": (111.76, 41.91, 270), "J8": (97.79, 5.08, 0),
    "J5": (16.51, 87.63, 0), "J6": (36.83, 87.63, 0),
    "J9": (6.35, 30.48, 0),    # microSD breakout header
    # input conditioning rows, west field
    "R20": (10.16, 55.88, 0), "C20": (16.51, 55.88, 0), "R30": (24.13, 55.88, 0),
    "R21": (10.16, 63.50, 0), "C21": (16.51, 63.50, 0), "R31": (24.13, 63.50, 0),
    "R22": (10.16, 71.12, 0), "C22": (16.51, 71.12, 0), "R32": (24.13, 71.12, 0),
    "R23": (10.16, 78.74, 0), "C23": (16.51, 78.74, 0), "R33": (24.13, 78.74, 0),
    "R40": (87.63, 67.31, 0), "R41": (92.71, 67.31, 0),
    "Q1": (95.25, 73.66, 0),
    "D1": (95.25, 80.01, 0),
    "J7": (95.25, 87.63, 0),
    "H1": (5.08, 5.08, 0), "H2": (115.57, 5.08, 0),
    "H3": (5.08, 90.17, 0), "H4": (115.57, 90.17, 0),
}
B_SIDE = {"R10", "R11", "R12", "R13", "R14", "R15", "R16", "R17"}

board = pcbnew.NewBoard(OUT)
bds = board.GetDesignSettings()
bds.SetCopperLayerCount(2)
bds.m_MinResolvedSpokes = 1  # pads also reached by tracks/pour geometry

# nets -------------------------------------------------------------------
netinfo = {}
for net in nets():
    ni = pcbnew.NETINFO_ITEM(board, net)
    board.Add(ni)
    netinfo[net] = ni

# footprints -------------------------------------------------------------
def load_fp(fpid):
    lib, name = fpid.split(":")
    if lib == "fluidnc-bob":
        path = os.path.join(HERE, "fluidnc-bob.pretty")
    else:
        path = os.path.join(FP_OFFICIAL, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise RuntimeError(f"footprint not found: {fpid}")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))  # keep lib nickname for parity
    return fp

import json
with open(os.path.join(os.path.dirname(__file__), "sch_uuids.json")) as f:
    SCH = json.load(f)

pads_by_net = {}     # net -> [(x_mm, y_mm)]
all_pads = []        # (x_mm, y_mm, r_mm, netname)
for ref, c in COMPONENTS.items():
    fp = load_fp(c["fp"])
    fp.SetReference(ref)
    fp.SetValue(c["value"])
    x, y, rot = PLACE[ref]
    fp.SetPosition(VECTOR2I(FromMM(x), FromMM(y)))
    fp.SetOrientationDegrees(rot)
    if ref in SCH["symbols"]:  # link to schematic symbol for parity
        fp.SetPath(pcbnew.KIID_PATH(
            f"/{SCH['root']}/{SCH['symbols'][ref]['uuid']}"))
    else:  # mounting holes: board-only
        fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_BOARD_ONLY
                         | pcbnew.FP_EXCLUDE_FROM_BOM)
    fp.Reference().SetPosition(VECTOR2I(FromMM(x + 1.27), FromMM(y - 2.54)))
    board.Add(fp)
    if ref in B_SIDE:  # Flip needs the footprint on a board (else segfault)
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    nc_names = SCH["symbols"].get(ref, {}).get("nc", {})
    for pad in fp.Pads():
        num = pad.GetNumber()
        net = c["pins"].get(num)
        px, py = pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y)
        sz = pad.GetSize()
        r = max(pcbnew.ToMM(sz.x), pcbnew.ToMM(sz.y)) / 2.0
        lays = tuple(l for l, L in ((0, pcbnew.F_Cu), (1, pcbnew.B_Cu))
                     if pad.IsOnLayer(L))
        if net:
            pad.SetNet(netinfo[net])
            pads_by_net.setdefault(net, []).append((px, py, lays))
        elif num in nc_names:  # KiCad's schematic-parity name for NC pins
            ncnet = f"unconnected-({ref}-{nc_names[num]}-Pad{num})"
            ni = pcbnew.NETINFO_ITEM(board, ncnet)
            board.Add(ni)
            pad.SetNet(ni)
        all_pads.append((px, py, r, net or f"__nc_{ref}_{num}", lays))

# board outline ----------------------------------------------------------
def edge(x1, y1, x2, y2):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(VECTOR2I(FromMM(x1), FromMM(y1)))
    s.SetEnd(VECTOR2I(FromMM(x2), FromMM(y2)))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(FromMM(0.1))
    board.Add(s)
edge(0, 0, BOARD_W, 0); edge(BOARD_W, 0, BOARD_W, BOARD_H)
edge(BOARD_W, BOARD_H, 0, BOARD_H); edge(0, BOARD_H, 0, 0)

# ------------------------------------------------------------------ router
NX, NY = int(BOARD_W / GRID), int(BOARD_H / GRID)
MARGIN = 3  # keep tracks off the board edge (cells)

def cell(x, y):
    return (min(max(int(round(x / GRID)), 0), NX - 1),
            min(max(int(round(y / GRID)), 0), NY - 1))

# obstacle maps: for each layer, dict cell -> set(netnames) that BLOCK there
blocked = [dict(), dict()]  # F.Cu=0, B.Cu=1

def block_square(layer_list, x, y, half, net):
    """Chebyshev blocking: safe for rect/roundrect pads, conservative for round."""
    ic, jc = int(round(x / GRID)), int(round(y / GRID))
    rr = int(math.ceil(half / GRID)) + 1
    for i in range(ic - rr, ic + rr + 1):
        for j in range(jc - rr, jc + rr + 1):
            if 0 <= i < NX and 0 <= j < NY:
                if max(abs(i * GRID - x), abs(j * GRID - y)) <= half:
                    for lay in layer_list:
                        blocked[lay].setdefault((i, j), set()).add(net)

for (px, py, r, net, lays) in all_pads:  # SMD pads block only their layer
    # halo for a 0.4mm track + small slack; wider tracks add is_free(extra)
    block_square(list(lays), px, py,
                 r + CLEARANCE + TRACK_W["default"] / 2 + 0.04, net)

def seg_cells(a, b):
    (x1, y1), (x2, y2) = a, b
    n = max(abs(x2 - x1), abs(y2 - y1))
    return [(x1 + (0 if n == 0 else round(k * (x2 - x1) / n)),
             y1 + (0 if n == 0 else round(k * (y2 - y1) / n)))
            for k in range(n + 1)]

def is_free(lay, ij, net, extra=0):
    i, j = ij
    if not (MARGIN <= i < NX - MARGIN and MARGIN <= j < NY - MARGIN):
        return False
    for di in range(-extra, extra + 1):
        for dj in range(-extra, extra + 1):
            s = blocked[lay].get((i + di, j + dj))
            if s and any(n != net for n in s):
                return False
    return True

def astar(net, starts, targets, width, start_layers=(0, 1)):
    """4-dir A* from any start cell to any (cell, layer) target, 2 layers."""
    extra = 1 if width > 0.5 else 0
    tset = set(targets)  # {(cell, layer)}
    tx = sum(i for (i, _), _ in targets) / len(targets)
    ty = sum(j for (_, j), _ in targets) / len(targets)
    h = lambda i, j: (abs(i - tx) + abs(j - ty)) * 0.9
    pq, seen, prev = [], {}, {}
    for s in starts:
        for lay in start_layers:
            if is_free(lay, s, net, extra):
                pq.append((h(*s), 0.0, s, lay))
                seen[(s, lay)] = 0.0
    heapq.heapify(pq)
    end = None
    while pq:
        f, g, ij, lay = heapq.heappop(pq)
        if seen.get((ij, lay), 1e9) < g - 1e-9:
            continue
        if (ij, lay) in tset:
            end = (ij, lay)
            break
        i, j = ij
        for ni, nj, cost in ((i+1, j, 1), (i-1, j, 1), (i, j+1, 1), (i, j-1, 1)):
            nij = (ni, nj)
            if not is_free(lay, nij, net, extra):
                continue
            ng = g + cost
            if ng < seen.get((nij, lay), 1e9) - 1e-9:
                seen[(nij, lay)] = ng
                prev[(nij, lay)] = (ij, lay)
                heapq.heappush(pq, (ng + h(ni, nj), ng, nij, lay))
        olay = 1 - lay
        if is_free(olay, ij, net, 2) and is_free(lay, ij, net, 2) and \
           g + 8 < seen.get((ij, olay), 1e9) - 1e-9:
            seen[(ij, olay)] = g + 8
            prev[(ij, olay)] = (ij, lay)
            heapq.heappush(pq, (g + 8 + h(i, j), g + 8, ij, olay))
    if end is None:
        return None
    path = [end]
    while path[-1] in prev:
        path.append(prev[path[-1]])
    return list(reversed(path))

LAYERS = [pcbnew.F_Cu, pcbnew.B_Cu]

def add_track(net, a, b, lay, width):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(VECTOR2I(FromMM(a[0]), FromMM(a[1])))
    t.SetEnd(VECTOR2I(FromMM(b[0]), FromMM(b[1])))
    t.SetWidth(FromMM(width))
    t.SetLayer(LAYERS[lay])
    t.SetNet(netinfo[net])
    board.Add(t)

def add_via(net, p):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(VECTOR2I(FromMM(p[0]), FromMM(p[1])))
    v.SetDrill(FromMM(VIA_DRILL))
    v.SetWidth(FromMM(VIA_D))
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetNet(netinfo[net])
    board.Add(v)

def commit_path(net, path, width):
    """Emit tracks/vias for an A* path; mark cells as this net's copper."""
    pts = [(ij, lay) for ij, lay in path]
    # simplify runs of collinear cells
    out = [pts[0]]
    for k in range(1, len(pts) - 1):
        (a, la), (b, lb), (c, lc) = pts[k - 1], pts[k], pts[k + 1]
        if la == lb == lc and (b[0] - a[0], b[1] - a[1]) == (c[0] - b[0], c[1] - b[1]):
            continue
        out.append(pts[k])
    out.append(pts[-1])
    for k in range(len(out) - 1):
        (a, la), (b, lb) = out[k], out[k + 1]
        pa = (a[0] * GRID, a[1] * GRID)
        pb = (b[0] * GRID, b[1] * GRID)
        if la != lb:
            add_via(net, pa)
            block_square([0, 1], pa[0], pa[1],
                         VIA_D / 2 + CLEARANCE + TRACK_W["default"] / 2 + 0.04,
                         net)
        else:
            add_track(net, pa, pb, la, width)
    halo = width / 2 + CLEARANCE + TRACK_W["default"] / 2 + 0.04
    for ij, lay in pts:
        block_square([lay], ij[0] * GRID, ij[1] * GRID, halo, net)
    return set(pts)

def pad_stub(net, pad_xy, lay, width):
    """Track from exact pad centre to its nearest grid cell, on one layer."""
    c = cell(*pad_xy)
    gp = (c[0] * GRID, c[1] * GRID)
    if abs(gp[0] - pad_xy[0]) > 1e-3 or abs(gp[1] - pad_xy[1]) > 1e-3:
        add_track(net, pad_xy, gp, lay, width)

# GND is poured on both layers, not routed (all GND pads sit in open pour).
ROUTE_ORDER = ["PROBE_G", "LIMZ_G", "LIMY_G", "LIMX_G",  # corridor first, farthest first
               "LIMX_IN", "LIMY_IN", "LIMZ_IN", "PROBE_IN",
               "SD_SCK", "SD_MISO", "SD_MOSI", "SD_CS",
               "+5V", "+3V3", "VIN5", "VCC_BUF", "LEDR",
               "STEPX_G", "DIRX_G", "STEPY_G", "DIRY_G",
               "STEPZ_G", "DIRZ_G", "SPIN_G", "AUX1_G",
               "ZSTEP", "ZDIR", "XSTEP", "XDIR", "YSTEP", "YDIR",
               "SPIN5", "AUX1", "NGATE", "RLY_N"]

VIA_PTS = []

def stitch_via(gp):
    if any(abs(gp[0] - vx) < 1.27 and abs(gp[1] - vy) < 1.27
           for vx, vy in VIA_PTS):
        return False
    add_via("GND", gp)
    VIA_PTS.append(gp)
    block_square([0, 1], gp[0], gp[1], VIA_D / 2 + CLEARANCE + 0.24, "GND")
    return True

# targeted pre-routing stitch: every single-layer SMD GND pad gets an
# adjacent via to the opposite pour NOW, while the board is empty — later
# routing can then never strand it in a pour pocket
for (px, py, r, net, lays) in all_pads:
    if net != "GND" or len(lays) != 1:
        continue
    lay = lays[0]
    ic, jc = cell(px, py)
    placed = False
    for dist in (3, 4, 5):
        for ux, uy in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            ij = (ic + ux * dist, jc + uy * dist)
            ray = [(ic + ux * k, jc + uy * k) for k in range(1, dist)]
            if (is_free(0, ij, "GND", 1) and is_free(1, ij, "GND", 1)
                    and all(is_free(lay, c, "GND", 0) for c in ray)):
                gp = (ij[0] * GRID, ij[1] * GRID)
                if not stitch_via(gp):
                    continue
                add_track("GND", (px, py), gp, lay, 0.4)
                for c in ray:
                    block_square([lay], c[0] * GRID, c[1] * GRID, 0.44, "GND")
                placed = True
                break
        if placed:
            break
    if not placed:
        print(f"WARN: no pre-stitch via for SMD GND pad at ({px}, {py})")

# board-wide GND stitch grid: ties F/B pours together; the router treats
# these vias as obstacles
for gx in range(10, int(BOARD_W) - 5, 13):
    for gy in range(10, int(BOARD_H) - 5, 13):
        ij = cell(float(gx), float(gy))
        if is_free(0, ij, "GND", 1) and is_free(1, ij, "GND", 1):
            stitch_via((ij[0] * GRID, ij[1] * GRID))

failed = []
for net in ROUTE_ORDER:
    pads = list(pads_by_net[net])
    width = TRACK_W.get(net, TRACK_W["default"])
    seed = cell(pads[0][0], pads[0][1])
    cloud = {(seed, l) for l in pads[0][2]}
    seed_stub_pending = True
    for p in sorted(pads[1:], key=lambda p: abs(cell(p[0], p[1])[0] - seed[0])
                    + abs(cell(p[0], p[1])[1] - seed[1])):
        px, py, slayers = p
        p = (px, py)
        start = cell(px, py)
        if any((start, l) in cloud for l in slayers):
            pad_stub(net, p, slayers[0], width)  # lands on existing copper
            continue
        rest_targets = list(cloud)
        w = width
        path = astar(net, [start], rest_targets, w, slayers)
        if path is None and w > 0.5:
            w = 0.5  # narrow fallback for congested spots
            path = astar(net, [start], rest_targets, w, slayers)
        if path is None:
            extra = 1 if w > 0.5 else 0
            sf = [is_free(l, start, net, extra) for l in (0, 1)]
            print(f"FAIL {net} pad@{p} start={start} startfree={sf}")
            if os.environ.get("DEBUG_BLOCK"):
                i0, j0 = start
                for lay in (0, 1):
                    print(f" layer {lay} window around start:")
                    for j in range(j0 - 4, j0 + 5):
                        row = ""
                        for i in range(i0 - 16, i0 + 3):
                            s = blocked[lay].get((i, j), set())
                            row += ("." if not s else
                                    ("o" if not any(n != net for n in s) else "#"))
                        print(f"  j={j}: {row}")
                    for i in range(i0 - 16, i0 + 3):
                        s = blocked[lay].get((i, j0), set())
                        if s and any(n != net for n in s):
                            print(f"   ({i},{j0}) {sorted(s)[:4]}")
            failed.append((net, p))
            continue
        pad_stub(net, p, path[0][1], w)  # pad -> first cell, path's layer
        if seed_stub_pending and path[-1][0] == seed:
            pad_stub(net, (pads[0][0], pads[0][1]), path[-1][1], w)
            seed_stub_pending = False
        cloud |= commit_path(net, path, w)
        cloud |= {(start, l) for l in slayers}
if failed:
    print("UNROUTED:", failed)
else:
    print("all nets routed")

# GND zones on both layers ----------------------------------------------
for lay in (pcbnew.F_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(board)
    z.SetLayer(lay)
    z.SetNet(netinfo["GND"])
    z.SetLocalClearance(FromMM(CLEARANCE + 0.05))
    z.SetMinThickness(FromMM(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(FromMM(0.4))
    z.SetThermalReliefSpokeWidth(FromMM(0.6))
    outline = z.Outline()
    idx = outline.NewOutline()
    for (x, y) in [(0.5, 0.5), (BOARD_W - 0.5, 0.5),
                   (BOARD_W - 0.5, BOARD_H - 0.5), (0.5, BOARD_H - 0.5)]:
        outline.Append(FromMM(x), FromMM(y), idx)
    board.Add(z)

filler = pcbnew.ZONE_FILLER(board)
filler.Fill(board.Zones())

pcbnew.SaveBoard(OUT, board)
print("wrote", OUT)
