#include <cstdint>
#include <iostream>
#include <poll.h>
#include <time.h>
#include "utils.h"
#include "events.h"
#include "game_state.h"

bool finish = false, clock_interrupt = false;
timer_t registered_clock;
itimerspec clock_interval;

void catch_int (int sig)
{
    finish = true;
}

void timer_handler(int sig, siginfo_t *si, void *uc)
{
    if (registered_clock == *static_cast<timer_t *>(si->si_value.sival_ptr)) {
        clock_interrupt = true;
    }
}

int main(int argc, char *argv[])
{

    /* Parsing arguments */
    uint32_t width = 800, height = 600,
            port = 12345, gspeed = 50, tspeed = 6,
            seed = static_cast<uint32_t >(time(NULL) % Generator::MOD);
    int opt;
    while ((opt = getopt(argc, argv, "W:H:p:s:t:r:")) != -1) {
        uint32_t parsed;
        if (optarg == NULL) {
            return 1;
        }
        try {
            parsed = str2uint32_t(optarg);
        } catch (UtilsError const &e) {
            std::cerr << static_cast<char>(opt) << ": " << e.what() << std::endl;
            return 1;
        }
        switch (opt) {
            case 'W':
                width = parsed;
                break;
            case 'H':
                height = parsed;
                break;
            case 'p':
                port = parsed;
                break;
            case 's':
                gspeed = parsed;
                break;
            case 't':
                tspeed = parsed;
                break;
            case 'r':
                seed = parsed;
                break;
            default:
                std::cerr << "Usage " << argv[0]
                          << " [-W n] [-H n] [-p n] [-s n] [-t n] [-r n]" << std::endl;
                return 1;
        }
    }

    /* TODO Purpose specific validation of arguments */

    if (!is_valid_port(port)) {
        std::cerr << "Incorrect port number" << std::endl;
        return 1;
    }

    if (gspeed < 1 || gspeed > 1000) {
        std::cerr << "Incorrect game speed rate" << std::endl;
        return 1;
    }

    /* Connections */
    //int numbytes;
   // struct sockaddr_storage their_addr;
    //char buf[MAXBUFLEN];
    //socklen_t addr_len;
    //char s[INET6_ADDRSTRLEN];

    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    Socket sock;
    AddrInfo info(nullptr, std::to_string(port).c_str(), hints);

    if (!info.info) {
        std::cerr << "getaddrinfo: " << info.err << std::endl;
        return 1;
    }

    // first try to create IPv6 socket, then try to create IPv6 or IPv4 socket
    std::string last_error;
    for (auto flag: {AF_INET6, AF_INET}) {
        for (addrinfo *p = info.info; p != NULL && sock.fd == -1; p = p->ai_next) {
            if ((p->ai_family & flag) != flag) {
                continue;
            }

            Socket try_socket;
            if ((try_socket.fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
                last_error = last_err("Socket: ");
                continue;
            }
            if (bind(try_socket.fd, p->ai_addr, p->ai_addrlen) == -1) {
                last_error = last_err("Bind: ");
                continue;
            }
            sock = std::move(try_socket);
        }
        if (sock.fd != -1) {
            break;
        }
    }

    if (sock.fd == -1) {
        std::cerr << "Couldn't create socket. " << last_error << std::endl;
        return 1;
    }

    if (fcntl(sock.fd, F_SETFL, O_NONBLOCK) < 0) {
        std::cerr << last_err("Fcntl: ") << std::endl;
        return 1;
    }

    /* Adjust signal handling */
    if (signal(SIGINT, catch_int) == SIG_ERR) {
        std::cerr << "Couldn't change signal handling" << std::endl;
    }

    uint64_t timeout = NANOSPERS / gspeed;
    try {
        create_timer(registered_clock, timeout, timer_handler);
    }
    catch (UtilsError const &e) {
        std::cerr << e.what();
        return 1;
    }

    /* Actual communication kicks off */
    size_t rcv_len;
    struct sockaddr_in client_addr;
    socklen_t rcva_len = (socklen_t) sizeof(client_addr);

    pollfd pollsocket;
    pollsocket.fd = sock.fd;
    pollsocket.events = POLLIN;

    struct timeval tv;

    gettimeofday(&tv, NULL);

    unsigned long long millisecondsSinceEpoch =
            (unsigned long long)(tv.tv_sec) * 1000 +
            (unsigned long long)(tv.tv_usec) / 1000;

    int counter = 0;

    disarm_timer(registered_clock, clock_interval);

    while (!finish) {
        pollsocket.revents = 0;
        int ret = poll(&pollsocket, 1, 3000);

        std::cout << "ddd " << clock_interrupt << " " << ret << std::endl;
        if (clock_interrupt) {
            ++counter;
            gettimeofday(&tv, NULL);
            clock_interrupt = false;
            if (counter % 1000 == 0) {
                disarm_timer(registered_clock, clock_interval);
            }
        }
        else {
            resume_timer(registered_clock, clock_interval);
        }
    }

    unsigned long long millisecondsSinceEpoch1 =
            (unsigned long long)(tv.tv_sec) * 1000 +
            (unsigned long long)(tv.tv_usec) / 1000;

    std::cout << millisecondsSinceEpoch1 - millisecondsSinceEpoch << " " << counter << std::endl;
    return 0;
}
