# fluidnc-bob rev D — ESP32-S3 FluidNC breakout, external 5 V logic supply

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
and **Datasheet** URL properties. **Built and brought up 2026-08-06**:
FluidNC v4.0.4 **patched** (stock v4.0.4 crash-loops on S3 — see
`firmware/README.md`), config loads clean, board at `sharkcnc.local`.
Known rework on the built board: R40 (ZDIR series) was fabbed 1 k, fixed
to 100R in the schematic — swap the part on the board (1 k gives the
FMD2740C opto only ~3 mA).

**Devkit:** single 44-pin symbol + the rev C `ESP32_S3_DevKit_44_Socket`
footprint (copied into `fluidnc-bob-revD.pretty/`, socketed via two
PPTC221LFBN-RC strips). DigiKey-stocked spare: ESP32-S3-DEVKITC-1-N8
(identical pin legend; see BOM.md).

**Note:** the 74AHCT541 channel assignment differs from rev C (channels
follow devkit pad order so the schematic bus is crossing-free). This is
NOT internal to the board: it swapped the X and Z step/dir GPIOs and the
AUX/spindle GPIOs relative to rev C. The as-built map is in the table
below and in this directory's `config.yaml` (verified against both the
schematic netlist and the fabbed PCB pad nets).

## What changed vs rev C (and why)

1. **External 5 V logic supply into J1** (a separate regulated wall-wart,
   ~1 A) → fuse → TVS → +5 V rail. The rev D onboard 36 V→5 V buck
   (TSR 1-4850WI) was dropped: it was the priciest part (~$26 CAD) with no
   cheap 1 A / wide-input SIP equivalent, and the 36 V motor bus is
   transient-hostile (stepper regen) for a buck input. The steppers still
   run from the 36 V PSU directly, off-board. Bring-up fact that still
   applies: the devkit does NOT bridge USB power to its 5 V pin, so the
   board rail must always be supplied.
2. **Fuse (2 A) + SMBJ5.0A TVS** on the 5 V input. The TVS clamps
   overvoltage and, on a reverse-wired input, forward-conducts to blow the
   fuse before anything downstream sees reverse polarity (order spares).
   USB and board 5 V simultaneously is safe on this devkit (isolated).
3. **Single 5 V source** — the rev D `5V-SRC` jumper (JP1) and bench
   `5V-EXT` terminal (J12) are removed; with one external feed there is
   nothing to select. (Its footprint is free for a future ideal-diode
   P-FET if reverse protection without blowing F1 is ever wanted.)
4. **SN74AHCT541N at a full 5 V** — TTL V_IH (2 V) accepts the ESP's
   3.3 V directly; the rev C GS1A VCC-drop hack is gone. Step/dir
   outputs get **100 Ω series resistors**: ~9 mA per driver opto
   (mid-spec) and caps the buffer package current at ~54 mA (< 75 mA
   abs max) — rev C could exceed the rating with all channels lit.
5. **Driver ENABLE finally wired.** Per-axis terminal carries +5 V (EN+)
   and a shared EN− sink (Q2, driven by GPIO9). Red LED2 lights when
   motors are disabled. FluidNC: `shared_stepper_disable_pin: gpio.9`.
6. **E-stop input on GPIO8**, conditioned like the limits (10 k pullup,
   1 k series, 100 nF). Wire an NC switch to GND. FluidNC v4:
   `control: estop_pin: gpio.8` — leave it commented out until J9 is
   wired (open input reads "pressed" → critical alarm at boot).
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
9. **GPIO map mostly carried over from the fabbed rev C board**, but the
   crossing-free 74AHCT541 channel ordering (see note above) swapped
   X↔Z step/dir and AUX↔spindle. Limits, probe, and SD are unchanged.

## GPIO map (ESP32-S3, as-built — verified against PCB pad nets)

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| X step / dir | 7 / 6 | | X / Y / Z limit | 42 / 2 / 1 |
| Y step / dir | 16 / 15 | | Probe | 41 |
| Z step / dir | 18 / 17 | | **E-stop (new)** | **8** |
| Spindle relay | 4 | | SD CS / MOSI / SCK / MISO | 10 / 11 / 12 / 13 |
| Aux out | 5 | | **Stepper disable (new)** | **9** |

## FluidNC config

The full working config is `config.yaml` in this directory (flashed and
verified on the built board). Update it there and push to the board with
`curl -T config.yaml http://sharkcnc.local/flash/config.yaml`, then
`$Bye` to reload — serial XModem is unreliable on v4.0.4 (task_wdt log
spam corrupts the stream; large transfers crash the firmware).

## Connector wiring

| Ref | Type | Pins | Goes to |
|---|---|---|---|
| J1 | screw 2-pos | +5V, GND | external regulated 5 V supply (fused, TVS-protected) |
| J2/J3/J4 | screw 5-pos | STEP, DIR, GND, +5V, EN− | FMD2740C: SP+, DIR+, (SP−+DIR− bridge), EN+, EN− |
| J5/J6/J7 | Molex KK-254 2 | SIG, GND | X / Y / Z limit switch (closes to GND) |
| J8 | Molex KK-254 2 | SIG, GND | probe |
| J9 | Molex KK-254 2 | SIG, GND | E-stop (NC to GND) |
| J10 | screw 3-pos | NO, COM, NC (COM center) | spindle AC switch: line hot → COM, spindle hot → NO; NC unused (planned: 2-pos COM+NO next rev) |
| J11 | Molex KK-254 2 | AUX, GND | spare buffered 5 V output (future spindle PWM) |
| J13 | header 2×6 | 5=3V3, 7=MISO, 8=MOSI, 9=SCK, 10=CS, 11/12=GND | salvaged microSD module — **same pad map as fabbed rev C** |

Driver interface is the bench-proven common-cathode sourcing: STEP→SP+,
DIR→DIR+, GND→SP−&DIR− bridge; step/dir stay **active high** in config
(same as rev C). EN unwired ⇒ enabled, so a rev C-style 3-wire hookup
still works before the EN lines are connected.

## ⚠ Layout checklist

1. The devkit socket footprint is the rev C one that already fits the
   fabbed board — no row-spacing gamble this time. Still check the
   devkit's printed pin legend against `DEVKIT_LEFT/RIGHT` in
   `generate/design.py` (pin 1 = 3V3, antenna end).
2. 5 V input nets (J1, F1, D1): keep F1 in series on J1.1 and the D1 TVS
   right at the connector; short/wide traces into the +5 V pour.
3. Local decoupling: C3/C4/C5 close to their loads (U1 VCC + devkit 5 V);
   keep the J1 input a few cm from the ESP antenna end.
4. Single-point ground at the supply; motor power never crosses the
   logic ground pour (same rule as rev C).
5. LED2/EN block near the driver terminals so the "MOTORS OFF" light is
   visible at the machine.
6. H1–H4 = M3 mounting holes, put one near each corner.
7. Power input: silk J1 clearly as **+5V / GND** — it is now a 5 V
   terminal, not 36 V. A reversed or over-voltage supply is caught only by
   F1 + D1, so keep that pair tight to the connector.
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
