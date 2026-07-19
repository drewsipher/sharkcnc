#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

#include "gcode/parser.h"

using namespace scnc;
using Catch::Approx;

TEST_CASE("parse basic moves") {
    auto p = parseGcode("G21 G90\nG0 X10 Y5\nG1 X20 Y5 F100\n");
    REQUIRE(p.segments.size() == 2);
    CHECK(p.segments[0].type == MotionType::Rapid);
    CHECK(p.segments[0].to.x == Approx(10));
    CHECK(p.segments[1].type == MotionType::Feed);
    CHECK(p.segments[1].feed == Approx(100));
    CHECK(p.segments[1].from.x == Approx(10));
    CHECK(p.max.x == Approx(20));
}

TEST_CASE("modal motion carries to following lines") {
    auto p = parseGcode("G1 X1 F50\nX2\nY3\n");
    REQUIRE(p.segments.size() == 3);
    CHECK(p.segments[2].type == MotionType::Feed);
    CHECK(p.segments[2].to.x == Approx(2));
    CHECK(p.segments[2].to.y == Approx(3));
}

TEST_CASE("inches convert to mm") {
    auto p = parseGcode("G20\nG0 X1\n");
    REQUIRE(p.segments.size() == 1);
    CHECK(p.segments[0].to.x == Approx(25.4));
    CHECK(p.sawInches);
}

TEST_CASE("relative moves accumulate") {
    auto p = parseGcode("G91\nG0 X5\nG0 X5\n");
    REQUIRE(p.segments.size() == 2);
    CHECK(p.segments[1].to.x == Approx(10));
    CHECK(p.sawRelative);
}

TEST_CASE("comments and blank lines ignored") {
    auto p = parseGcode("; hello\n(comment) G0 X1\n\nG1 X2 F10 ; end\n");
    REQUIRE(p.segments.size() == 2);
}

TEST_CASE("arc tessellation endpoints and radius") {
    // quarter circle CCW from (10,0) to (0,10) centred at origin
    auto p = parseGcode("G0 X10 Y0\nG3 X0 Y10 I-10 J0 F60\n");
    REQUIRE(p.segments.size() == 2);
    auto pts = tessellateArc(p.segments[1], 0.01);
    REQUIRE(pts.size() > 5);
    CHECK(pts.back().x == Approx(0).margin(1e-9));
    CHECK(pts.back().y == Approx(10).margin(1e-9));
    for (auto& q : pts) {
        CHECK(std::hypot(q.x, q.y) == Approx(10).margin(0.02));
    }
}

TEST_CASE("probe motion recognised") {
    auto p = parseGcode("G38.2 Z-5 F30\n");
    REQUIRE(p.segments.size() == 1);
    CHECK(p.segments[0].type == MotionType::Probe);
}

TEST_CASE("splitLinear covers the move") {
    auto pts = splitLinear({0, 0, 0}, {10, 0, -1}, 2.0);
    REQUIRE(pts.size() == 5);
    CHECK(pts.back().x == Approx(10));
    CHECK(pts.back().z == Approx(-1));
    CHECK(pts[0].x == Approx(2));
}

TEST_CASE("fmtNum trims zeros") {
    CHECK(scnc::fmtNum(1.5) == "1.5");
    CHECK(scnc::fmtNum(2.0) == "2");
    CHECK(scnc::fmtNum(-0.0001) == "-0.0001");
    CHECK(scnc::fmtNum(0.12345) == "0.1235");  // 4 decimals
}
