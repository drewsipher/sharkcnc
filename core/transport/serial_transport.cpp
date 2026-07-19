#include "transport.h"

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>

namespace scnc {
namespace {

speed_t baudConst(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default: return B115200;
    }
}

class SerialTransport : public Transport {
public:
    SerialTransport(std::string dev, int baud)
        : dev_(std::move(dev)), baud_(baud) {}
    ~SerialTransport() override { close(); }

    bool open() override {
        int fd = ::open(dev_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) return false;
        termios tio{};
        if (tcgetattr(fd, &tio) != 0) {
            ::close(fd);
            return false;
        }
        cfmakeraw(&tio);
        tio.c_cflag |= CLOCAL | CREAD;
        tio.c_cflag &= ~CRTSCTS;
        cfsetispeed(&tio, baudConst(baud_));
        cfsetospeed(&tio, baudConst(baud_));
        tio.c_cc[VMIN] = 0;
        tio.c_cc[VTIME] = 0;
        if (tcsetattr(fd, TCSANOW, &tio) != 0) {
            ::close(fd);
            return false;
        }
        tcflush(fd, TCIOFLUSH);
        fd_ = fd;
        return true;
    }

    void close() override {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
    bool isOpen() const override { return fd_ >= 0; }

    bool write(const uint8_t* data, size_t n) override {
        size_t off = 0;
        while (off < n) {
            ssize_t w = ::write(fd_, data + off, n - off);
            if (w < 0) {
                if (errno == EAGAIN) {
                    pollfd pfd{fd_, POLLOUT, 0};
                    ::poll(&pfd, 1, 1000);
                    continue;
                }
                return false;
            }
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
        ssize_t r = ::read(fd_, buf, n);
        if (r < 0) return errno == EAGAIN ? 0 : -1;
        if (r == 0) return -1;
        return static_cast<int>(r);
    }

    std::string describe() const override {
        return dev_ + "@" + std::to_string(baud_);
    }

private:
    std::string dev_;
    int baud_;
    int fd_ = -1;
};

}  // namespace

std::unique_ptr<Transport> makeSerialTransport(const std::string& device,
                                               int baud) {
    return std::make_unique<SerialTransport>(device, baud);
}

}  // namespace scnc
#else
namespace scnc {
std::unique_ptr<Transport> makeSerialTransport(const std::string&, int) {
    return nullptr;  // Windows port: M6 milestone
}
}  // namespace scnc
#endif
