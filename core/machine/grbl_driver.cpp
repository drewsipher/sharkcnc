#include "grbl_driver.h"

#include <cstring>
#include <numeric>

namespace scnc {

const char* toString(MachineState s) {
    switch (s) {
        case MachineState::Disconnected: return "Disconnected";
        case MachineState::Unknown: return "Unknown";
        case MachineState::Idle: return "Idle";
        case MachineState::Run: return "Run";
        case MachineState::Hold: return "Hold";
        case MachineState::Jog: return "Jog";
        case MachineState::Alarm: return "Alarm";
        case MachineState::Door: return "Door";
        case MachineState::Check: return "Check";
        case MachineState::Home: return "Home";
        case MachineState::Sleep: return "Sleep";
    }
    return "?";
}

namespace {
MachineState stateFromName(std::string_view n) {
    if (n.starts_with("Idle")) return MachineState::Idle;
    if (n.starts_with("Run")) return MachineState::Run;
    if (n.starts_with("Hold")) return MachineState::Hold;
    if (n.starts_with("Jog")) return MachineState::Jog;
    if (n.starts_with("Alarm")) return MachineState::Alarm;
    if (n.starts_with("Door")) return MachineState::Door;
    if (n.starts_with("Check")) return MachineState::Check;
    if (n.starts_with("Home")) return MachineState::Home;
    if (n.starts_with("Sleep")) return MachineState::Sleep;
    return MachineState::Unknown;
}

bool parse3(std::string_view v, double& a, double& b, double& c) {
    char buf[96];
    if (v.size() >= sizeof buf) return false;
    std::memcpy(buf, v.data(), v.size());
    buf[v.size()] = 0;
    return std::sscanf(buf, "%lf,%lf,%lf", &a, &b, &c) == 3;
}
}  // namespace

GrblDriver::GrblDriver(Callbacks cb) : cb_(std::move(cb)) {}

GrblDriver::~GrblDriver() { disconnect(); }

bool GrblDriver::connect(std::unique_ptr<Transport> t) {
    disconnect();
    if (!t || !t->open()) return false;
    tp_ = std::move(t);
    stopping_ = false;
    connected_ = true;
    reader_ = std::thread([this] { readerLoop(); });
    // wake FluidNC/grbl and get an initial picture
    writeRealtime('\n');
    requestStatus();
    return true;
}

void GrblDriver::disconnect() {
    stopping_ = true;
    if (tp_) tp_->close();
    if (reader_.joinable()) reader_.join();
    tp_.reset();
    if (connected_.exchange(false)) {
        std::lock_guard lk(m_);
        jobRunning_ = false;
        window_.clear();
        pendingAcks_.clear();
    }
}

std::string GrblDriver::transportName() const {
    return tp_ ? tp_->describe() : "";
}

Status GrblDriver::lastStatus() const {
    std::lock_guard lk(m_);
    return status_;
}

void GrblDriver::requestStatus() { writeRealtime('?'); }
void GrblDriver::feedHold() { writeRealtime('!'); }
void GrblDriver::resume() { writeRealtime('~'); }
void GrblDriver::jogCancel() { writeRealtime(0x85); }

void GrblDriver::softReset() {
    {
        std::lock_guard lk(m_);
        jobRunning_ = false;
        jobPaused_ = false;
        job_.clear();
        jobSent_ = jobAcked_ = 0;
        window_.clear();
        pendingAcks_.clear();
    }
    writeRealtime(0x18);
    // let the UI reset its Run/Hold/Stop state after an abort
    if (cb_.onJobProgress) cb_.onJobProgress({0, 0, 0, false, false});
}

void GrblDriver::unlock() { sendCommand("$X"); }
void GrblDriver::home() { sendCommand("$H"); }

void GrblDriver::sendCommand(const std::string& line,
                             std::function<void(int)> ack) {
    std::lock_guard lk(m_);
    pendingAcks_.push_back(std::move(ack));
    window_.push_back(line.size() + 1);
    writeLine(line);
}

void GrblDriver::jog(const std::string& axes, double feed) {
    char buf[128];
    std::snprintf(buf, sizeof buf, "$J=G91 G21 %s F%.1f", axes.c_str(), feed);
    sendCommand(buf);
}

void GrblDriver::probe(const std::string& cmd) { sendCommand(cmd); }

bool GrblDriver::startJob(std::vector<std::string> lines) {
    std::lock_guard lk(m_);
    if (jobRunning_ || !connected_) return false;
    // drop trailing blank lines so completion lands cleanly at 100%
    while (!lines.empty() &&
           lines.back().find_first_not_of(" \t\r\n") == std::string::npos)
        lines.pop_back();
    if (lines.empty()) return false;
    job_ = std::move(lines);
    jobSent_ = jobAcked_ = 0;
    jobRunning_ = true;
    jobPaused_ = false;
    pumpJob();
    return true;
}

void GrblDriver::pauseJob() {
    {
        std::lock_guard lk(m_);
        jobPaused_ = true;
    }
    feedHold();
}

void GrblDriver::resumeJob() {
    {
        std::lock_guard lk(m_);
        jobPaused_ = false;
        pumpJob();
    }
    resume();
}

void GrblDriver::stopJob() {
    feedHold();
    softReset();
}

JobProgress GrblDriver::jobProgress() const {
    std::lock_guard lk(m_);
    return {jobSent_, jobAcked_, job_.size(), jobRunning_, jobPaused_};
}

// ---- internals ---------------------------------------------------------

void GrblDriver::writeLine(const std::string& line) {
    if (!tp_) return;
    std::string s = line;
    s.push_back('\n');
    tp_->write(s);
    if (cb_.onSent) cb_.onSent(line);
}

void GrblDriver::writeRealtime(uint8_t b) {
    if (tp_) tp_->write(&b, 1);
}

void GrblDriver::pumpJob() {
    // caller holds m_
    if (!jobRunning_ || jobPaused_) return;
    size_t used = std::accumulate(window_.begin(), window_.end(), size_t{0});
    while (jobSent_ < job_.size()) {
        const std::string& L = job_[jobSent_];
        size_t need = L.size() + 1;
        if (need > kRxBuffer) {  // pathological line: send alone
            if (!window_.empty()) break;
        } else if (used + need > kRxBuffer) {
            break;
        }
        window_.push_back(need);
        pendingAcks_.push_back(nullptr);
        used += need;
        ++jobSent_;
        writeLine(L);
    }
}

void GrblDriver::readerLoop() {
    std::string acc;
    uint8_t buf[512];
    auto lastStatusPoll = std::chrono::steady_clock::now();
    while (!stopping_) {
        int r = tp_->read(buf, sizeof buf, std::chrono::milliseconds(50));
        if (r < 0) break;
        for (int i = 0; i < r; ++i) {
            char c = static_cast<char>(buf[i]);
            if (c == '\n' || c == '\r') {
                if (!acc.empty()) {
                    handleLine(acc);
                    acc.clear();
                }
            } else {
                acc.push_back(c);
            }
        }
        auto now = std::chrono::steady_clock::now();
        if (connected_ && now - lastStatusPoll > std::chrono::milliseconds(200)) {
            lastStatusPoll = now;
            requestStatus();
        }
    }
    connected_ = false;
    if (!stopping_ && cb_.onDisconnected) cb_.onDisconnected();
}

void GrblDriver::handleLine(const std::string& line) {
    if (cb_.onLine) cb_.onLine(line);

    if (line[0] == '<') {  // status report
        Status st;
        {
            std::lock_guard lk(m_);
            status_ = parseStatus(line, status_);
            st = status_;
        }
        // callback outside the lock: it may re-enter driver methods
        if (cb_.onStatus) cb_.onStatus(st);
        return;
    }

    if (line == "ok" || line.starts_with("error:")) {
        int code = 0;
        if (line.starts_with("error:")) code = std::atoi(line.c_str() + 6);
        std::function<void(int)> ack;
        JobProgress prog;
        bool notifyProg = false;
        {
            std::lock_guard lk(m_);
            if (!window_.empty()) window_.erase(window_.begin());
            if (!pendingAcks_.empty()) {
                ack = std::move(pendingAcks_.front());
                pendingAcks_.erase(pendingAcks_.begin());
            }
            if (jobRunning_) {
                if (jobAcked_ < jobSent_) ++jobAcked_;
                if (jobAcked_ >= job_.size()) {
                    jobRunning_ = false;
                }
                pumpJob();
                prog = {jobSent_, jobAcked_, job_.size(), jobRunning_,
                        jobPaused_};
                notifyProg = true;
            }
        }
        if (ack) ack(code);
        if (code && cb_.onError) cb_.onError(code, line);
        if (notifyProg && cb_.onJobProgress) cb_.onJobProgress(prog);
        return;
    }

    if (line.starts_with("ALARM:")) {
        int code = std::atoi(line.c_str() + 6);
        {
            std::lock_guard lk(m_);
            status_.state = MachineState::Alarm;
        }
        if (cb_.onAlarm) cb_.onAlarm(code);
        return;
    }

    if (line.starts_with("[PRB:")) {
        // [PRB:x,y,z:1] (grbl reports more axes on some builds; take 3)
        ProbeResult pr;
        double a = 0, b = 0, c = 0;
        auto colon = line.rfind(':');
        auto body = line.substr(5, line.find(']') - 5);
        auto lastColon = body.rfind(':');
        if (lastColon != std::string::npos) {
            pr.success = body.substr(lastColon + 1).starts_with("1");
            body = body.substr(0, lastColon);
        }
        if (parse3(body, a, b, c)) {
            pr.x = a; pr.y = b; pr.z = c;
        }
        static_cast<void>(colon);
        if (cb_.onProbe) cb_.onProbe(pr);
        return;
    }
    // banners, [MSG:...], $ dumps: surfaced via onLine already
}

Status GrblDriver::parseStatus(const std::string& line, const Status& prev) {
    Status st = prev;
    // <State|Field:...|Field:...>
    std::string body = line.substr(1, line.rfind('>') - 1);
    size_t p = 0;
    bool first = true;
    bool sawMpos = false, sawWpos = false;
    while (p <= body.size()) {
        size_t bar = body.find('|', p);
        std::string tok = body.substr(
            p, bar == std::string::npos ? std::string::npos : bar - p);
        if (first) {
            st.state = stateFromName(tok);
            first = false;
        } else if (tok.starts_with("MPos:")) {
            sawMpos = parse3(tok.substr(5), st.mx, st.my, st.mz);
        } else if (tok.starts_with("WPos:")) {
            sawWpos = parse3(tok.substr(5), st.wx, st.wy, st.wz);
        } else if (tok.starts_with("WCO:")) {
            st.hasWco = parse3(tok.substr(4), st.wcoX, st.wcoY, st.wcoZ);
        } else if (tok.starts_with("FS:")) {
            std::sscanf(tok.c_str() + 3, "%lf,%lf", &st.feed, &st.speed);
        } else if (tok.starts_with("F:")) {
            st.feed = std::atof(tok.c_str() + 2);
        } else if (tok.starts_with("Bf:")) {
            std::sscanf(tok.c_str() + 3, "%d,%d", &st.plannerFree, &st.rxFree);
        } else if (tok.starts_with("Pn:")) {
            st.pins = tok.substr(3);
        }
        if (bar == std::string::npos) break;
        p = bar + 1;
    }
    if (line.find("Pn:") == std::string::npos) st.pins.clear();
    if (sawMpos && st.hasWco) {
        st.wx = st.mx - st.wcoX;
        st.wy = st.my - st.wcoY;
        st.wz = st.mz - st.wcoZ;
    } else if (sawWpos && st.hasWco) {
        st.mx = st.wx + st.wcoX;
        st.my = st.wy + st.wcoY;
        st.mz = st.wz + st.wcoZ;
    } else if (sawMpos && !st.hasWco) {
        st.wx = st.mx; st.wy = st.my; st.wz = st.mz;
    }
    return st;
}

}  // namespace scnc
