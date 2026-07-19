# SharkCNC

Open CNC control stack for a Sherline 2000 mill running FluidNC on an
ESP32-S3, with a PCB-milling-first workflow. Light enough for old laptops,
robust enough to trust over WiFi, and a CAM path that respects your time.

```
core/       C++20 library, no Qt: gcode parser, height-map warp,
            grbl/FluidNC driver + simulator, and the CAM engines
            (Gerber, Excellon, isolation, facing, outline, tool model)
app/        Qt6 desktop sender: 2D + 3D views, jog/DRO/console/run,
            probing, autolevel, integrated PCB CAM, tool library
tools/      sharkcam  - CLI CAM (iso / drill / face / outline / info)
            scnc-send - headless streamer (TCP or serial)
            scnc-sim  - fake FluidNC on a TCP port for development
hardware/   fluidnc-bob: the ESP32-S3 breakout board (KiCad)
tests/      41 unit + integration tests incl. driver<->simulator E2E
```

## Architecture in one line

The **PC** runs everything heavy (parsing, CAM, 3D, probing). The **ESP32**
only runs FluidNC and turns buffered G-code into step pulses. Nothing CAM- or
CAD-related ever touches the ESP — which is why a $6 chip drives the mill.

## Build

```sh
cmake --preset release        # add -DCMAKE_PREFIX_PATH=~/Qt/6.x/gcc_64 if
cmake --build build           # Qt lives outside the system paths
ctest --test-dir build
```

Qt 6 (Widgets + OpenGLWidgets) is only needed for the desktop app; `core/`
and `tools/` build without it. If the app aborts on launch with an xcb
plugin error, install `libxcb-cursor0` (or run with
`QT_QPA_PLATFORM=wayland`).

## Try it with zero hardware

```sh
./build/tools/scnc-sim 2323 &
./build/app/sharkcnc --tcp 127.0.0.1:2323
```

The simulator speaks real grbl protocol — status, flow control, jogging,
homing, `G38.2` probing against a fake warped surface, tool-change pauses —
so the whole workflow can be exercised on the couch.

## Views

- **2D** (default): fast QPainter top-down view, ideal for PCB. Copper
  overlay under isolation toolpaths, origin marker, scale bar. Pan-drag,
  wheel-zoom, double-click / **F** to fit.
- **3D** (Tab, or View menu): OpenGL, orbit / shift-drag pan / wheel zoom,
  **orthographic ↔ perspective** toggle, Top/Front/Iso presets (7/1/0).
  Load an STL (`File > Open STL`) to place stock or a part under the paths.

## PCB workflow (all in the CAM dock)

Export copper Gerber + Excellon drill from KiCad, then in the **PCB CAM**
panel:

| Tab | Does |
|---|---|
| **Copper (isolation)** | Gerber → isolation toolpaths. Pick a tool from the library; V-bits derive their cut width from the depth automatically. Live preview over the copper. |
| **Drill** | Excellon → drill G-code. Single-tool mode = one continuous job; multi-tool mode prompts you at each tool change. |
| **Face** | Raster / spiral surface flattening of a rectangular area, multi-pass. |
| **Outline** | Board cut-out offset by the tool radius, multi-pass, with N holding tabs so the board stays put. |

Each tab regenerates instantly as you change parameters, then **Load into
sender** and Run. Or headless:

```sh
sharkcam iso     front.gbr -o iso.nc  --tool 0.2 --passes 2
sharkcam drill   board.drl -o drill.nc
sharkcam face    -o face.nc    --w 60 --h 40 --tool 6 --depth 0.3
sharkcam outline -o cut.nc     --w 60 --h 40 --tabs 4
```

## Probing & autolevel

- **Z touch-off** (Probe menu): probe the surface, set Z zero with an
  optional plate-thickness offset.
- **Height map** (Probe menu): probe a serpentine grid over the board, then
  warp the loaded G-code so cuts follow the real (non-flat) surface — the
  thing that makes single-sided isolation reliable on warped FR4.

## Tool library

`Tools > Tool library`. Define end mills, ball noses, V-bits, chamfers,
drills — length, diameters, flutes, tip angle, corner radius, feeds/speeds.
The V-bit **cut-width-at-depth** number (shown live) is what isolation uses,
so choosing the tool + depth sets the trace clearance correctly. Persists to
the app config dir; the isolation tab picks from it.

## Streaming robustness

Motion is buffered inside FluidNC; the sender uses character-counting flow
control so the planner never starves. A network hiccup pauses the queue — it
cannot corrupt a running move. Tool changes (M0/M6) are handled host-side:
the sender drains motion, prompts you, and resumes on your click. Jobs can
also run from the controller's SD card with SharkCNC only supervising.

Status: M1 (sender) complete; M3 (probing/autolevel), M4 (PCB CAM: iso +
drill + outline), and M5 (facing, tool library) delivered. 3D view + STL
import done; STEP import and voxel cut-simulation are the next 3D items.
See [PLAN.md](PLAN.md). GPLv3-adjacent licensing TBD before v0.2.
