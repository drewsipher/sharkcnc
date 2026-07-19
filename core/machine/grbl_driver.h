// grbl 1.1 / FluidNC protocol driver: owns a reader thread, parses status
// reports, streams jobs with character-counting flow control, and exposes
// everything through thread-safe calls + callbacks (invoked on the reader
// thread; UI layers marshal to their own thread).
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../transport/transport.h"

namespace scnc {

enum class MachineState {
    Disconnected, Unknown, Idle, Run, Hold, Jog, Alarm, Door, Check,
    Home, Sleep
};

const char* toString(MachineState s);

struct Status {
    MachineState state = MachineState::Unknown;
    double mx = 0, my = 0, mz = 0;   // machine position
    double wx = 0, wy = 0, wz = 0;   // work position (MPos - WCO)
    double feed = 0, speed = 0;
    int plannerFree = -1, rxFree = -1;
    std::string pins;                // Pn: field if present
    bool hasWco = false;
    double wcoX = 0, wcoY = 0, wcoZ = 0;
};

struct ProbeResult {
    bool success = false;
    double x = 0, y = 0, z = 0;
};

struct JobProgress {
    size_t sentLines = 0, ackedLines = 0, totalLines = 0;
    bool running = false, paused = false;
};

class GrblDriver {
public:
    struct Callbacks {
        std::function<void(const Status&)> onStatus;
        std::function<void(const std::string&)> onLine;      // every rx line
        std::function<void(const std::string&)> onSent;      // every tx line
        std::function<void(int)> onAlarm;
        std::function<void(int, const std::string&)> onError; // error:N + line
        std::function<void(const ProbeResult&)> onProbe;
        std::function<void(const JobProgress&)> onJobProgress;
        std::function<void()> onDisconnected;
    };

    explicit GrblDriver(Callbacks cb);
    ~GrblDriver();

    // Takes ownership of the transport; starts reader + status polling.
    bool connect(std::unique_ptr<Transport> t);
    void disconnect();
    bool isConnected() const { return connected_; }
    std::string transportName() const;

    Status lastStatus() const;

    // --- immediate commands -------------------------------------------
    void requestStatus();                  // '?'
    void feedHold();                       // '!'
    void resume();                         // '~'
    void softReset();                      // 0x18: abort everything
    void jogCancel();                      // 0x85
    void unlock();                         // $X
    void home();                           // $H
    // Queue one line (job must not be running). cb: called with error code
    // (0 = ok) when acknowledged.
    void sendCommand(const std::string& line,
                     std::function<void(int)> ack = nullptr);
    // $J= jog. axes like "X10.5 Y-2", feed mm/min.
    void jog(const std::string& axes, double feed);

    // --- job streaming -------------------------------------------------
    bool startJob(std::vector<std::string> lines);
    void pauseJob();    // feed hold + stop filling the window
    void resumeJob();
    void stopJob();     // feed hold, then soft reset + clear queue
    JobProgress jobProgress() const;

    // --- probing -------------------------------------------------------
    // Sends a G38.2 (or given command); result arrives via onProbe.
    void probe(const std::string& cmd);

    static constexpr int kRxBuffer = 127;  // safe window for grbl + FluidNC

private:
    void readerLoop();
    void handleLine(const std::string& line);
    void pumpJob();     // fill the char-count window from the job queue
    void writeLine(const std::string& line);
    void writeRealtime(uint8_t b);
    static Status parseStatus(const std::string& line, const Status& prev);

    Callbacks cb_;
    std::unique_ptr<Transport> tp_;
    std::thread reader_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopping_{false};

    mutable std::mutex m_;
    Status status_;
    std::vector<std::string> job_;
    size_t jobSent_ = 0, jobAcked_ = 0;
    bool jobRunning_ = false, jobPaused_ = false;
    std::vector<size_t> window_;   // byte length of each in-flight line
    std::vector<std::function<void(int)>> pendingAcks_;
    std::chrono::steady_clock::time_point lastPoll_{};
};

}  // namespace scnc
