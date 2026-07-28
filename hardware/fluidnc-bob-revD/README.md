# fluidnc-bob rev D — ESP32-S3 FluidNC breakout, single 36 V supply

Successor to the rev C board, folding in everything learned during the
2026-07-27 bench bring-up. **Deliverable = schematic + verified netlist +
footprint/3D assignments + DigiKey BOM.** Drew does the PCB layout
("Update PCB from Schematic" pulls everything in; footprints are from
KiCad's standard libraries with 3D models, plus the proven rev C devkit
socket footprint).

**Design state:** drawn-wire schematic (power symbols + real wires;
global labels only for long-haul nets) passes `kicad-cli sch erc` with
**0 violations**; netlist machine-verified against `generate/design.py`
(connectivity-partition + power-name diff); every footprint + 3D model
existence-checked against the installed KiCad 10 libraries (one
exception: the devkit socket uses the proven rev C custom footprint,
which has no 3D model). Each non-passive part carries hidden **Digikey**
and **Datasheet** URL properties. Unbuilt.

**Devkit:** single 44-pin symbol + the rev C `ESP32_S3_DevKit_44_Socket`
footprint (copied into `fluidnc-bob-revD.pretty/`, socketed via two
PPTC221LFBN-RC strips). DigiKey-stocked spare: ESP32-S3-DEVKITC-1-N8
(identical pin legend; see BOM.md).

**Note:** the 74AHCT541 channel assignment differs from rev C (channels
follow devkit pad order so the schematic bus is crossing-free). This is
internal to the board — the GPIO map and `config.yaml` are unchanged.

## What changed vs rev C (and why)

1. **Onboard 36 V→5 V buck (Traco TSR 1-4850WI, 9–72 V in, 1 A).** One
   power input from the motor PSU — no separate 5 V supply. Bring-up
   fact that forced this: the devkit does NOT bridge USB power to its
   5 V pin, so the board rail must always be supplied externally.
2. **Fuse (2 A/63 V) + SMBJ40A TVS** on the 36 V input. Reversed input
   forward-biases the TVS and blows the fuse (order spares).
3. **`5V-SRC` jumper (JP1):** 1-2 = buck (normal), 2-3 = `5V-EXT`
   terminal (J12) for USB-free bench work with a lab supply. USB and
   board 5 V simultaneously is safe on this devkit (verified isolated).
4. **SN74AHCT541N at a full 5 V** — TTL V_IH (2 V) accepts the ESP's
   3.3 V directly; the rev C GS1A VCC-drop hack is gone. Step/dir
   outputs get **100 Ω series resistors**: ~9 mA per driver opto
   (mid-spec) and caps the buffer package current at ~54 mA (< 75 mA
   abs max) — rev C could exceed the rating with all channels lit.
5. **Driver ENABLE finally wired.** Per-axis terminal carries +5 V (EN+)
   and a shared EN− sink (Q2, driven by GPIO8). Red LED2 lights when
   motors are disabled. FluidNC: `shared_stepper_disable_pin: gpio.8`.
6. **E-stop input on GPIO9**, conditioned like the limits (10 k pullup,
   1 k series, 100 nF). Wire an NC switch to GND. Configure per the
   FluidNC control-pin docs for the firmware you flash (verify the key
   name — v4 changed some control options).
7. **Real connectors** (rev C was solder pads): Phoenix MKDS 5.08 screw
   terminals for power/drivers/spindle, Molex KK-254 (friction lock,
   crimp housings in the BOM) for limits/probe/E-stop/aux; SD keeps the
   rev C 2×6 header so the salvaged module plugs straight in.
10. **Onboard spindle relay** (Omron G5LE-14 DC5, SPDT 10 A/250 VAC,
   5 V coil) replaces the harvested off-board Songle. Q1 sinks the coil
   (S1M flyback); contacts COM/NO/NC on J10 switch the spindle's AC
   line. Wire the spindle through COM+NO so a dead board = spindle off.
8. **SD module runs from 3.3 V** (proven on the bench; keeps its level
   shifter below ESP-safe voltages).
9. **GPIO map carried over unchanged from the fabbed, verified rev C
   board** — the traced `.kicad_pcb` map, not the stale rev C docs — so
   the working `config.yaml` needs only additions, no changes.

## GPIO map (ESP32-S3)

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| X step / dir | 18 / 17 | | X / Y / Z limit | 42 / 2 / 1 |
| Y step / dir | 16 / 15 | | Probe | 41 |
| Z step / dir | 7 / 6 | | **E-stop (new)** | **9** |
| Spindle relay | 5 | | SD CS / MOSI / SCK / MISO | 10 / 11 / 12 / 13 |
| Aux out | 4 | | **Stepper disable (new)** | **8** |

## FluidNC config delta (relative to rev C `config.yaml`)

```yaml
axes:
  shared_stepper_disable_pin: gpio.8    # high = motors disabled (LED2 on)
# E-stop switch (NC to GND) on gpio.9 — add per FluidNC control-pin docs
```

## Connector wiring

| Ref | Type | Pins | Goes to |
|---|---|---|---|
| J1 | screw 2-pos | +36V, GND | motor PSU (fused, TVS-protected) |
| J2/J3/J4 | screw 5-pos | STEP, DIR, GND, +5V, EN− | FMD2740C: SP+, DIR+, (SP−+DIR− bridge), EN+, EN− |
| J5/J6/J7 | Molex KK-254 2 | SIG, GND | X / Y / Z limit switch (closes to GND) |
| J8 | Molex KK-254 2 | SIG, GND | probe |
| J9 | Molex KK-254 2 | SIG, GND | E-stop (NC to GND) |
| J10 | screw 3-pos | COM, NO, NC | spindle AC switch (onboard G5LE-14 relay contacts) |
| J11 | Molex KK-254 2 | AUX, GND | spare buffered 5 V output (future spindle PWM) |
| J12 | screw 2-pos | +5V-EXT, GND | bench 5 V (JP1 → 2-3) |
| J13 | header 2×6 | 5=3V3, 7=MISO, 8=MOSI, 9=SCK, 10=CS, 11/12=GND | salvaged microSD module — **same pad map as fabbed rev C** |
| JP1 | header 1×3 | BUCK / +5V / EXT | 5 V source select (shunt on 1-2 normally) |

Driver interface is the bench-proven common-cathode sourcing: STEP→SP+,
DIR→DIR+, GND→SP−&DIR− bridge; step/dir stay **active high** in config
(same as rev C). EN unwired ⇒ enabled, so a rev C-style 3-wire hookup
still works before the EN lines are connected.

## ⚠ Layout checklist

1. The devkit socket footprint is the rev C one that already fits the
   fabbed board — no row-spacing gamble this time. Still check the
   devkit's printed pin legend against `DEVKIT_LEFT/RIGHT` in
   `generate/design.py` (pin 1 = 3V3, antenna end).
2. 36 V nets (J1, F1, D1, C1, C2, U2.1): ≥ 0.5 mm clearance, short and
   wide; keep them away from the limit/probe/SD signal area.
3. TSR buck: C1/C2 close to U2.1; keep the buck a few cm from the ESP
   antenna end.
4. Single-point ground at the supply; motor power never crosses the
   logic ground pour (same rule as rev C).
5. LED2/EN block near the driver terminals so the "MOTORS OFF" light is
   visible at the machine.
6. H1–H4 = M3 mounting holes, put one near each corner.
7. Solder-jumper alternative: if JP1 feels like a snag hazard, rotate it
   flat or swap for a 2-pos + default-closed trace you cut for bench use.
8. **MAINS on the RLY1/J10 corner.** Keep the relay contact traces and
   J10 in one corner, ≥ 6.4 mm creepage from ALL logic (slot the board
   under the relay if convenient), trace width ≥ 1 mm, no ground pour
   under the contact area. Coil side (pins 2/5) is logic — the relay
   body is the isolation barrier (4 kV impulse rated).

## Regenerating / verifying

```sh
python3 generate/design.py          # netlist dump + single-pad-net assert
python3 generate/gen_sch.py         # schematic (drawn wires + labels)
kicad-cli sch erc --exit-code-violations fluidnc-bob-revD.kicad_sch
python3 generate/verify_netlist.py  # diff KiCad netlist vs design.py
```

`generate/design.py` is the netlist source of truth **for the schematic
only** — once a board is fabbed, the `.kicad_pcb` becomes the pin
authority (rev C lesson, see `docs/HANDOFF.md`).

Ordering: import `digikey_bom.csv` at DigiKey (matches on MPN); details
and alternatives in `BOM.md`.
