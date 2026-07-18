# fluidnc-bob rev C BOM — everything from Drew's bins

| Qty | Ref | Part | Notes |
|---|---|---|---|
| 1 | A1 | ESP32-S3-WROOM-1 N16R8 devkit, 44-pin dual USB-C | on hand |
| 2 | (A1) | 1×22 female header, 2.54 mm | cut from stock female headers |
| 1 | U1 | SN74HC541N, DIP-20 | on hand (×3); VCC fed via D3 |
| 1 | (U1) | DIP-20 socket | optional, if in the bin |
| 3 | D1, D2, D3 | MCC GS1A, DO-214AC (SMA) | flyback / reverse crowbar / VCC drop |
| 9 | R10–R17, R41 | 100 kΩ 0805 | boot-time pulldowns + gate bleed |
| 4 | R20–R23 | 10 kΩ 0805 | input pullups |
| 6 | R30–R33, R40, R50 | 1 kΩ 0805 | series / gate / LED |
| 6 | C2, C3, C20–C23 | 100 nF 0805 | |
| 1 | C1 | 100 µF ≥16 V electrolytic THT, 2.5 mm | on hand |
| 1 | Q1 | 2N7000 TO-92 | relay driver (≤200 mA coil) |
| 1 | F1 | 1–2 A fast fuse (multimeter spare, pigtailed onto wire pads) | crowbar D2 blows it on reverse polarity |
| 1 | LED1 | 5 mm LED | power indicator |
| — | J1–J11 | solder wire pads (on the PCB) | no connectors: wires solder in |
| 1 | — | microSD breakout (74HC4050 + 3.3 V reg type) | on hand; wires to J9, feed 5 V |
| 4 | H1–H4 | M3 screw + standoff | |

Only possible purchase: a DIP-20 socket (optional).
