#include "transport.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

namespace scnc {
namespace {

class TcpTransport : public Transport {
public:
    TcpTransport(std::string host, int port)
        : host_(std::move(host)), port_(port) {}
    ~TcpTransport() override { close(); }

    bool open() override {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        std::string portStr = std::to_string(port_);
        if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &res) != 0)
            return false;
        int fd = -1;
        for (addrinfo* p = res; p; p = p->ai_next) {
            fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd < 0) continue;
            if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
            ::close(fd);
            fd = -1;
        }
        freeaddrinfo(res);
        if (fd < 0) return false;
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        fd_ = fd;
        return true;
    }

    void close() override {
        if (fd_ >= 0) {
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
            fd_ = -1;
        }
    }
    bool isOpen() const override { return fd_ >= 0; }

    bool write(const uint8_t* data, size_t n) override {
        size_t off = 0;
        while (off < n) {
            ssize_t w = ::send(fd_, data + off, n - off, MSG_NOSIGNAL);
            if (w <= 0) return false;
            off += static_cast<size_t>(w);
        }
        return true;
    }

    int read(uint8_t* buf, size_t n,
             std::chrono::milliseconds timeout) override {
        if (fd_ < 0) return -1;
        pollfd pfd{fd_, POLLIN, 0};
        int pr = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
        if (pr == 0) return 0;
        if (pr < 0) return -1;
        ssize_t r = ::recv(fd_, buf, n, 0);
        if (r == 0) return -1;  // peer closed
        return static_cast<int>(r);
    }

    std::string describe() const override {
        return "tcp://" + host_ + ":" + std::to_string(port_);
    }

private:
    std::string host_;
    int port_;
    int fd_ = -1;
};

}  // namespace

std::unique_ptr<Transport> makeTcpTransport(const std::string& host,
                                            int port) {
    return std::make_unique<TcpTransport>(host, port);
}

}  // namespace scnc
#else
namespace scnc {
std::unique_ptr<Transport> makeTcpTransport(const std::string&, int) {
    return nullptr;  // Windows port: M6 milestone
}
}  // namespace scnc
#endif
