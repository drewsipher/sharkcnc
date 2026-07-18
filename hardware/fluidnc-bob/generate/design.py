"""Single source of truth for the fluidnc-bob board — rev B.

Target: ESP32-S3-WROOM-1 N16R8 dual-USB-C devkit, 44 pins (2x22).
Buffer: SN74HC541N run at ~4.2V (GS1A drop from 5V) so 3.3V inputs meet VIH.
Passives 0805 SMD, diodes GS1A (SMA), connectors Molex KK-254 + pin header.

Devkit physical layout (USB-C ports at the BOTTOM end):
pads 1-22 = left column top->bottom, pads 23-44 = right column top->bottom.
Pin 1 (3V3) is at the end AWAY from the USB connectors.
N16R8 module: GPIO 35/36/37 reserved (octal PSRAM). Strapping 0/3/45/46 left NC.
GPIO 19/20 = native USB, 43/44 = UART0 — left NC.
"""

# net names --------------------------------------------------------------
P5V, P3V3, GND = "+5V", "+3V3", "GND"

DEVKIT_LEFT = [  # (pad, devkit pin name, net or None)
    (1,  "3V3",    P3V3),
    (2,  "3V3",    None),
    (3,  "RST",    None),
    (4,  "GPIO4",  "STEPX_G"),
    (5,  "GPIO5",  "DIRX_G"),
    (6,  "GPIO6",  "STEPY_G"),
    (7,  "GPIO7",  "DIRY_G"),
    (8,  "GPIO15", "STEPZ_G"),
    (9,  "GPIO16", "DIRZ_G"),
    (10, "GPIO17", "SPIN_G"),
    (11, "GPIO18", "AUX1_G"),
    (12, "GPIO8",  None),
    (13, "GPIO3",  None),   # strapping
    (14, "GPIO46", None),   # strapping
    (15, "GPIO9",  None),
    (16, "GPIO10", None),
    (17, "GPIO11", None),
    (18, "GPIO12", None),
    (19, "GPIO13", None),
    (20, "GPIO14", None),
    (21, "5V",     P5V),
    (22, "GND",    GND),
]
DEVKIT_RIGHT = [
    (23, "GND",    GND),
    (24, "TX43",  None),
    (25, "RX44",  None),
    (26, "GPIO1",  "LIMX_G"),
    (27, "GPIO2",  "LIMY_G"),
    (28, "GPIO42", "LIMZ_G"),
    (29, "GPIO41", "PROBE_G"),
    (30, "GPIO40", "SD_SCK"),
    (31, "GPIO39", "SD_MISO"),
    (32, "GPIO38", "SD_MOSI"),
    (33, "GPIO37", None),   # octal PSRAM
    (34, "GPIO36", None),   # octal PSRAM
    (35, "GPIO35", None),   # octal PSRAM
    (36, "GPIO0",  None),   # strapping
    (37, "GPIO45", None),   # strapping
    (38, "GPIO48", None),   # onboard RGB LED on most clones
    (39, "GPIO47", None),
    (40, "GPIO21", "SD_CS"),
    (41, "GPIO20", None),   # native USB D+
    (42, "GPIO19", None),   # native USB D-
    (43, "GND",    GND),
    (44, "GND",    GND),
]

FP = {  # footprint lib:name shorthands
    "pads2": "fluidnc-bob:WirePads_1x02_P5.08mm",
    "pads3": "fluidnc-bob:WirePads_1x03_P5.08mm",
    "pads6": "fluidnc-bob:WirePads_1x06_P5.08mm",
    "dip20": "Package_DIP:DIP-20_W7.62mm_Socket",
    "to92":  "Package_TO_SOT_THT:TO-92_Inline",
    "r":     "Resistor_SMD:R_0805_2012Metric",
    "c":     "Capacitor_SMD:C_0805_2012Metric",
    "cp":    "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm",
    "sma":   "Diode_SMD:D_SMA",
    "fuse":  "Fuse:Fuse_1210_3225Metric",
    "led":   "LED_THT:LED_D5.0mm",
    "socket": "fluidnc-bob:ESP32_S3_DevKit_44_Socket",
    "hole":  "MountingHole:MountingHole_3.2mm_M3_Pad",
}

COMPONENTS = {}

def add(ref, sym, value, fp, pins):
    COMPONENTS[ref] = dict(sym=sym, value=value, fp=FP[fp], pins=pins)

# --- ESP32-S3 devkit socket --------------------------------------------
add("A1", "ESP32_S3_DevKit_44", "ESP32-S3-DevKit-44", "socket",
    {str(p): n for p, _, n in DEVKIT_LEFT + DEVKIT_RIGHT if n})

# --- 74HC541 buffer at reduced VCC -------------------------------------
# Inputs pins 2-9 (one side), outputs 18..11 (other side): Y1=18 ... Y8=11
# natural channel order (A1=X step … A8=aux); channels are interchangeable —
# feel free to permute during board layout, a buffer bit is a buffer bit
add("U1", "74HC541", "SN74HC541N", "dip20", {
    "1": GND, "19": GND,                      # OE1#, OE2#
    "2": "STEPX_G", "18": "XSTEP",
    "3": "DIRX_G",  "17": "XDIR",
    "4": "STEPY_G", "16": "YSTEP",
    "5": "DIRY_G",  "15": "YDIR",
    "6": "STEPZ_G", "14": "ZSTEP",
    "7": "DIRZ_G",  "13": "ZDIR",
    "8": "SPIN_G",  "12": "SPIN5",
    "9": "AUX1_G",  "11": "AUX1",
    "20": "VCC_BUF", "10": GND,
})
# GS1A drops 5V -> ~4.2V so HC-family VIH (~0.7*VCC) accepts 3.3V logic
add("D3", "D", "GS1A", "sma", {"1": "VCC_BUF", "2": P5V})  # K=VCC_BUF, A=+5V
add("C2", "C", "100nF", "c", {"1": "VCC_BUF", "2": GND})

# boot-time pulldowns on buffer inputs
for i, net in enumerate(["STEPX_G", "DIRX_G", "STEPY_G", "DIRY_G",
                         "STEPZ_G", "DIRZ_G", "SPIN_G", "AUX1_G"]):
    add(f"R1{i}", "R", "100k", "r", {"1": net, "2": GND})

# --- driver output wire pads (shared GND return to each FMD2740C) -------
for ref, axis in [("J2", "X"), ("J3", "Y"), ("J4", "Z")]:
    add(ref, "PadS_R_03", f"{axis}-DRV", "pads3",
        {"1": f"{axis}STEP", "2": f"{axis}DIR", "3": GND})

# --- aux buffered output ------------------------------------------------
add("J8", "PadS_R_02", "AUX", "pads2", {"1": "AUX1", "2": GND})

# --- inputs: limits + probe (one pad pair per input) --------------------
add("J5", "PadS_02", "LIM-X", "pads2", {"1": "LIMX_IN", "2": GND})
add("J6", "PadS_02", "LIM-Y", "pads2", {"1": "LIMY_IN", "2": GND})
add("J10", "PadS_02", "LIM-Z", "pads2", {"1": "LIMZ_IN", "2": GND})
add("J11", "PadS_02", "PROBE", "pads2", {"1": "PROBE_IN", "2": GND})

INPUTS = [("LIMX", 0), ("LIMY", 1), ("LIMZ", 2), ("PROBE", 3)]
for name, i in INPUTS:
    add(f"R2{i}", "R", "10k", "r", {"1": P3V3, "2": f"{name}_IN"})   # pullup
    add(f"C2{i}", "C", "100nF", "c", {"1": f"{name}_IN", "2": GND})  # filter
    add(f"R3{i}", "R_H", "1k", "r", {"1": f"{name}_IN", "2": f"{name}_G"})

# --- microSD breakout wire pads (module has its own 3.3V reg + 4050) ----
add("J9", "PadS_06", "SD", "pads6",
    {"1": P5V, "2": "SD_SCK", "3": "SD_MISO", "4": "SD_MOSI",
     "5": "SD_CS", "6": GND})

# --- relay driver -------------------------------------------------------
add("Q1", "Q_NMOS", "2N7000", "to92", {"1": GND, "2": "NGATE", "3": "RLY_N"})
add("R40", "R", "1k", "r", {"1": "SPIN5", "2": "NGATE"})
add("R41", "R", "100k", "r", {"1": "NGATE", "2": GND})
add("D1", "D_V", "GS1A", "sma", {"1": P5V, "2": "RLY_N"})  # flyback K=+5V
add("J7", "PadS_R_02", "RELAY", "pads2", {"1": P5V, "2": "RLY_N"})

# --- power entry --------------------------------------------------------
add("J1", "PadS_R_02", "5V-IN", "pads2", {"1": "VIN5", "2": GND})
add("F1", "Fuse_H", "1210L150/16WR", "fuse", {"1": "VIN5", "2": P5V})
add("D2", "D_V", "GS1A", "sma", {"1": P5V, "2": GND})  # reverse crowbar K=+5V
add("C1", "C_Pol", "100uF", "cp", {"1": P5V, "2": GND})
add("C3", "C", "100nF", "c", {"1": P5V, "2": GND})
add("LED1", "LED_V", "PWR", "led", {"1": GND, "2": "LEDR"})   # K=1, A=2
add("R50", "R", "1k", "r", {"1": P5V, "2": "LEDR"})

for i in range(1, 5):
    add(f"H{i}", None, "M3", "hole", {})

def nets():
    ns = {}
    for ref, c in COMPONENTS.items():
        for pin, net in c["pins"].items():
            ns.setdefault(net, []).append((ref, pin))
    return ns

if __name__ == "__main__":
    for net, pads in sorted(nets().items()):
        print(f"{net:10s} {' '.join(f'{r}.{p}' for r, p in pads)}")
