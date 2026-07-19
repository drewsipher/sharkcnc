# SharkCNC

Open CNC control stack for a Sherline 2000 mill running FluidNC on an
ESP32-S3, with a PCB-milling-first workflow. Light enough for old laptops,
robust enough to trust over WiFi, and a CAM path that respects your time.

```
core/       C++20 library, no Qt: gcode parser, height-map warp,
            grbl/FluidNC driver + simulator, Gerber/Excellon CAM
app/        Qt6 desktop sender: connect, jog, DRO, console, 2D preview,
            Z touch-off, height-map autolevel, gerber/drill import
tools/      sharkcam  - CLI PCB CAM (gerber -> isolation, excellon -> drill)
            scnc-send - headless streamer (TCP or serial)
            scnc-sim  - fake FluidNC on a TCP port for development
hardware/   fluidnc-bob: the ESP32-S3 breakout board (KiCad)
tests/      28 unit + integration tests incl. driver<->simulator E2E
```

## Build

```sh
cmake --preset release        # add -DCMAKE_PREFIX_PATH=~/Qt/6.x/gcc_64 if
cmake --build build           # Qt lives outside the system paths
ctest --test-dir build
```

Qt 6 (Widgets) is only needed for the desktop app; `core/` and `tools/`
build without it.

## Try it with zero hardware

```sh
./build/tools/scnc-sim 2323 &
./build/app/sharkcnc --tcp 127.0.0.1:2323
```

The simulator speaks real grbl protocol - status reports, ok/error flow
control, jogging, homing, even `G38.2` probing against a fake warped
surface, so the whole autolevel workflow can be exercised on the couch.

## PCB workflow

1. Export a copper Gerber + Excellon drill from KiCad.
2. `File > Open Gerber` - set tool/passes/depth - toolpaths appear
   instantly; or headless: `sharkcam iso front.gbr -o iso.nc --tool 0.2
   --passes 2`.
3. Zero XY at the board corner, `Probe > Z touch-off` on the surface.
4. `Probe > Height map` - probes a serpentine grid over the board and
   rewrites the G-code so cuts follow the real surface.
5. Run. Drill with `sharkcam drill board.drl -o drill.nc` (single-tool
   mode by default: every hole in one continuous job).

## Streaming robustness

Motion is buffered inside FluidNC; the sender uses character-counting
flow control so the controller's planner never starves on a busy line.
A network hiccup pauses the queue - it cannot corrupt a running move.
Jobs can also be run from the controller's SD card, with SharkCNC only
supervising.

Status: M1 (sender) + first slices of M3 (probing) and M4 (PCB CAM) of
[PLAN.md](PLAN.md). GPLv3-adjacent licensing TBD before v0.2.
