#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

#include "cam/excellon.h"
#include "cam/gerber.h"
#include "cam/isolation.h"
#include "gcode/parser.h"

using namespace scnc;
using Catch::Approx;

namespace {
// Minimal hand-written RS-274X: two round pads joined by a trace + a region.
const char* kSimpleGerber = R"(%FSLAX46Y46*%
%MOMM*%
%ADD10C,1.600000*%
%ADD11C,0.250000*%
G04 pads*
D10*
X0Y0D03*
X10000000Y0D03*
G04 trace*
D11*
X0Y0D02*
X10000000Y0D01*
G04 region*
G36*
X0Y5000000D02*
X2000000Y5000000D01*
X2000000Y7000000D01*
X0Y7000000D01*
X0Y5000000D01*
G37*
M02*
)";

// KiCad-style rounded-rectangle pad via aperture macro
const char* kMacroGerber = R"(%FSLAX46Y46*%
%MOMM*%
%AMRoundRect*
0 Rectangle with rounded corners*
0 $1 Rounding radius*
$9=$1x2*
21,1,$2-$9,$3-$9,0,0,0*
21,1,$2,$3-$9,0,0,0*
21,1,$2-$9,$3,0,0,0*
1,1,$9,$2/2-$1,$3/2-$1*
1,1,$9,-$2/2+$1,$3/2-$1*
1,1,$9,$2/2-$1,-$3/2+$1*
1,1,$9,-$2/2+$1,-$3/2+$1*%
%ADD12RoundRect,0.250000X2.000000X1.500000*%
D12*
X0Y0D03*
M02*
)";

const char* kExcellon = R"(M48
; DRILL file
METRIC
T1C1.000
T2C1.300
%
G90
G05
T1
X10.0Y5.0
X12.5Y5.0
T2
X20.0Y10.0
T0
M30
)";
}  // namespace

TEST_CASE("gerber: pads, trace and region union") {
    auto r = parseGerber(kSimpleGerber);
    REQUIRE(r.ok);
    CHECK(r.layer.flashes == 2);
    CHECK(r.layer.strokes == 1);
    CHECK(r.layer.regions == 1);
    REQUIRE_FALSE(r.layer.copper.empty());
    // bounds: pads at x 0 and 10 with r=0.8 -> [-0.8, 10.8]
    CHECK(r.layer.minX == Approx(-0.8).margin(0.02));
    CHECK(r.layer.maxX == Approx(10.8).margin(0.02));
    // pads+trace merged into ONE polygon, region separate
    CHECK(r.layer.copper.size() == 2);
}

TEST_CASE("gerber: aperture macro roundrect flash") {
    auto r = parseGerber(kMacroGerber);
    REQUIRE(r.ok);
    CHECK(r.layer.flashes == 1);
    REQUIRE_FALSE(r.layer.copper.empty());
    // 2.0 x 1.5 pad centred at origin
    CHECK(r.layer.minX == Approx(-1.0).margin(0.03));
    CHECK(r.layer.maxX == Approx(1.0).margin(0.03));
    CHECK(r.layer.minY == Approx(-0.75).margin(0.03));
    CHECK(r.layer.maxY == Approx(0.75).margin(0.03));
}

TEST_CASE("isolation: paths clear the copper by the tool radius") {
    auto r = parseGerber(kSimpleGerber);
    REQUIRE(r.ok);
    IsolationOptions opt;
    opt.toolDiameter = 0.2;
    opt.passes = 2;
    auto iso = isolationRoute(r.layer, opt);
    REQUIRE(iso.ok);
    CHECK(iso.toolpaths.size() >= 3);  // 2 outlines x 2 passes (some merge)
    CHECK(iso.lengthMm > 20);

    // no toolpath vertex may lie inside the copper
    for (auto& path : iso.toolpaths)
        for (auto& pt : path) {
            auto inside = Clipper2Lib::PointInPolygon(
                pt, r.layer.copper.front());
            CHECK(inside != Clipper2Lib::PointInPolygonResult::IsInside);
        }

    // generated gcode parses and stays at cut depth only while cutting
    auto prog = parseGcode(iso.gcode);
    REQUIRE(prog.hasBounds());
    CHECK(prog.min.z == Approx(opt.cutZ));
    CHECK(prog.max.z == Approx(opt.travelZ));
}

TEST_CASE("excellon parse and single-tool drill job") {
    auto d = parseExcellon(kExcellon);
    REQUIRE(d.ok);
    REQUIRE(d.tools.size() == 2);
    CHECK(d.tools[0].diameter == Approx(1.0));
    CHECK(d.hits.size() == 3);

    auto g = drillGcode(d, {});
    REQUIRE(g.ok);
    CHECK(g.holes == 3);
    CHECK(g.gcode.find("M0") == std::string::npos);  // no toolchange pauses

    DrillOptions two;
    two.singleTool = false;
    auto g2 = drillGcode(d, two);
    REQUIRE(g2.ok);
    CHECK(g2.gcode.find("M0") != std::string::npos);
}

TEST_CASE("drill gcode reaches every hole") {
    auto d = parseExcellon(kExcellon);
    auto g = drillGcode(d, {});
    auto prog = parseGcode(g.gcode);
    int plunges = 0;
    for (auto& s : prog.segments)
        if (s.type == MotionType::Feed && s.to.z < 0) ++plunges;
    CHECK(plunges == 3);
}

// --- real KiCad output fixtures ----------------------------------------
#include <fstream>
#include <sstream>

namespace {
std::string readFixture(const char* name) {
    std::ifstream f(std::string(FIXTURE_DIR) + "/" + name);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace

TEST_CASE("real KiCad gerber parses with macro apertures") {
    auto r = parseGerber(readFixture("kicad_f_cu.gbr"));
    REQUIRE(r.ok);
    CHECK(r.layer.flashes == 12);
    CHECK(r.layer.strokes == 3);
    REQUIRE_FALSE(r.layer.copper.empty());
    // isolation on it must produce non-trivial toolpaths
    IsolationOptions opt;
    auto iso = isolationRoute(r.layer, opt);
    REQUIRE(iso.ok);
    CHECK(iso.lengthMm > 50);
}

TEST_CASE("real KiCad excellon parses") {
    auto d = parseExcellon(readFixture("kicad_pth.drl"));
    REQUIRE(d.ok);
    CHECK(d.tools.size() == 2);
    CHECK(d.hits.size() == 5);
    auto g = drillGcode(d, {});
    REQUIRE(g.ok);
    CHECK(g.holes == 5);
}

// --- tool library -------------------------------------------------------
#include "cam/tool.h"

TEST_CASE("v-bit isolation width grows with depth") {
    Tool v;
    v.type = ToolType::VBit;
    v.diameter = 3.175;
    v.tipAngle = 30;  // included; half = 15deg
    // width = 2 * depth * tan(15deg); at 0.1mm depth ~ 0.0536mm
    CHECK(v.widthAtDepth(0.1) == Approx(2 * 0.1 * std::tan(15 * M_PI / 180)).margin(1e-4));
    // deeper cut => wider, capped at diameter
    CHECK(v.widthAtDepth(100) == Approx(3.175));
    CHECK(v.widthAtDepth(0.0) == Approx(0.0));
}

TEST_CASE("flat endmill width is constant") {
    Tool e;
    e.type = ToolType::EndMill;
    e.diameter = 1.0;
    CHECK(e.widthAtDepth(0.05) == Approx(1.0));
    CHECK(e.widthAtDepth(5.0) == Approx(1.0));
}

TEST_CASE("ball nose width follows the sphere") {
    Tool b;
    b.type = ToolType::BallNose;
    b.diameter = 2.0;  // r=1
    CHECK(b.widthAtDepth(1.0) == Approx(2.0));       // full radius -> full dia
    CHECK(b.widthAtDepth(0.5) == Approx(2 * std::sqrt(1 - 0.25)).margin(1e-6));
}

TEST_CASE("tool library json round trip and lookup") {
    auto lib = ToolLibrary::defaults();
    REQUIRE(lib.tools().size() == 4);
    int id = lib.tools()[0].id;
    REQUIRE(lib.find(id) != nullptr);
    auto back = ToolLibrary::fromJson(lib.toJson());
    REQUIRE(back.tools().size() == 4);
    CHECK(back.tools()[0].name == lib.tools()[0].name);
    CHECK(back.tools()[0].type == lib.tools()[0].type);
    CHECK(back.find(id) != nullptr);
    // adding after load gets a fresh, non-colliding id
    int nid = back.add(Tool{});
    for (const auto& t : back.tools())
        CHECK((t.id == nid) == (&t == &back.tools().back()));
}

TEST_CASE("tool library remove") {
    auto lib = ToolLibrary::defaults();
    int id = lib.tools()[1].id;
    CHECK(lib.remove(id));
    CHECK(lib.find(id) == nullptr);
    CHECK(lib.tools().size() == 3);
    CHECK_FALSE(lib.remove(99999));
}

// --- facing / flattening ------------------------------------------------
#include "cam/facing.h"

TEST_CASE("facing covers the area at the right depth") {
    FacingOptions o;
    o.width = 40; o.height = 20;
    o.toolDiameter = 6; o.stepover = 0.5;
    o.totalDepth = 0.6; o.depthPerPass = 0.2;  // 3 passes
    auto r = facingRoutine(o);
    REQUIRE(r.ok);
    CHECK(r.passes == 3);
    auto prog = parseGcode(r.gcode);
    REQUIRE(prog.hasBounds());
    CHECK(prog.min.z == Approx(-0.6));
    // stays within the area (tool centre inside by the radius)
    CHECK(prog.min.x >= Approx(o.x0).margin(0.01));
    CHECK(prog.max.x <= Approx(o.x0 + o.width).margin(0.01));
    CHECK(prog.min.y >= Approx(o.y0).margin(0.01));
    CHECK(prog.max.y <= Approx(o.y0 + o.height).margin(0.01));
    CHECK(r.lengthMm > o.width);  // multiple rows
}

TEST_CASE("facing spiral option produces a path") {
    FacingOptions o;
    o.width = 30; o.height = 30; o.toolDiameter = 6; o.spiral = true;
    auto r = facingRoutine(o);
    REQUIRE(r.ok);
    CHECK(r.toolpaths.size() == 1);
    CHECK(r.toolpaths[0].size() > 4);
}

TEST_CASE("facing rejects nonsense") {
    FacingOptions o; o.width = 0;
    CHECK_FALSE(facingRoutine(o).ok);
    o = FacingOptions{}; o.toolDiameter = 0;
    CHECK_FALSE(facingRoutine(o).ok);
}

// --- board outline with tabs --------------------------------------------
#include "cam/outline.h"

TEST_CASE("outline cuts through with tab lifts") {
    auto rect = rectBoundary(0, 0, 30, 20);
    OutlineOptions o;
    o.toolDiameter = 1.0;
    o.cutZ = -1.8; o.depthPerPass = 0.6;  // 3 passes
    o.tabs = 4; o.tabWidth = 3; o.tabHeight = 0.5;
    auto r = outlineRoutine(rect, o);
    REQUIRE(r.ok);
    CHECK(r.passes == 3);
    auto prog = parseGcode(r.gcode);
    REQUIRE(prog.hasBounds());
    // deepest Z reaches the full cut depth somewhere
    CHECK(prog.min.z == Approx(-1.8));
    // the tool cuts OUTSIDE the board: min X below 0 by ~radius
    CHECK(prog.min.x < 0);
    CHECK(prog.max.x > 30);
    // tabs mean some cutting points sit at the tab-top Z (-1.3) on the
    // deepest pass -> that Z value appears in the program
    CHECK(r.gcode.find("Z-1.3") != std::string::npos);
}

TEST_CASE("outline with no tabs is a clean loop") {
    auto rect = rectBoundary(0, 0, 20, 20);
    OutlineOptions o;
    o.tabs = 0; o.cutZ = -1.0; o.depthPerPass = 1.0;
    auto r = outlineRoutine(rect, o);
    REQUIRE(r.ok);
    CHECK(r.gcode.find("Z-1.3") == std::string::npos);
    CHECK(r.toolpaths.size() == 1);
}

TEST_CASE("isolation fills small pad/via holes but keeps big voids") {
    // a 2mm pad with a 0.8mm drill hole
    const char* padWithHole = R"(%FSLAX46Y46*%
%MOMM*%
%ADD10C,2.000000X0.800000*%
D10*
X0Y0D03*
M02*
)";
    auto g = parseGerber(padWithHole);
    REQUIRE(g.ok);
    IsolationOptions o;
    o.toolDiameter = 0.2;
    o.passes = 1;

    o.fillHolesBelow = 2.0;  // fill the 0.8mm hole
    auto filled = isolationRoute(g.layer, o);
    REQUIRE(filled.ok);
    CHECK(filled.toolpaths.size() == 1);  // only the outer boundary

    o.fillHolesBelow = 0.0;  // isolate the hole too (old behaviour)
    auto notFilled = isolationRoute(g.layer, o);
    REQUIRE(notFilled.ok);
    CHECK(notFilled.toolpaths.size() == 2);  // outer + inner ring
}

TEST_CASE("isolation merges a pad and its trace into one outline") {
    // two pads joined by a trace, same net -> one connected region
    const char* padTracePad = R"(%FSLAX46Y46*%
%MOMM*%
%ADD10C,2.000000*%
%ADD11C,0.400000*%
D10*
X0Y0D03*
X8000000Y0D03*
D11*
X0Y0D02*
X8000000Y0D01*
M02*
)";
    auto g = parseGerber(padTracePad);
    REQUIRE(g.ok);
    IsolationOptions o;
    o.toolDiameter = 0.2;
    o.passes = 1;
    auto iso = isolationRoute(g.layer, o);
    REQUIRE(iso.ok);
    // pad+trace+pad is one region -> a single outer isolation loop
    CHECK(iso.toolpaths.size() == 1);
}

TEST_CASE("isolation keeps a small clearance that contains an isolated pad") {
    // a ground pour with a small clearance gap around an isolated pad on a
    // different net. The clearance is small (<fillHolesBelow) but must be
    // KEPT because it surrounds copper - filling it would short the pad to
    // the pour. (The old area/size-only filter got this wrong.)
    GerberLayer L;
    L.copper.push_back({{0, 0}, {20, 0}, {20, 20}, {0, 20}});     // pour (CCW)
    // clearance gap 2.2mm across, wound as a hole (CW). Smaller than the
    // 2.5mm fill threshold, so a naive size filter would delete it.
    L.copper.push_back({{8.9, 8.9}, {8.9, 11.1}, {11.1, 11.1}, {11.1, 8.9}});
    L.copper.push_back({{9.4, 9.4}, {10.6, 9.4}, {10.6, 10.6},
                        {9.4, 10.6}});                            // isolated pad
    L.minX = 0; L.minY = 0; L.maxX = 20; L.maxY = 20;

    IsolationOptions o;
    o.toolDiameter = 0.2;
    o.passes = 1;
    o.fillHolesBelow = 2.5;  // > the clearance size, but it has a pad inside
    auto iso = isolationRoute(L, o);
    REQUIRE(iso.ok);
    // pour outer + clearance + pad => 3 loops; the pad must still be isolated
    CHECK(iso.toolpaths.size() == 3);
    bool padIsolated = false;
    for (const auto& path : iso.toolpaths)
        for (const auto& pt : path)
            if (std::abs(pt.x - 10) < 2 && std::abs(pt.y - 10) < 2)
                padIsolated = true;
    CHECK(padIsolated);
}

TEST_CASE("real poured board: pads stay solid (no cross-category voids)") {
    // Regression for the keyhole bug: flashes and regions came out with
    // opposite windings, so a pad drawn as both a flash and covered by the
    // pour region cancelled into a void. Parse the real board and check a
    // known pad centre is solid copper with no tiny hole punched in it.
    auto text = readFixture("board_pour_F_Cu.gbr");
    auto g = parseGerber(text);
    REQUIRE(g.ok);
    // The bug punched keyhole voids into pads, which isolation then traced as
    // extra inner loops. Count small interior voids (the drill/via holes are
    // legitimately a handful; the bug added ~150 pad voids) - there should be
    // very few, and isolation shouldn't explode into spurious loops.
    int smallVoids = 0;
    for (const auto& p : g.layer.copper)
        if (Clipper2Lib::Area(p) < 0) {
            auto b = Clipper2Lib::GetBounds(Clipper2Lib::PathsD{p});
            if (std::max(b.Width(), b.Height()) < 2.0) ++smallVoids;
        }
    // ~47 legit drill holes (44-pin socket etc.); the bug added ~150 more
    CHECK(smallVoids < 80);

    IsolationOptions o;
    o.toolDiameter = 0.2;
    auto iso = isolationRoute(g.layer, o);
    REQUIRE(iso.ok);
    CHECK(iso.toolpaths.size() < 160);  // was 264+ with the void bug
}
