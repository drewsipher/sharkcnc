# SharkCNC — Session Handoff / Orientation

Read this first if you're a new contributor (human or LLM) picking up the
project. It captures what SharkCNC is, how it's built, the non-obvious
decisions, the gotchas that cost real debugging time, and what's left to do.
For the product vision and milestone plan see [PLAN.md](../PLAN.md); for user-
facing usage see [README.md](../README.md).

---

## 1. What this is, in one paragraph

An open, Linux-first CNC control stack for **Drew's Sherline 2000 mill**, with
a **PCB-milling-first** feature set. It replaces the proprietary PlanetCNC
software (Windows-only, license-locked) that came with the retired UCNCV4
controller. Two halves: **firmware** = FluidNC on an ESP32-S3 (motion only),
and **host software** = `sharkcnc`, a native C++/Qt6 desktop app (everything
else — G-code, CAM, 3D view, probing). Hardware bridge is a custom KiCad
breakout (`hardware/fluidnc-bob/`) that Drew is having fabbed at JLCPCB.

**The load-bearing architectural fact:** the ESP does *only* real-time motion
(step pulses via its RMT hardware peripheral). All CAM, parsing, 3D, and
simulation run on the PC. This is why a ~$6 chip can drive a mill the
proprietary board charged hundreds for. Never move heavy work onto the ESP.

---

## 2. Current status (2026-07-19)

Milestone **M1 (sender) is complete**, plus large slices of **M3** (probing/
autolevel), **M4** (Gerber/Excellon CAM), and **M5** (facing, tool library)
delivered ahead of the plan. 48 tests green, CI green on `main`.

Working end-to-end **today, with zero hardware** via the built-in simulator:
connect → jog/DRO → load a gerber → isolation/drill/face/outline CAM → probe →
height-map autolevel → run job → tool-change prompts → job recovery.

**Not yet done / deferred** (see §8): the physical board bring-up (M2 — waiting
on JLCPCB, ~1 week out as of this writing), Windows packaging (M6), and the two
big 3D items — STEP import (needs OpenCASCADE) and voxel cut-simulation.

---

## 3. Repository map

```
core/         C++20 library, NO Qt. The whole engine; unit-tested headless.
  gcode/      parser.* (modal G-code -> motion model), warp.* (height-map
              warp), resume.* (build a safe resume-from-line job)
  heightmap/  heightmap.* (probe grid, bilinear interp, serpentine order, JSON)
  machine/    grbl_driver.* (grbl/FluidNC protocol, char-counting streaming,
              tool-change, job control), simulator.* (protocol-faithful fake
              controller used by tests AND the scnc-sim tool)
  transport/  transport.h (iface + in-proc PipeTransport), tcp_/serial_*
  cam/        gerber.* excellon.* isolation.* facing.* outline.* tool.*
              gcode_out.* — the CAM engines (see §6 for the gerber gotcha)
app/          Qt6 Widgets desktop app (needs Qt6 Widgets + OpenGLWidgets)
  main_window.* (the shell), gcode_view.* (2D QPainter), gcode_view3d.*
  (3D OpenGL), cam_panel.* (the 4-tab PCB CAM dock), tool_dialog.*/
  tool_library.* (tool library), probe_dialog.*, machine_client.* (Qt
  bridge: marshals driver callbacks to signals on the GUI thread)
tools/        sharkcam (CLI CAM), scnc-send (headless streamer),
              scnc-sim (TCP-exposed simulator for GUI dev)
tests/        Catch2 suites + fixtures/ (real KiCad gerber/drill, and the
              poured board board_pour_F_Cu.gbr used as a regression)
hardware/fluidnc-bob/   KiCad breakout board (generated from generate/*.py;
              see that dir's README). Drew laid out the PCB himself.
.github/workflows/ci.yml   build + test + offscreen app smoke + CAM smoke
PLAN.md       vision + milestones (kept up to date)
```

**Strict rule:** `core/` has **zero Qt**. It's a plain C++ library so it
unit-tests headlessly and a future Windows CLI/alternate UI stays cheap. All Qt
lives in `app/`. Don't leak Qt into core.

---

## 4. Build / test / run

```sh
cmake --preset release        # presets: release, debug, asan
cmake --build build
ctest --test-dir build
```

**Qt location gotcha:** Qt 6.10.3 was installed via `aqtinstall` to
`~/Qt/6.10.3/gcc_64` (the distro's system Qt is too old / lacks pieces). If
CMake can't find Qt, pass `-DCMAKE_PREFIX_PATH=~/Qt/6.10.3/gcc_64`. `core/` and
`tools/` build without Qt; only `app/` needs it.

**Running the app:**
```sh
./build/tools/scnc-sim 2323 &                       # fake FluidNC on TCP
QT_QPA_PLATFORM=wayland ./build/app/sharkcnc --tcp 127.0.0.1:2323
```
- **xcb launch crash:** the app aborts on X11/XWayland if `libxcb-cursor0` is
  missing. Fix: `sudo apt install libxcb-cursor0`, or run with
  `QT_QPA_PLATFORM=wayland`. CI installs the lib.
- **Debug CLI flags** (used for screenshot-based verification, all gated):
  `--tcp host:port`, `--cam-gerber file.gbr`, `--stl file.stl`, `--view3d`,
  `--tool-dialog`, `--screenshot out.png` (grabs after 3 s and exits — works
  under `QT_QPA_PLATFORM=offscreen`). These are how the app is regression-
  screenshotted without a human.

**CLI CAM (no GUI):**
```sh
sharkcam iso front.gbr -o iso.nc --tool 0.2 --passes 2 --fill-holes 2.5
sharkcam drill board.drl -o drill.nc         # add --multi-tool for prompts
sharkcam face -o face.nc --w 60 --h 40 --tool 6
sharkcam outline -o cut.nc --w 60 --h 40 --tabs 4
sharkcam info file.gbr|file.drl
```

---

## 5. How to *verify* changes (this project's testing culture)

Unit tests (`ctest`) cover core logic. But several classes of bug are only
visible when you *look*, so the workflow leans on rendering:

- **App UI / views:** run the app under `QT_QPA_PLATFORM=offscreen` with
  `--screenshot`, then `Read` the PNG. Offscreen has **no OpenGL** — the 3D
  view is gated off there (see §7), so to see 3D you need real GL (next point).
- **3D view (needs GL):** there's no system GL in the sandbox, so render under
  a software-GL xvfb. The recipe that works (libxcb-cursor was fetched without
  root via `apt-get download` + `dpkg-deb -x` into a temp dir):
  ```sh
  LD_LIBRARY_PATH=<xcbcursor>/usr/lib/x86_64-linux-gnu \
  LIBGL_ALWAYS_SOFTWARE=1 QT_QPA_PLATFORM=xcb \
  xvfb-run -a -s "-screen 0 1440x950x24" \
    ./build/app/sharkcnc file.nc --view3d --screenshot out.png
  ```
- **CAM geometry:** dump toolpaths/copper as text and plot with matplotlib in a
  venv (`/tmp/plt`), or — critically — **compare against a reference gerber
  renderer**. `gerbonara` (Python, `pip install gerbonara`) is the ground truth
  that caught the pad-void bug. When a CAM output looks wrong, render the same
  input with gerbonara and diff visually. Do not trust your own parser as its
  own oracle.

CI runs: build (Linux), all tests, an **offscreen app smoke test** (this is
what caught the headless-GL crash — it launches the app connected via `--tcp`),
and CAM smoke tests for all four operations.

---

## 6. Non-obvious things that cost real debugging time

Read these before touching the relevant code.

- **Gerber parser winding (the big one).** Flashes, strokes, and regions come
  out of primitive generation with *inconsistent winding between categories*.
  Unioning them all raw in one Clipper `NonZero` pass makes overlaps *between*
  categories cancel to zero — e.g. a pad drawn as a flash *and* covered by the
  pour region gets a keyhole void punched through it. Each category renders
  fine alone; only the mix breaks. **Fix in place:** `gerber.cpp` unions each
  category (`flashAcc`/`strokeAcc`/`regionAcc`) separately — Clipper normalises
  winding per union — then combines the clean results (`combineDark`). If you
  refactor the parser's accumulation, keep this separation. Verified against
  gerbonara; `board_pour_F_Cu.gbr` fixture guards it.
- **Isolation hole classification.** Drill/via holes must be filled (not
  milled) but ground-pour *clearances that surround an isolated pad* must be
  kept. Size alone can't tell them apart. `isolation.cpp` builds a Clipper
  **PolyTree** and fills only *childless* small voids (`fillHolesBelow`, UI
  "Fill holes < mm"). A clearance with a pad inside has a child → kept.
- **Simulator must ack blank lines.** Real grbl acks blank lines; the sim
  didn't, so a job's trailing blank line (from `split('\n')`) never got acked
  and jobs hung at 99%. `simulator.cpp` now acks empties; `grbl_driver.cpp`
  also strips trailing blanks and `softReset()` emits a progress reset so Stop
  re-enables Run without homing.
- **Offscreen has no OpenGL.** `QOpenGLWidget` *crashes* under
  `-platform offscreen` when repainted (surfaced only via `--tcp`, because a
  status update triggers the repaint). `main_window.cpp` probes for a real GL
  context at startup (`openglAvailable()`) and only builds the 3D view if it
  succeeds; all `view3d_` uses are null-guarded. Keep that guard.
- **3D camera is a quaternion arcball.** Not turntable. Orbit maps mouse
  positions onto a virtual sphere so rotation depends on where you grabbed
  (Blender-style). Presets set `rot_` quaternions. Don't regress to yaw/pitch.
- **2D grid is an adaptive decade grid** — a coarse level kept ≥60px plus a
  finer level that fades; never a fixed spacing (that vanished when zoomed out).
- **Tool-change is host-managed.** The sender intercepts `M0`/`M6` in the
  stream, drains motion, prompts, and resumes on click — it does *not* rely on
  the controller's M0 semantics (which vary).

---

## 7. Hardware context (for when the boards arrive)

- **Machine:** Sherline 2000, 20 TPI leadscrews → **1259.84 steps/mm** at 1/8
  microstep. FMD2740C stepper drivers (kept), long-body NEMA 23s (kept).
- **Controller:** retiring the proprietary UCNCV4 for a **44-pin dual-USB-C
  ESP32-S3-WROOM-1 (N16R8)** running FluidNC.
- **Breakout `hardware/fluidnc-bob/`:** SN74HC541N buffer (VCC dropped via a
  GS1A diode so 3.3 V logic meets V_IH), GS1A diodes, 0805 passives, MMBT2222A
  relay driver, 1210 polyfuse, solder wire pads (no connectors), microSD
  header. **GPIO map & FluidNC `config.yaml` are in that dir's README.** The
  schematic is generated from `generate/design.py`; Drew did the PCB layout.
- **Spindle:** relay on/off first (harvest a Songle relay off the UCNCV4);
  the AUX pad is wired for future isolated-PWM speed control. **Never wire
  logic to the Sherline speed pot — it's not mains-isolated.**
- **Bring-up when boards land:** measure the devkit's pin-row spacing (the
  socket footprint assumed 22.86 mm), flash the **S3 build** of FluidNC, set
  driver DIP switches to motor current + 1/8 µstep, then connect the app with
  the board's IP/serial (only the connection field changes vs the sim).

---

## 8. What's left — task list

Roughly in order.

- **M2 — Physical bring-up** (blocked on JLCPCB fab). Populate the board,
  flash FluidNC-S3, wire limits/E-stop, migrate off the UCNCV4, validate the
  full workflow on real copper. Verify the config.yaml pin map on hardware.
- **Non-rectangular board outline.** `outline.cpp` currently takes a rectangle
  (`rectBoundary`). Parse the Edge.Cuts gerber into a real boundary polygon for
  arbitrary shapes.
- **Combined PCB job.** Chain isolation → drill → outline into one job with
  tool-change prompts between ops (the pieces exist; wire them together).
- **3D: STEP import.** STL works; STEP needs OpenCASCADE — a heavy dep, gate it
  behind a CMake option. This is what "load real 3D CAD" needs.
- **3D: cut simulation.** Voxel/dexel material-removal using the tool-library
  geometry, to catch gouges/crashes before cutting. Depends on the 3D view
  (done) and tool library (done). Later: time-accurate feed/speed playback.
- **M6 — Windows build & packaging.** CI already cross-compiles the core; this
  is serial-port quirks, an installer, and testing. POSIX bits in
  `transport/` and `tools/scnc_sim.cpp` are `#ifdef`'d for this.
- **Spindle speed (phase 2).** KB-style isolator or higher-RPM ER11 spindle
  swap driven from the AUX PWM pad; FluidNC config change + an RC filter.
- **Nice-to-haves:** backlash comp, DXF import for CAM, pendant/MPG, per-op
  tool assignment surfaced in the CAM panel, thermal-relief-aware isolation.

---

## 9. Conventions

- **Commits:** Conventional-ish subject; body explains *why*. Footer lines are
  auto-appended (Co-Authored-By + Claude-Session) — keep them.
- **Branch:** work on `main` is fine for this solo project, but keep CI green;
  every push runs it. Don't merge red.
- **Drew's workflow preferences** (from the sessions): he does the PCB layout
  and final schematic cleanup himself — the software/CAM and part-selection
  reasoning are the assistant's lane. He's strong on electronics and C++,
  Linux-first, and dislikes slow/cumbersome tools (the whole point of the CAM
  UX). When a CAM result looks wrong, reproduce against a reference before
  claiming a fix — two symptom-only "fixes" shipped before the real
  winding-cancellation root cause was found.
- **Repo:** `github.com/drewsipher/sharkcnc` (public).
