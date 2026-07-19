// In-process grbl/FluidNC simulator. Speaks enough of the protocol to
// develop and test the entire sender without hardware: ok/error acks with
// a real 127-byte RX buffer, status reports, homing, jogging, alarms and
// G38.2 probing against a configurable analytic surface.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include "../transport/transport.h"

namespace scnc {

class Simulator {
public:
    struct Config {
        double maxRate = 3000;        // mm/min, used to time Run state
        bool instantMoves = true;     // true: moves complete immediately
        // Surface probed by G38.2 (returns Z at x,y). Defaults to a gentle
        // bowl in the constructor if left empty.
        std::function<double(double, double)> surface;
    };

    Simulator() : Simulator(Config{}) {}
    explicit Simulator(Config cfg);
    ~Simulator();

    // Endpoint the driver-under-test should connect() to.
    std::unique_ptr<Transport> takeClientEnd();

    // Introspection for tests
    double x() const { return x_; }
    double y() const { return y_; }
    double z() const { return z_; }
    int linesExecuted() const { return linesExecuted_; }

private:
    void loop();
    void execLine(const std::string& line);
    std::string statusReport();

    Config cfg_;
    std::unique_ptr<PipeTransport> serverEnd_;
    std::unique_ptr<PipeTransport> clientEnd_;
    std::thread th_;
    std::atomic<bool> stop_{false};

    std::atomic<double> x_{0}, y_{0}, z_{0};
    std::atomic<int> linesExecuted_{0};
    bool alarm_ = false;
    bool hold_ = false;
    double feed_ = 0;
    bool relative_ = false;
};

}  // namespace scnc
