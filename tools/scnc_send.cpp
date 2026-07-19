// scnc-send: headless streamer. Sends a G-code file to a FluidNC/grbl
// controller over TCP or serial with live progress.
//   scnc-send tcp <host> <port> file.nc
//   scnc-send serial </dev/ttyACM0> <baud> file.nc
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#include "machine/grbl_driver.h"

using namespace scnc;
using namespace std::chrono_literals;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::puts("usage: scnc-send tcp <host> <port> file.nc\n"
                  "       scnc-send serial <device> <baud> file.nc");
        return 2;
    }
    std::string mode = argv[1];
    std::ifstream f(argv[4]);
    if (!f.good()) {
        std::fprintf(stderr, "cannot read %s\n", argv[4]);
        return 1;
    }
    std::vector<std::string> lines;
    std::string l;
    while (std::getline(f, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
    }

    std::atomic<bool> done{false}, failed{false};
    GrblDriver::Callbacks cb;
    cb.onJobProgress = [&](const JobProgress& p) {
        std::printf("\r%zu/%zu lines  ", p.ackedLines, p.totalLines);
        std::fflush(stdout);
        if (!p.running && p.ackedLines == p.totalLines) done = true;
    };
    cb.onError = [&](int code, const std::string& line) {
        std::fprintf(stderr, "\ncontroller %s (code %d)\n", line.c_str(), code);
    };
    cb.onAlarm = [&](int code) {
        std::fprintf(stderr, "\nALARM:%d - stopping\n", code);
        failed = true;
    };
    cb.onDisconnected = [&] {
        std::fprintf(stderr, "\nconnection lost\n");
        failed = true;
    };

    GrblDriver drv(cb);
    std::unique_ptr<Transport> t;
    if (mode == "tcp")
        t = makeTcpTransport(argv[2], std::atoi(argv[3]));
    else
        t = makeSerialTransport(argv[2], std::atoi(argv[3]));
    if (!drv.connect(std::move(t))) {
        std::fprintf(stderr, "cannot connect\n");
        return 1;
    }
    std::this_thread::sleep_for(300ms);  // let banners pass

    std::printf("streaming %zu lines to %s\n", lines.size(),
                drv.transportName().c_str());
    drv.startJob(std::move(lines));
    while (!done && !failed) std::this_thread::sleep_for(50ms);
    drv.disconnect();
    std::printf("\n%s\n", failed ? "FAILED" : "done");
    return failed ? 1 : 0;
}
