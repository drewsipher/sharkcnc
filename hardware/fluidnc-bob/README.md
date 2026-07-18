# fluidnc-bob rev C — ESP32-S3 FluidNC breakout (schematic + netlist)

FluidNC controller interface for the Sherline 2000 / FMD2740C drivers:
socketed **44-pin dual-USB-C ESP32-S3-WROOM-1 (N16R8)** devkit, SN74HC541N
5 V buffer (VCC dropped through a GS1A so 3.3 V inputs meet V_IH), filtered
limit/probe inputs, microSD breakout header, spindle relay driver.
All connections are **solder wire pads** — no connectors.

**Deliverable = schematic + verified netlist + footprint assignments.**
Board layout is done by hand in KiCad ("Update PCB from Schematic" pulls in
everything, including the custom footprints in `fluidnc-bob.pretty/`).

**Design state:** ERC clean (0 violations); netlist machine-verified
against `generate/design.py` (`generate/verify_netlist.py` — partition and
power-net comparison). Unbuilt, rev C.

## ⚠ Layout-time checklist

1. **MEASURE THE DEVKIT ROW SPACING.** The socket footprint assumes
   **22.86 mm (0.9")**; both 22.86 and 25.4 mm exist among 44-pin S3 boards.
   Set `ROWSP` in `generate/gen_fp.py` (or edit the footprint) to match, and
   check the devkit's printed pin legend against `DEVKIT_LEFT/RIGHT` in
   `generate/design.py` (pin 1 = 3V3, antenna end).
2. N16R8 (octal PSRAM): GPIO 35/36/37 intentionally unconnected.
3. U1's buffer channels are interchangeable — permute freely during routing.
4. Keep the FMD2740C signal runs and logic ground away from motor power;
   single-point ground at the supply.
5. Don't feed 5 V and devkit USB simultaneously unless the devkit's
   backfeed diode is verified.

## Wire pad groups

| Ref | Pads (1 = square) | Goes to |
|---|---|---|
| J1 | 5V, GND | 5 V PSU (through F1 fuse, D2 reverse crowbar) |
| J2 | STEP, DIR, GND | X driver: SP+, DIR+; GND wire bridges SP−+DIR− |
| J3 | STEP, DIR, GND | Y driver |
| J4 | STEP, DIR, GND | Z driver |
| J5 / J6 | SIG, GND | X / Y limit switch (closes to GND) |
| J10 / J11 | SIG, GND | Z limit / probe |
| J7 | +5V, COIL− | spindle relay coil (2N7000 low side, D1 flyback) |
| J8 | AUX, GND | spare buffered 5 V output |
| J9 | 5V, SCK, MISO, MOSI, CS, GND | microSD breakout (feed it 5 V; its 4050 + 3.3 V reg handle the rest) |

## GPIO map (ESP32-S3)

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| X step / dir | 4 / 5 | | X / Y / Z limit | 1 / 2 / 42 |
| Y step / dir | 6 / 7 | | Probe | 41 |
| Z step / dir | 15 / 16 | | SD SCK / MISO / MOSI / CS | 40 / 39 / 38 / 21 |
| Spindle relay | 17 | | Aux out | 18 |

Inputs: 10 k pullup to 3V3 + 1 k series + 100 nF on board.

## FluidNC config (starting point — S3 build of FluidNC)

```yaml
name: "Sherline 2000"
board: "fluidnc-bob-revC"
stepping: { engine: RMT, idle_ms: 255, pulse_us: 5, dir_delay_us: 5 }
axes:
  x:
    steps_per_mm: 1259.84        # 20 TPI, 200 steps, 1/8 microstep
    max_rate_mm_per_min: 700
    acceleration_mm_per_sec2: 40
    max_travel_mm: 228
    motor0:
      limit_neg_pin: gpio.1
      standard_stepper: { step_pin: gpio.4, direction_pin: gpio.5 }
  y:
    steps_per_mm: 1259.84
    max_rate_mm_per_min: 700
    acceleration_mm_per_sec2: 40
    max_travel_mm: 127
    motor0:
      limit_neg_pin: gpio.2
      standard_stepper: { step_pin: gpio.6, direction_pin: gpio.7 }
  z:
    steps_per_mm: 1259.84
    max_rate_mm_per_min: 400
    acceleration_mm_per_sec2: 25
    max_travel_mm: 158
    motor0:
      limit_neg_pin: gpio.42
      standard_stepper: { step_pin: gpio.15, direction_pin: gpio.16 }
probe: { pin: gpio.41 }
spi: { sck_pin: gpio.40, miso_pin: gpio.39, mosi_pin: gpio.38 }
sd: { cs_pin: gpio.21 }
OnOff: { output_pin: gpio.17, spinup_ms: 2000, spindown_ms: 2000 }
```

Upload jobs to SD and run locally — WiFi then carries only status/jog and
can never stall a cut. Verify pin syntax against the FluidNC wiki for the
S3 release you flash.

## Regenerating / verifying

```sh
python3 generate/gen_fp.py        # footprints (socket ROWSP lives here)
python3 generate/gen_sch.py       # schematic (drawn wires; self-checks)
kicad-cli sch erc --exit-code-violations fluidnc-bob.kicad_sch
python3 generate/verify_netlist.py  # diff KiCad netlist vs design.py
```

`generate/design.py` is the netlist source of truth. `generate/gen_pcb.py`
is retired (the rev B auto-layout experiment) — kept for reference only;
it no longer matches the current design.
