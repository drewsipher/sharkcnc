#include "simulator.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace scnc {

Simulator::Simulator(Config cfg) : cfg_(std::move(cfg)) {
    if (!cfg_.surface)
        cfg_.surface = [](double x, double y) {
            return -0.05 + 0.001 * x + 0.0008 * y +
                   0.00002 * (x * x + y * y);
        };
    auto [a, b] = PipeTransport::makePair();
    serverEnd_ = std::move(a);
    clientEnd_ = std::move(b);
    th_ = std::thread([this] { loop(); });
}

Simulator::~Simulator() {
    stop_ = true;
    serverEnd_->close();
    if (th_.joinable()) th_.join();
}

std::unique_ptr<Transport> Simulator::takeClientEnd() {
    return std::move(clientEnd_);
}

std::string Simulator::statusReport() {
    char buf[160];
    const char* state = alarm_ ? "Alarm" : (hold_ ? "Hold:0" : "Idle");
    std::snprintf(buf, sizeof buf,
                  "<%s|MPos:%.3f,%.3f,%.3f|FS:%.0f,0|Bf:15,127|WCO:0.000,0.000,0.000>",
                  state, x_.load(), y_.load(), z_.load(), feed_);
    return buf;
}

void Simulator::loop() {
    std::string acc;
    uint8_t buf[256];
    serverEnd_->write(std::string("\r\nGrbl 1.1 ['$' for help]\r\n"));
    while (!stop_) {
        int r = serverEnd_->read(buf, sizeof buf, std::chrono::milliseconds(50));
        if (r < 0) break;
        for (int i = 0; i < r; ++i) {
            uint8_t c = buf[i];
            switch (c) {
                case '?':
                    serverEnd_->write(statusReport() + "\r\n");
                    continue;
                case '!': hold_ = true; continue;
                case '~': hold_ = false; continue;
                case 0x18:  // soft reset
                    acc.clear();
                    hold_ = false;
                    serverEnd_->write(
                        std::string("\r\nGrbl 1.1 ['$' for help]\r\n"));
                    continue;
                case 0x85: continue;  // jog cancel: nothing queued to cancel
                default: break;
            }
            if (c == '\r') continue;  // ignore CR; '\n' terminates a line
            if (c == '\n') {
                execLine(acc);  // real grbl acks blank lines too
                acc.clear();
            } else {
                acc.push_back(static_cast<char>(c));
            }
        }
    }
}

void Simulator::execLine(const std::string& raw) {
    ++linesExecuted_;
    std::string line;
    for (char c : raw)
        if (!std::isspace(static_cast<unsigned char>(c)))
            line.push_back(
                static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

    auto ok = [&] { serverEnd_->write(std::string("ok\r\n")); };
    auto err = [&](int n) {
        serverEnd_->write("error:" + std::to_string(n) + "\r\n");
    };

    if (line.empty()) { ok(); return; }

    if (line[0] == '$') {
        if (line == "$X") { alarm_ = false; ok(); return; }
        if (line == "$H") { x_ = 0; y_ = 0; z_ = 0; ok(); return; }
        if (line.starts_with("$J=")) {
            if (alarm_) { err(9); return; }
            // parse G91-style jog: axes words after $J=
            std::string body = line.substr(3);
            bool rel = body.find("G91") != std::string::npos;
            double nx = x_, ny = y_, nz = z_;
            auto grab = [&](char L, double cur) {
                auto p = body.find(L);
                if (p == std::string::npos) return cur;
                // skip G-words like G91/G21 (their digits follow immediately
                // and are integers, but G is a different letter so fine)
                double v = std::atof(body.c_str() + p + 1);
                return rel ? cur + v : v;
            };
            nx = grab('X', nx);
            ny = grab('Y', ny);
            nz = grab('Z', nz);
            x_ = nx; y_ = ny; z_ = nz;
            ok();
            return;
        }
        ok();  // other $ commands: accepted silently
        return;
    }

    if (alarm_) { err(9); return; }

    // minimal G-code executor: G0/G1 moves, G38.2 probe, modal G90/91
    if (line.find("G91") != std::string::npos) relative_ = true;
    if (line.find("G90") != std::string::npos) relative_ = false;

    bool probe = line.find("G38.2") != std::string::npos ||
                 line.find("G38.3") != std::string::npos;

    auto axis = [&](char L, bool& has) -> double {
        // find L not preceded by 'G' digit context
        for (size_t p = 0; p < line.size(); ++p) {
            if (line[p] == L &&
                (p == 0 || !std::isalpha(static_cast<unsigned char>(line[p]))
                     || true)) {
                // ensure previous char isn't part of a word like "G38"
                if (p > 0 && line[p - 1] >= '0' && line[p - 1] <= '9' &&
                    L == 'X' ) {
                    // fine: numbers before X are a previous word's value
                }
                has = true;
                return std::atof(line.c_str() + p + 1);
            }
        }
        has = false;
        return 0;
    };

    bool hx = false, hy = false, hz = false, hf = false;
    double vx = axis('X', hx), vy = axis('Y', hy), vz = axis('Z', hz);
    double vf = axis('F', hf);
    if (hf) feed_ = vf;

    double nx = hx ? (relative_ ? x_ + vx : vx) : x_.load();
    double ny = hy ? (relative_ ? y_ + vy : vy) : y_.load();
    double nz = hz ? (relative_ ? z_ + vz : vz) : z_.load();

    if (probe) {
        double zs = cfg_.surface(nx, ny);
        x_ = nx; y_ = ny;
        if (nz <= zs) {
            z_ = zs;
            char buf[96];
            std::snprintf(buf, sizeof buf, "[PRB:%.3f,%.3f,%.3f:1]\r\n",
                          x_.load(), y_.load(), z_.load());
            serverEnd_->write(std::string(buf));
            ok();
        } else {
            z_ = nz;
            serverEnd_->write(std::string("[PRB:0.000,0.000,0.000:0]\r\n"));
            err(5);  // probe fail-ish (grbl uses ALARM:4/5; error keeps it simple)
        }
        return;
    }

    x_ = nx; y_ = ny; z_ = nz;
    ok();
}

}  // namespace scnc
