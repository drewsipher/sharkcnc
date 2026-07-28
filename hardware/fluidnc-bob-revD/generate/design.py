"""Single source of truth for the fluidnc-bob board — rev D.

Rev D goals (everything learned bringing up rev C):
  * Single 36 V input from the motor PSU; onboard TSR 1-4850WI buck
    (9-72 V in, 5 V/1 A out) makes the logic rail — no separate 5 V supply.
    (Bring-up fact: the devkit does NOT bridge USB power to its 5 V pin,
    so the board rail must always be supplied; USB+board power is safe.)
  * SN74AHCT541N at a full 5 V (TTL V_IH=2 V accepts 3.3 V directly) —
    the rev C GS1A VCC-drop hack is gone. 100R series resistors on the six
    step/dir outputs cap the per-pin and package current.
  * Driver ENABLE wired at last: EN+ pads get +5 V, all EN- lines sink
    through Q2 (MMBT2222A) driven by GPIO8. FluidNC:
    axes: shared_stepper_disable_pin: gpio.8 (active high = disabled).
    LED2 (red) lights when motors are disabled.
  * E-stop input on GPIO9, conditioned like the limits.
  * Real connectors (DigiKey order): Phoenix MKDS 5.08 screw terminals for
    power/drivers/relay, JST-XH for limits/probe/E-stop/SD/aux.
  * GPIO map for step/dir/limits/probe/SD is IDENTICAL to the fabbed,
    verified rev C board (see fluidnc-bob.kicad_pcb trace, 2026-07-27) —
    config.yaml carries over; only the additions are new.

Devkit: 44-pin dual-USB-C ESP32-S3-WROOM-1 (N16R8), socketed as two 1x22
female headers A1 (left column, pads 1-22) and A2 (right column, pads
23-44 -> pins 1-22). Pin 1 = 3V3 at the antenna end. Verify the printed
legend against these tables before layout (README checklist).

FMD2740C interface (proven on the bench 2026-07-27): common-cathode
sourcing — STEP->SP+, DIR->DIR+, GND->SP-&DIR- bridge; ~9 mA per opto
at 5 V through the internal ~270R. EN is opto too: +5V->EN+, EN-->Q2.
"""

# net names --------------------------------------------------------------
P36I, P36, P5B, P5, P5X, P3V3, GND = ("+36V_IN", "+36V", "+5V_BUCK",
                                      "+5V", "+5V_EXT", "+3V3", "GND")

# devkit socket A1 = left column (antenna end = pin 1)
DEVKIT_LEFT = [   # (socket pin, devkit legend, net or None)
    (1,  "3V3",    P3V3),
    (2,  "3V3",    None),
    (3,  "RST",    None),
    (4,  "GPIO4",  "AUX1_G"),
    (5,  "GPIO5",  "SPIN_G"),
    (6,  "GPIO6",  "DIRZ_G"),
    (7,  "GPIO7",  "STEPZ_G"),
    (8,  "GPIO15", "DIRY_G"),
    (9,  "GPIO16", "STEPY_G"),
    (10, "GPIO17", "DIRX_G"),
    (11, "GPIO18", "STEPX_G"),
    (12, "GPIO8",  "EN_G"),      # NEW: shared stepper-disable
    (13, "GPIO3",  None),        # strapping
    (14, "GPIO46", None),        # strapping
    (15, "GPIO9",  "ESTOP_G"),   # NEW: E-stop input
    (16, "GPIO10", "SD_CS"),
    (17, "GPIO11", "SD_MOSI"),
    (18, "GPIO12", "SD_SCK"),
    (19, "GPIO13", "SD_MISO"),
    (20, "GPIO14", None),
    (21, "5V",     P5),
    (22, "GND",    GND),
]
DEVKIT_RIGHT = [  # devkit pads 23-44 top->bottom (index 1 -> pad 23)
    (1,  "GND",    GND),
    (2,  "TX43",   None),
    (3,  "RX44",   None),
    (4,  "GPIO1",  "LIMZ_G"),
    (5,  "GPIO2",  "LIMY_G"),
    (6,  "GPIO42", "LIMX_G"),
    (7,  "GPIO41", "PROBE_G"),
    (8,  "GPIO40", None),
    (9,  "GPIO39", None),
    (10, "GPIO38", None),
    (11, "GPIO37", None),        # octal PSRAM
    (12, "GPIO36", None),        # octal PSRAM
    (13, "GPIO35", None),        # octal PSRAM
    (14, "GPIO0",  None),        # strapping
    (15, "GPIO45", None),        # strapping
    (16, "GPIO48", None),        # onboard RGB LED on most clones
    (17, "GPIO47", None),
    (18, "GPIO21", None),
    (19, "GPIO20", None),        # native USB D+
    (20, "GPIO19", None),        # native USB D-
    (21, "GND",    GND),
    (22, "GND",    GND),
]

FP = {  # footprint shorthands — ALL from KiCad's standard libs (3D incl.)
    "socket": "fluidnc-bob-revD:ESP32_S3_DevKit_44_Socket",
    "dip20":  "Package_DIP:DIP-20_W7.62mm_Socket",
    "tsr1":   "Converter_DCDC:Converter_DCDC_TRACO_TSR-1_THT",
    "sot23":  "Package_TO_SOT_SMD:SOT-23",
    "r":      "Resistor_SMD:R_0805_2012Metric",
    "c":      "Capacitor_SMD:C_0805_2012Metric",
    "c1206":  "Capacitor_SMD:C_1206_3216Metric",
    "cp":     "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm",
    "sma":    "Diode_SMD:D_SMA",
    "smb":    "Diode_SMD:D_SMB",
    "fuse":   "Fuse:Fuse_1206_3216Metric",
    "led":    "LED_THT:LED_D5.0mm",
    "tb2":    "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
    "tb5":    "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-5-5.08_1x05_P5.08mm_Horizontal",
    "xh2":    "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical",
    "xh6":    "Connector_JST:JST_XH_B6B-XH-A_1x06_P2.50mm_Vertical",
    "hdr3":   "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
    "hole":   "MountingHole:MountingHole_3.2mm_M3_Pad",
}

COMPONENTS = {}

def add(ref, sym, value, fp, pins, info=None):
    COMPONENTS[ref] = dict(sym=sym, value=value, fp=FP[fp], pins=pins,
                           info=info or {})

# --- ESP32-S3 devkit socket (rev C custom footprint, 2x22 rows) ---------
# DEVKIT_RIGHT rows are devkit pads 23-44 (entry 1 -> pad 23, etc.)
add("A1", "ESP32_S3_DevKit_44", "ESP32-S3-DevKit-44", "socket",
    dict({str(p): n for p, _, n in DEVKIT_LEFT if n},
         **{str(i + 23): n for i, (_, _, n) in enumerate(DEVKIT_RIGHT) if n}),
    info=dict(
        digikey="https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-DEVKITC-1-N8/15199021",
        datasheet="https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide.html"))

# --- power entry: 36V -> fuse -> TVS/bulk -> buck -> jumper -> +5V ------
add("J1", "TB_02", "36V-IN", "tb2", {"1": P36I, "2": GND},
    info=dict(digikey="https://www.digikey.com/en/products/detail/phoenix-contact/1715721/260631",
              datasheet="https://www.phoenixcontact.com/en-us/products/printed-circuit-board-terminal-mkds-15-2-508-1715721"))
add("F1", "Fuse_H", "2A", "fuse", {"1": P36I, "2": P36},
    info=dict(digikey="https://www.digikey.com/en/products/detail/littelfuse-inc/0466002.NR/F1457CT-ND/521355",
              datasheet="https://www.digikey.com/en/products/result?keywords=0466002.NR"))
add("D1", "D_V", "SMBJ40A", "smb", {"1": P36, "2": GND},
    info=dict(digikey="https://www.digikey.com/en/products/result?keywords=SMBJ40A",
              datasheet="https://www.digikey.com/en/products/result?keywords=SMBJ40A"))   # K=+36V, A=GND
add("C1", "C_Pol", "100uF/50V", "cp", {"1": P36, "2": GND})
add("C2", "C", "100nF", "c", {"1": P36, "2": GND})
add("U2", "TSR1", "TSR 1-4850WI", "tsr1",
    {"1": P36, "2": GND, "3": P5B},
    info=dict(digikey="https://www.digikey.com/en/products/detail/traco-power/TSR-1-4850WI/10438384",
              datasheet="https://www.tracopower.com/model/tsr-1-4850wi"))
# 5V source select: 1-2 = buck (normal), 2-3 = external bench 5V
add("JP1", "HDR_03", "5V-SRC", "hdr3", {"1": P5B, "2": P5, "3": P5X},
    info=dict(digikey="https://www.digikey.com/en/products/result?keywords=PRPC040SAAN-RC",
              datasheet="https://www.digikey.com/en/products/result?keywords=PRPC040SAAN-RC"))
add("J12", "TB_02", "5V-EXT", "tb2", {"1": P5X, "2": GND},
    info=dict(digikey="https://www.digikey.com/en/products/detail/phoenix-contact/1715721/260631",
              datasheet="https://www.phoenixcontact.com/en-us/products/printed-circuit-board-terminal-mkds-15-2-508-1715721"))
add("C3", "C_1206", "10uF/25V", "c1206", {"1": P5, "2": GND})
add("C4", "C", "100nF", "c", {"1": P5, "2": GND})
add("R50", "R", "1k", "r", {"1": P5, "2": "LEDR"})
add("LED1", "LED_V", "PWR", "led", {"2": "LEDR", "1": GND},
    info=dict(digikey="https://www.digikey.com/en/products/result?keywords=WP7113GD",
              datasheet="https://www.kingbrightusa.com/images/catalog/SPEC/WP7113GD.pdf"))  # A=2, K=1

# --- 74AHCT541 buffer at full 5V ----------------------------------------
# Channel order = devkit pad order (pads 4..11 -> A1..A8), so the
# schematic bus is crossing-free; a buffer bit is a buffer bit.
add("U1", "74AHCT541", "SN74AHCT541N", "dip20", {
    "1": GND, "19": GND,                      # OE1#, OE2#
    "2": "AUX1_G",  "18": "AUX5",
    "3": "SPIN_G",  "17": "SPIN5",
    "4": "DIRZ_G",  "16": "ZDIR5",
    "5": "STEPZ_G", "15": "ZSTEP5",
    "6": "DIRY_G",  "14": "YDIR5",
    "7": "STEPY_G", "13": "YSTEP5",
    "8": "DIRX_G",  "12": "XDIR5",
    "9": "STEPX_G", "11": "XSTEP5",
    "20": P5, "10": GND,
},
    info=dict(digikey="https://www.digikey.com/en/products/detail/texas-instruments/SN74AHCT541N/375903",
              datasheet="https://www.ti.com/lit/ds/symlink/sn74ahct541.pdf"))
add("C5", "C", "100nF", "c", {"1": P5, "2": GND})

# boot-time pulldowns on buffer inputs
for i, net in enumerate(["AUX1_G", "SPIN_G", "DIRZ_G", "STEPZ_G",
                         "DIRY_G", "STEPY_G", "DIRX_G", "STEPX_G"]):
    add(f"R1{i}", "R", "100k", "r", {"1": net, "2": GND})

# 100R series on the six step/dir outputs (current cap; ~9mA per opto)
for i, (n5, n) in enumerate([("XSTEP5", "XSTEP"), ("XDIR5", "XDIR"),
                             ("YSTEP5", "YSTEP"), ("YDIR5", "YDIR"),
                             ("ZSTEP5", "ZSTEP"), ("ZDIR5", "ZDIR")]):
    add(f"R6{i}", "R_H", "100R", "r", {"1": n, "2": n5})
add("R66", "R_H", "100R", "r", {"1": "AUX", "2": "AUX5"})

# --- driver terminals: STEP DIR GND | +5V(EN+) EN-(sink) ----------------
for ref, axis in [("J2", "X"), ("J3", "Y"), ("J4", "Z")]:
    add(ref, "TB_05", f"{axis}-DRV", "tb5",
        {"1": f"{axis}STEP", "2": f"{axis}DIR", "3": GND,
         "4": P5, "5": "EN_SINK"},
        info=dict(digikey="https://www.digikey.com/en/products/result?keywords=1715750",
                  datasheet="https://www.phoenixcontact.com/en-us/products/printed-circuit-board-terminal-mkds-15-5-508-1715750"))

# --- shared enable sink: GPIO8 high = motors disabled -------------------
add("Q2", "Q_NPN", "MMBT2222A", "sot23",
    {"1": "Q2B", "2": GND, "3": "EN_SINK"},
    info=dict(digikey="https://www.digikey.com/en/products/result?keywords=MMBT2222A-7-F",
              datasheet="https://www.diodes.com/assets/Datasheets/ds30041.pdf"))   # SOT-23: 1=B, 2=E, 3=C
add("R42", "R_H", "1k", "r", {"1": "EN_G", "2": "Q2B"})
add("R43", "R", "100k", "r", {"1": "Q2B", "2": GND})
add("R51", "R", "1k", "r", {"1": P5, "2": "LED2R"})
add("LED2", "LED_V", "MOTORS-OFF", "led", {"2": "LED2R", "1": "EN_SINK"},
    info=dict(digikey="https://www.digikey.com/en/products/result?keywords=WP7113ID",
              datasheet="https://www.kingbrightusa.com/images/catalog/SPEC/WP7113ID.pdf"))

# --- inputs: limits + probe + E-stop (pullup 10k, series 1k, 100nF) -----
INPUTS = [("LIMX", 0, "J5", "LIM-X"), ("LIMY", 1, "J6", "LIM-Y"),
          ("LIMZ", 2, "J7", "LIM-Z"), ("PROBE", 3, "J8", "PROBE"),
          ("ESTOP", 4, "J9", "E-STOP")]
for name, i, jref, jval in INPUTS:
    add(jref, "XH_02", jval, "xh2", {"1": f"{name}_IN", "2": GND},
        info=dict(digikey="https://www.digikey.com/en/products/result?keywords=B2B-XH-A(LF)(SN)",
                  datasheet="https://www.jst-mfg.com/product/pdf/eng/eXH.pdf"))
    add(f"R2{i}", "R", "10k", "r", {"1": P3V3, "2": f"{name}_IN"})
    add(f"C2{i}", "C", "100nF", "c", {"1": f"{name}_IN", "2": GND})
    add(f"R3{i}", "R_H", "1k", "r", {"1": f"{name}_G", "2": f"{name}_IN"})

# --- spindle relay (harvested Songle 5V coil, off-board) ----------------
add("Q1", "Q_NPN", "MMBT2222A", "sot23",
    {"1": "Q1B", "2": GND, "3": "RLY_N"},
    info=dict(digikey="https://www.digikey.com/en/products/result?keywords=MMBT2222A-7-F",
              datasheet="https://www.diodes.com/assets/Datasheets/ds30041.pdf"))
add("R40", "R_H", "1k", "r", {"1": "SPIN5", "2": "Q1B"})
add("R41", "R", "100k", "r", {"1": "Q1B", "2": GND})
add("D2", "D_V", "S1M", "sma", {"1": P5, "2": "RLY_N"},
    info=dict(digikey="https://www.digikey.com/en/products/result?keywords=S1M-13-F",
              datasheet="https://www.diodes.com/assets/Datasheets/ds28002.pdf"))    # flyback K=+5V
add("J10", "TB_02", "RELAY", "tb2", {"1": P5, "2": "RLY_N"},
    info=dict(digikey="https://www.digikey.com/en/products/detail/phoenix-contact/1715721/260631",
              datasheet="https://www.phoenixcontact.com/en-us/products/printed-circuit-board-terminal-mkds-15-2-508-1715721"))

# --- aux buffered 5V output (future spindle PWM / isolator) -------------
add("J11", "XH_02", "AUX", "xh2", {"1": "AUX", "2": GND},
    info=dict(digikey="https://www.digikey.com/en/products/result?keywords=B2B-XH-A(LF)(SN)",
              datasheet="https://www.jst-mfg.com/product/pdf/eng/eXH.pdf"))

# --- microSD module: powered from 3V3 (proven safe on the bench) --------
add("J13", "XH_06", "SD", "xh6",
    {"1": P3V3, "2": GND, "3": "SD_CS", "4": "SD_MOSI",
     "5": "SD_SCK", "6": "SD_MISO"},
    info=dict(digikey="https://www.digikey.com/en/products/result?keywords=B6B-XH-A(LF)(SN)",
              datasheet="https://www.jst-mfg.com/product/pdf/eng/eXH.pdf"))

for i in range(1, 5):
    add(f"H{i}", "HOLE", "M3", "hole", {})

def nets():
    ns = {}
    for ref, c in COMPONENTS.items():
        for pin, net in c["pins"].items():
            ns.setdefault(net, []).append((ref, pin))
    return ns

if __name__ == "__main__":
    bad = [n for n, pads in nets().items() if len(pads) < 2]
    assert not bad, f"single-pad nets: {bad}"
    for net, pads in sorted(nets().items()):
        print(f"{net:10s} {' '.join(f'{r}.{p}' for r, p in pads)}")
