// Driver <-> simulator integration: the tests that make hardware optional.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

#include <atomic>
#include <chrono>
#include <thread>

#include "gcode/warp.h"
#include "heightmap/heightmap.h"
#include "machine/grbl_driver.h"
#include "machine/simulator.h"

using namespace scnc;
using namespace std::chrono_literals;
using Catch::Approx;

namespace {
bool waitFor(std::function<bool()> pred, std::chrono::milliseconds ms = 3000ms) {
    auto end = std::chrono::steady_clock::now() + ms;
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return pred();
}
}  // namespace

TEST_CASE("driver connects and receives status") {
    Simulator sim;
    std::atomic<int> statusCount{0};
    GrblDriver::Callbacks cb;
    cb.onStatus = [&](const Status&) { ++statusCount; };
    GrblDriver drv(cb);
    REQUIRE(drv.connect(sim.takeClientEnd()));
    REQUIRE(waitFor([&] { return statusCount > 0; }));
    CHECK(drv.lastStatus().state == MachineState::Idle);
    drv.disconnect();
}

TEST_CASE("command ack round trip") {
    Simulator sim;
    GrblDriver drv({});
    REQUIRE(drv.connect(sim.takeClientEnd()));
    std::atomic<int> code{-1};
    drv.sendCommand("G21", [&](int c) { code = c; });
    REQUIRE(waitFor([&] { return code >= 0; }));
    CHECK(code == 0);
    drv.disconnect();
}

TEST_CASE("job streams entirely and reports progress") {
    Simulator sim;
    std::atomic<size_t> acked{0};
    std::atomic<bool> done{false};
    GrblDriver::Callbacks cb;
    cb.onJobProgress = [&](const JobProgress& p) {
        acked = p.ackedLines;
        if (!p.running && p.ackedLines == p.totalLines) done = true;
    };
    GrblDriver drv(cb);
    REQUIRE(drv.connect(sim.takeClientEnd()));

    std::vector<std::string> lines;
    for (int i = 0; i < 200; ++i)
        lines.push_back("G1 X" + std::to_string(i % 50) + " F1000");
    REQUIRE(drv.startJob(lines));
    REQUIRE(waitFor([&] { return done.load(); }, 10000ms));
    CHECK(acked == 200);
    CHECK(sim.linesExecuted() >= 200);
    drv.disconnect();
}

TEST_CASE("jog moves the simulated machine") {
    Simulator sim;
    GrblDriver drv({});
    REQUIRE(drv.connect(sim.takeClientEnd()));
    drv.jog("X5", 1000);
    REQUIRE(waitFor([&] { return sim.x() == Approx(5.0).margin(1e-9); }));
    drv.jog("X5", 1000);
    REQUIRE(waitFor([&] { return sim.x() == Approx(10.0).margin(1e-9); }));
    drv.disconnect();
}

TEST_CASE("probe returns surface height") {
    Simulator::Config cfg;
    cfg.surface = [](double, double) { return -0.42; };
    Simulator sim(cfg);
    std::atomic<bool> got{false};
    ProbeResult pr;
    GrblDriver::Callbacks cb;
    cb.onProbe = [&](const ProbeResult& r) {
        pr = r;
        got = true;
    };
    GrblDriver drv(cb);
    REQUIRE(drv.connect(sim.takeClientEnd()));
    drv.probe("G38.2 Z-5 F60");
    REQUIRE(waitFor([&] { return got.load(); }));
    CHECK(pr.success);
    CHECK(pr.z == Approx(-0.42).margin(1e-6));
    drv.disconnect();
}

TEST_CASE("end to end: probe grid, build map, warp follows surface") {
    // simulated warped board: tilted plane
    Simulator::Config cfg;
    cfg.surface = [](double x, double y) { return 0.01 * x - 0.005 * y; };
    Simulator sim(cfg);

    std::atomic<bool> got{false};
    ProbeResult last;
    GrblDriver::Callbacks cb;
    cb.onProbe = [&](const ProbeResult& r) {
        last = r;
        got = true;
    };
    GrblDriver drv(cb);
    REQUIRE(drv.connect(sim.takeClientEnd()));

    HeightMap hm(0, 0, 10, 10, 3, 3);
    for (auto& pt : hm.probeOrder()) {
        drv.sendCommand("G0 X" + fmtNum(pt.x) + " Y" + fmtNum(pt.y) + " Z1");
        got = false;
        drv.probe("G38.2 Z-5 F60");
        REQUIRE(waitFor([&] { return got.load(); }));
        REQUIRE(last.success);
        hm.set(pt.ix, pt.iy, last.z);
        drv.sendCommand("G0 Z1");
    }
    REQUIRE(hm.complete());
    // map should reproduce the analytic surface
    CHECK(hm.interpolate(20, 0) == Approx(0.2).margin(1e-3));
    CHECK(hm.interpolate(0, 20) == Approx(-0.1).margin(1e-3));

    // warp a trace and confirm the Z at the far end tracks the tilt
    auto prog = parseGcode("G21 G90\nG0 X0 Y0 Z1\nG1 Z-0.05 F30\nG1 X20 Y0 F100\n");
    auto w = warpGcode(prog, hm, {});
    REQUIRE(w.ok);
    CHECK(w.gcode.find("X20 Y0 Z0.15") != std::string::npos);
    drv.disconnect();
}

TEST_CASE("soft reset clears a running job") {
    Simulator sim;
    GrblDriver drv({});
    REQUIRE(drv.connect(sim.takeClientEnd()));
    std::vector<std::string> lines(500, "G1 X1 F1000");
    REQUIRE(drv.startJob(lines));
    drv.stopJob();
    auto p = drv.jobProgress();
    CHECK_FALSE(p.running);
    drv.disconnect();
}

TEST_CASE("job with trailing blank lines completes at 100%") {
    Simulator sim;
    std::atomic<bool> done{false};
    std::atomic<int> acked{0}, total{0};
    GrblDriver::Callbacks cb;
    cb.onJobProgress = [&](const JobProgress& p) {
        acked = static_cast<int>(p.ackedLines);
        total = static_cast<int>(p.totalLines);
        if (!p.running && p.totalLines > 0 && p.ackedLines == p.totalLines)
            done = true;
    };
    GrblDriver drv(cb);
    REQUIRE(drv.connect(sim.takeClientEnd()));
    // mimic a CAM file: real lines then trailing blanks (from split on '\n')
    std::vector<std::string> lines = {"G21", "G90", "G0 X1", "M5", "M2", "", ""};
    REQUIRE(drv.startJob(lines));
    REQUIRE(waitFor([&] { return done.load(); }, 5000ms));
    CHECK(acked == total);
    CHECK(total == 5);  // trailing blanks stripped
    drv.disconnect();
}

TEST_CASE("stop re-enables run via job-progress reset") {
    Simulator sim;
    bool sawReset = false;
    GrblDriver::Callbacks cb;
    cb.onJobProgress = [&](const JobProgress& p) {
        if (!p.running && p.totalLines == 0) sawReset = true;
    };
    GrblDriver drv(cb);
    REQUIRE(drv.connect(sim.takeClientEnd()));
    std::vector<std::string> lines(300, "G1 X1 F1000");
    REQUIRE(drv.startJob(lines));
    drv.stopJob();
    REQUIRE(waitFor([&] { return sawReset; }));
    CHECK(drv.startJob({"G0 X0"}));  // can run again immediately
    drv.disconnect();
}

TEST_CASE("multi-tool job pauses and prompts at M0, resumes to completion") {
    Simulator sim;
    std::atomic<bool> prompted{false}, done{false};
    std::string msg;
    std::atomic<int> acked{0}, total{0};
    GrblDriver::Callbacks cb;
    cb.onToolChange = [&](const std::string& m) { msg = m; prompted = true; };
    cb.onJobProgress = [&](const JobProgress& p) {
        acked = int(p.ackedLines); total = int(p.totalLines);
        if (!p.running && p.totalLines > 0 && p.ackedLines == p.totalLines)
            done = true;
    };
    GrblDriver drv(cb);
    REQUIRE(drv.connect(sim.takeClientEnd()));
    std::vector<std::string> job = {
        "G0 X1", "G1 Z-1 F100", "G0 Z2",
        "M0 ; change to 1.0mm drill",
        "G0 X5", "G1 Z-1 F100", "G0 Z2"};
    REQUIRE(drv.startJob(job));
    REQUIRE(waitFor([&] { return prompted.load(); }));
    CHECK(msg.find("1.0mm drill") != std::string::npos);
    // job is paused at the tool change, not finished
    CHECK_FALSE(done.load());
    // operator inserts tool and resumes
    drv.resumeJob();
    REQUIRE(waitFor([&] { return done.load(); }, 5000ms));
    CHECK(acked == total);
    drv.disconnect();
}
