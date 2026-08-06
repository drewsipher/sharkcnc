# Patched FluidNC firmware for the rev D board

`fluidnc-4.0.4-patched-wifi_s3.bin` is the firmware actually flashed on the
board (2026-08-06): upstream FluidNC **v4.0.4**, `wifi_s3` target, plus the
three patches in this directory. Stock v4.0.4 is unusable on ESP32-S3:

1. **0001 — Timed-engine stale-pulse spin (the crash loop).** A soft reset
   (Ctrl-X, which most clients send on connect) reaches the Timed engine's
   `start_unstep()` with no step pulse in flight; `spinUntil()` then spins on
   a stale CCOUNT deadline for up to ~9 s and the 5 s task watchdog reboots
   the chip — ~25% odds per soft reset. S3 has no RMT engine, so every S3
   user runs Timed. Observed: 26 reboots in 8 minutes with a reconnecting
   client. Also fixes the inverted `connected()` check in
   `TelnetClient::write()` (connected clients had all output dropped).
2. **0002 — upstream PR #1748** (unmerged): WiFi AP scan from the WebUI /
   `$ESP410` starves the TCP watchdog and reboots the board.
3. **0003 — Telnet never transmits on esp32.** v4.0.4's new telnet TX queue
   drains via `WiFiClient::availableForWrite()`, which arduino-esp32 never
   implements (Print's default returns 0), so the queue never drains and
   telnet delivers nothing. Replaced with a non-blocking `::send(MSG_DONTWAIT)`.

Upstream status at time of writing: all three unfixed on `main` (9c690fb2).

## Reflash

```sh
esptool --chip esp32s3 --port /dev/ttyACM1 --baud 230400 \
  --before default-reset --after hard-reset write-flash -z \
  --flash-mode dio --flash-freq 80m --flash-size detect \
  0x10000 fluidnc-4.0.4-patched-wifi_s3.bin
```

App partition only — config.yaml and the WebUI files in littlefs survive.

## Rebuild from source

```sh
git clone --branch v4.0.4 https://github.com/bdring/FluidNC && cd FluidNC
git am path/to/000*.patch
pio run -e wifi_s3        # needs python on PATH (venv works); PlatformIO 6.x
# result: .pio/build/wifi_s3/firmware.bin
```

Before rebuilding, check whether a newer upstream release already includes
these fixes (search FluidNC PRs #1742/#1748 and the telnet/timed-engine
history) — prefer stock firmware once upstream is fixed.
