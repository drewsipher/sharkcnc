// scnc-sim: exposes the in-process simulator on a TCP port so the desktop
// app (or any grbl sender) can be developed with zero hardware.
//   scnc-sim [port=2323]
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>

#include "machine/simulator.h"

using namespace scnc;

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 2323;
    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 ||
        listen(srv, 1) != 0) {
        std::perror("bind/listen");
        return 1;
    }
    std::printf("scnc-sim: fake FluidNC on 127.0.0.1:%d  (ctrl-c to quit)\n",
                port);

    for (;;) {
        int cl = accept(srv, nullptr, nullptr);
        if (cl < 0) break;
        std::printf("client connected\n");
        Simulator sim;
        auto end = sim.takeClientEnd();
        end->open();
        std::atomic<bool> gone{false};

        std::thread up([&] {  // socket -> sim
            uint8_t buf[512];
            while (!gone) {
                ssize_t r = recv(cl, buf, sizeof buf, 0);
                if (r <= 0) break;
                end->write(buf, static_cast<size_t>(r));
            }
            gone = true;
        });
        // sim -> socket
        uint8_t buf[512];
        while (!gone) {
            int r = end->read(buf, sizeof buf, std::chrono::milliseconds(100));
            if (r < 0) break;
            if (r > 0 && send(cl, buf, static_cast<size_t>(r),
                              MSG_NOSIGNAL) <= 0)
                break;
        }
        gone = true;
        shutdown(cl, SHUT_RDWR);
        close(cl);
        up.join();
        std::printf("client disconnected\n");
    }
    close(srv);
    return 0;
}
