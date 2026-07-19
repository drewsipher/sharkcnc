// Byte transports the machine driver talks through. Implementations:
// TCP (FluidNC telnet), POSIX serial (USB-CDC), and in-process loopback
// used by the simulator and tests.
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace scnc {

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    // Blocking write of the whole buffer; false on failure.
    virtual bool write(const uint8_t* data, size_t n) = 0;
    bool write(const std::string& s) {
        return write(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
    // Read up to n bytes; blocks up to timeout. Returns bytes read,
    // 0 on timeout, -1 on error/closed.
    virtual int read(uint8_t* buf, size_t n,
                     std::chrono::milliseconds timeout) = 0;
    virtual std::string describe() const = 0;
};

// Thread-safe in-memory pipe endpoint pair; used to wire a driver to the
// in-process simulator (or tests).
class PipeTransport : public Transport {
public:
    struct Shared {
        std::mutex m;
        std::condition_variable cv;
        std::deque<uint8_t> aToB, bToA;
        bool closed = false;
    };

    PipeTransport(std::shared_ptr<Shared> sh, bool isA)
        : sh_(std::move(sh)), isA_(isA) {}

    using Transport::write;  // keep the std::string convenience overload

    static std::pair<std::unique_ptr<PipeTransport>,
                     std::unique_ptr<PipeTransport>>
    makePair() {
        auto sh = std::make_shared<Shared>();
        return {std::make_unique<PipeTransport>(sh, true),
                std::make_unique<PipeTransport>(sh, false)};
    }

    bool open() override { return true; }
    void close() override {
        std::lock_guard lk(sh_->m);
        sh_->closed = true;
        sh_->cv.notify_all();
    }
    bool isOpen() const override { return !sh_->closed; }

    bool write(const uint8_t* data, size_t n) override {
        std::lock_guard lk(sh_->m);
        if (sh_->closed) return false;
        auto& q = isA_ ? sh_->aToB : sh_->bToA;
        q.insert(q.end(), data, data + n);
        sh_->cv.notify_all();
        return true;
    }

    int read(uint8_t* buf, size_t n,
             std::chrono::milliseconds timeout) override {
        std::unique_lock lk(sh_->m);
        auto& q = isA_ ? sh_->bToA : sh_->aToB;
        if (!sh_->cv.wait_for(lk, timeout,
                              [&] { return !q.empty() || sh_->closed; }))
            return 0;
        if (q.empty()) return sh_->closed ? -1 : 0;
        size_t k = std::min(n, q.size());
        for (size_t i = 0; i < k; ++i) {
            buf[i] = q.front();
            q.pop_front();
        }
        return static_cast<int>(k);
    }
    std::string describe() const override { return "pipe"; }

private:
    std::shared_ptr<Shared> sh_;
    bool isA_;
};

// Factories (implemented in tcp_transport.cpp / serial_transport.cpp)
std::unique_ptr<Transport> makeTcpTransport(const std::string& host, int port);
std::unique_ptr<Transport> makeSerialTransport(const std::string& device,
                                               int baud);

}  // namespace scnc
