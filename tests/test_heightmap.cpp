#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

#include "gcode/warp.h"
#include "heightmap/heightmap.h"

using namespace scnc;
using Catch::Approx;

TEST_CASE("bilinear interpolation") {
    HeightMap hm(0, 0, 10, 10, 2, 2);
    hm.set(0, 0, 0.0);
    hm.set(1, 0, 1.0);
    hm.set(0, 1, 0.0);
    hm.set(1, 1, 1.0);
    CHECK(hm.complete());
    CHECK(hm.interpolate(0, 0) == Approx(0));
    CHECK(hm.interpolate(10, 10) == Approx(1));
    CHECK(hm.interpolate(5, 5) == Approx(0.5));
    // clamped outside
    CHECK(hm.interpolate(-5, 5) == Approx(0));
    CHECK(hm.interpolate(50, 5) == Approx(1));
}

TEST_CASE("json round trip") {
    HeightMap hm(1, 2, 3, 4, 3, 2);
    int k = 0;
    for (int iy = 0; iy < 2; ++iy)
        for (int ix = 0; ix < 3; ++ix) hm.set(ix, iy, 0.1 * k++);
    HeightMap back;
    REQUIRE(HeightMap::fromJson(hm.toJson(), back));
    CHECK(back.nx() == 3);
    CHECK(back.interpolate(1 + 3, 2) == Approx(hm.interpolate(4, 2)));
}

TEST_CASE("serpentine probe order minimises travel") {
    HeightMap hm(0, 0, 5, 5, 3, 2);
    auto pts = hm.probeOrder();
    REQUIRE(pts.size() == 6);
    CHECK(pts[2].ix == 2);
    CHECK(pts[3].ix == 2);  // second row starts where first ended
    CHECK(pts[3].iy == 1);
}

TEST_CASE("warp splits and offsets cutting moves") {
    HeightMap hm(0, 0, 10, 10, 2, 2);
    hm.set(0, 0, 0.1);
    hm.set(1, 0, 0.1);
    hm.set(0, 1, 0.1);
    hm.set(1, 1, 0.1);
    auto prog = parseGcode("G21 G90\nG0 X0 Y0 Z1\nG1 Z-0.05 F30\nG1 X10 F100\n");
    auto r = warpGcode(prog, hm, {});
    REQUIRE(r.ok);
    // all cutting Z should be -0.05 + 0.1 = 0.05
    CHECK(r.gcode.find("Z0.05") != std::string::npos);
    // the 10mm move must be split into multiple G1s
    size_t count = 0, pos = 0;
    while ((pos = r.gcode.find("G1 X", pos)) != std::string::npos) {
        ++count;
        pos += 4;
    }
    CHECK(count >= 5);
}

TEST_CASE("warp refuses relative programs") {
    HeightMap hm(0, 0, 10, 10, 2, 2);
    auto prog = parseGcode("G91\nG1 X5 F10\n");
    auto r = warpGcode(prog, hm, {});
    CHECK_FALSE(r.ok);
}
