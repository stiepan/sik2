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

    /* Purpose specific validation of arguments */

    if (!is_valid_port(port)) {
        std::cerr << "Incorrect port number" << std::endl;
        return 1;
    }

    if (gspeed < 1 || gspeed > 1000) {
        std::cerr << "Incorrect game speed rate" << std::endl;
        return 1;
    }

    if (tspeed >= 360) {
        std::cerr << "Turning speed should be in [0, 360) range" << std::endl;
        return 1;
    }

    /* Connections */
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

    bool timer_active = false;
    /* Prepare timer */
    uint64_t timeout = NANOSPERS / gspeed;
    try {
        create_timer(registered_clock, timeout, timer_handler);
        disarm_timer(registered_clock, clock_interval);
        clock_interrupt = false;
    }
    catch (UtilsError const &e) {
        std::cerr << e.what();
        return 1;
    }

    /* Actual communication kicks off */
    pollfd pollsocket;
    pollsocket.fd = sock.fd;

    sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);

    GameState gs{seed, gspeed, tspeed, width, height};
    bool want_to_write = false;
    size_t max_datagram_size = MAX_FROM_CLIENT_DATAGRAM_SIZE + 1;

    while (!finish) {
        pollsocket.events = (!want_to_write)? POLLIN : (POLLIN | POLLOUT);
        pollsocket.revents = 0;
        int ret = poll(&pollsocket, 1, -1);
        if (ret <= 0 && !clock_interrupt) {
            continue;
        }
        if (pollsocket.revents & POLLIN) {
            uint64_t rec_time = milliseconds_since_epoch();
            std::string buffer(max_datagram_size, '\0');
            size_t len = recvfrom(
                    sock.fd, &buffer[0], max_datagram_size, 0,
                    reinterpret_cast<sockaddr *>(&client_addr), &addr_len);
            // simply ignore incorrect messages or errors
            if (len > 0 && len <= MAX_FROM_CLIENT_DATAGRAM_SIZE) {
                buffer.resize(len);
                gs.got_message(buffer, client_addr, rec_time);
            }
            // if new round started as a consequence of player's move
            if (std::get<1>(gs.has_active_round()) && !timer_active) {
                timer_active = true;
                resume_timer(registered_clock, clock_interval);
            }
        }
        if (std::get<1>(gs.has_active_round()) && clock_interrupt) {
            gs.cycle();
            // if round has finished within last cycle
            if (!std::get<1>(gs.has_active_round())) {
                disarm_timer(registered_clock, clock_interval);
                timer_active = false;
            }
            clock_interrupt = false;
        }
        if (gs.want_to_write()) {
            std::string buffer;
            sockaddr_storage rec_addr;
            auto events_no = gs.next_datagram(buffer, rec_addr);
            if (events_no > 0) {
                /*std::cerr << "SO it begins " << events_no << ": ";
                for (auto &c : buffer) {
                    std::cerr << (uint32_t)((uint8_t)c) << " ";
                }
                std::cerr << std::endl;*/
                auto len = sendto(sock.fd, &buffer[0], buffer.size(), 0,
                        reinterpret_cast<sockaddr *>(&rec_addr), sizeof(rec_addr));
                if (len >= 0 || (errno != EWOULDBLOCK && errno != EAGAIN)) {
                    gs.mark_sent(events_no);
                }
            }
            want_to_write = gs.want_to_write();
        }
    }

    return 0;
}
