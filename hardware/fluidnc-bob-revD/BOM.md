# fluidnc-bob rev D BOM

Import `digikey_bom.csv` into DigiKey's BOM Manager (myLists → upload) —
it matches on Manufacturer Part Number; the Digi-Key Part Number column is
filled where it was explicitly verified. Quantities include spares.

**Verified on DigiKey (product page confirmed, in stock as of 2026-07-27):**
buffer, fuse, socket strips, Phoenix terminal blocks. Jellybean
passives/discretes use industry-standard MPNs.

**Power:** the board runs from a **separate regulated 5 V wall-wart supply
(~1 A)** wired into J1 — NOT included in this BOM (Drew supplies it). The
rev D onboard 36 V→5 V buck (TSR 1-4850WI) was removed: it was the single
priciest part (~$26 CAD) with no cheap 1 A / wide-input equivalent. The
36 V motor PSU still feeds the stepper drivers directly, off-board.

## Semiconductors & modules

| Ref | Part | MPN | DK PN | Qty (order) | Alternatives |
|---|---|---|---|---|---|
| U1 | Octal buffer, TTL-in, DIP-20 | **SN74AHCT541N** (TI) | 296-4755-5-ND | 2 | Nexperia 74HCT541N; TI SN74HCT541N |
| Q1, Q2 | NPN, SOT-23 | **MMBT2222A-7-F** (Diodes) | — | 10 | onsemi MMBT2222ALT1G |
| D1 | TVS 5 V standoff, SMB | **SMBJ5.0A** (Littelfuse) | — | 5 | Vishay SMBJ5.0A-E3/52 — clamps the 5 V rail and crowbars a reverse-wired input (blows F1) |
| D2 | Flyback 1 A 1000 V, SMA | **S1M-13-F** (Diodes) | — | 10 | onsemi S1M; any 1N4007-class SMA |
| LED1 | 5 mm green | **WP7113GD** (Kingbright) | — | 5 | any 5 mm LED |
| LED2 | 5 mm red | **WP7113ID** (Kingbright) | — | 5 | any 5 mm LED |
| F1 | Fuse 2 A 63 V, 1206 | **0466002.NR** (Littelfuse) | F1457CT-ND | 5 | Bourns SF-1206F200-2 (verified) |
| RLY1 | Relay SPDT 10 A/250 VAC, 5 V coil | **G5LE-14 DC5** (Omron) | Z1011-ND (page verified) | 2 | TE OJ-SH-105LMH; the harvested Songle SRD-05VDC is the same class |

## Passives (0805 unless noted)

| Ref | Value | MPN | Qty (order) |
|---|---|---|---|
| R10–R17, R41, R43 | 100 kΩ | RC0805FR-07100KL (Yageo) | 20 |
| R20–R24 | 10 kΩ | RC0805FR-0710KL | 20 |
| R30–R34, R40, R42, R50, R51 | 1 kΩ | RC0805FR-071KL | 20 |
| R60–R66 | 100 Ω | RC0805FR-07100RL | 20 |
| C4, C5, C20–C24 | 100 nF 50 V X7R | CL21B104KBCNNNC (Samsung) | 20 |
| C3 | 10 µF 25 V X7R, 1206 | CL31B106KAHNNNE (Samsung) | 5 |

## Connectors

| Ref | Part | MPN | DK PN | Qty (order) |
|---|---|---|---|---|
| (A1) | Socket strip 1×22, 2.54 mm ×2 | **PPTC221LFBN-RC** (Sullins) | — (page verified) | 3 |
| J1 | Screw terminal 2-pos 5.08 (5 V in) | **1715721** (Phoenix MKDS 1,5/2-5,08) | — (page verified) | 2 |
| J10 | Screw terminal 3-pos 5.08 | **1715734** (Phoenix MKDS 1,5/3-5,08) | — (page verified) | 2 |
| J2–J4 | Screw terminal 5-pos 5.08 | **1715750** (Phoenix MKDS 1,5/5-5,08) | — (page verified) | 4 |
| J5–J9, J11 | Molex KK-254 header 2-pos, friction lock | **22-23-2021** (Molex) | — (mfr page verified) | 8 |
| — | KK-254 housing 2-pos w/ ramp | **22-01-3027** (Molex) | — (mfr page verified) | 10 |
| — | KK-254 crimp terminal (2759 series, 22–30 AWG) | 08-50-0114 (Molex) | — | 50 |
| J13 | Pin header 2×6, 2.54 mm (SD module, rev C pad map) | PRPC006DAAN-RC (Sullins) | — | 2 |
| (U1) | DIP-20 socket | — on hand (optional) | — | — |

Crimp-shy alternative for the KK inputs: pre-crimped KK/2759-series
leads (search "Molex KK 254 pre-crimped 22AWG") — the board side is
identical. J13 mates with the salvaged SD module's existing 2×6 cable
(pad map identical to the fabbed rev C board: 5=3V3, 7=MISO, 8=MOSI,
9=SCK, 10=CS, 11/12=GND, rest NC).

## Devkit (A1)

The board sockets the existing 44-pin dual-USB-C ESP32-S3 clone using the
rev C custom socket footprint (proven fit). The official Espressif board
has the identical pin legend and is DigiKey-stocked if you want a spare:
**ESP32-S3-DEVKITC-1-N8** ([product page](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-DEVKITC-1-N8/15199021))
— N8 = no PSRAM, which is fine (FluidNC doesn't need it; GPIO 35-37 are
then free). Verify its header row spacing against the socket before
soldering a board around it.

## Not ordered (on hand / off-board)

- ESP32-S3-WROOM-1 N16R8 44-pin dual-USB-C devkit (existing)
- microSD breakout module (salvaged, fed 3.3 V via J13 pin 5)
- M3 standoffs/screws (H1–H4)

## Electrical sanity notes

- Opto drive: AHCT541 at 5 V through 100 Ω into FMD2740C (~270 Ω internal
  + LED): ≈ 9 mA per input — mid-spec (5–15 mA), matches the proven rev C
  drive topology.
- Worst-case buffer package current: 6 ch × 9 mA ≈ 54 mA < 75 mA abs max.
- 5 V supply sizing: devkit WiFi bursts ≈ 350 mA + buffer 55 mA + SD
  100 mA + relay coil ≈ 80 mA + LEDs ≈ 600 mA peak, ~300 mA typical → a
  regulated **5 V / 1 A** wall-wart is comfortable (2 A gives headroom).
- Relay contacts (J10 COM/NO/NC) switch the spindle's AC line: 10 A /
  250 VAC rating vs the Sherline's ~1.5 A draw. Mains lives ONLY on the
  RLY1-contact / J10 corner — see the layout checklist.
- 5 V input protection (J1 → F1 → D1): F1 (2 A) is overcurrent; D1
  (SMBJ5.0A, 5 V standoff) clamps overvoltage AND, if the input is
  reverse-wired, forward-conducts to blow F1 before anything downstream
  sees reverse polarity — that's why F1 gets spares. (A dedicated
  ideal-diode P-FET was considered; the fuse + TVS crowbar is simpler and
  sufficient for a regulated wall-wart source — see README.)
