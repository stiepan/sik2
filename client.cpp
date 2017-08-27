#include <netinet/tcp.h>
#include <poll.h>
#include "utils.h"
#include "events.h"

uint64_t TIMEOUT_NS = 20000000;
size_t const MAX_FROM_GUI_SIZE = 15;

bool finish = false, clock_interrupt = false;
timer_t registered_clock;
itimerspec clock_interval;

/* Game state */
int8_t turn_direction = 0;
uint32_t next_expected_event_no = 0;
uint64_t session_id;
std::string player_name;

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

void set_turn_direction_accordingly(std::string &m)
{
    if (m == "LEFT_KEY_DOWN") {
        turn_direction = -1;
    }
    else if (m == "RIGHT_KEY_DOWN") {
        turn_direction = 1;
    }
    else if (m == "LEFT_KEY_UP" || m == "RIGHT_KEY_UP") {
        turn_direction = 0;
    }
}

void process_gui_response(std::string &gbuf, size_t &ggot, size_t &rec)
{
    size_t i;
    for (i = 0; i < rec; ++i) {
        if (gbuf[ggot + i] == '\n') {
            break;
        }
    }
    if (i < rec) {
        std::string message = gbuf.substr(0, ggot + i);
        set_turn_direction_accordingly(message);
        // trim new line and rewrite rest to beginning of the buffer
        for (size_t j = 0; j < rec - i - 1; j++) {
            gbuf[j] = gbuf[ggot + i + 1 + j];
        }
        ggot = rec - i - 1;
    }
}

std::string to_server_message()
{
    Event::ClientEvent e;
    e.session_id = session_id;
    e.turn_direction = turn_direction;
    e.next_expected_event_no = next_expected_event_no;
    e.player_name = player_name;
    return e.serialize();
}

int main(int argc, char *argv[])
{

    /* Set session id to microseconds elapsed since epoch  */
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);

        session_id = UINT64_C(1000000) * (tv.tv_sec) + (tv.tv_usec);
    }

    /* Parsing arguments */
    std::string sa, sp, ga, gp;

    if (argc < 3 || argc > 4) {
        std::cerr << "Usage " << argv[0]
                  << "player_name game_server_host[:port] [ui_server_host[:port]]" << std::endl;
        return 1;
    }

    player_name = argv[1];
    sa = argv[2];

    if (argc == 4) {
        ga = argv[3];
    }

    if (player_name.length() > 64) {
        std::cerr << "Player name too long" << std::endl;
        return 1;
    }

    for (auto &c : player_name) {
        if (c < 33 || c > 126) {
            std::cerr << "Incorrect player name" << std::endl;
            return 1;
        }
    }

    if (!sa.length() || sa.back() == ':' || (ga.length() && ga.back() == ':')) {
        std::cerr << "Trailing ':'" << std::endl;
        return 1;
    }

    auto split = [](std::string &addr) -> std::string {
        auto pos = addr.rfind(':');
        if (pos == std::string::npos) {
            return "";
        }
        std::string port = addr.substr(pos + 1);
        addr.resize(pos);
        return port;
    };

    sp = split(sa);
    gp = split(ga);

    if (!sa.length()) {
        std::cerr << "Incorrect game server address" << std::endl;
        return 1;
    }

    sp = (!sp.length())? "12345" : sp;
    gp = (!gp.length())? "12346" : gp;
    ga = (!ga.length())? "localhost" : ga;

    try {
        if (!is_valid_port(str2uint32_t(sp)) || !is_valid_port(str2uint32_t(gp))) {
            std::cerr << "Inocrrect port number" << std::endl;
            return 1;
        }
    }
    catch (UtilsError &e) {
        std::cerr << "Port parsing: " << e.what() << std::endl;
        return 1;
    }

    /* Game server socket */
    Socket ssock;
    {
        struct addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;

        AddrInfo sinfo(&sa[0], &sp[0], hints);

        if (!sinfo.info) {
            std::cerr << "getaddrinfo: " << sinfo.err << std::endl;
            return 1;
        }

        std::string last_error;
        for (addrinfo *p = sinfo.info; p != NULL && ssock.fd == -1; p = p->ai_next) {

            Socket try_socket;
            if ((try_socket.fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
                last_error = last_err("Socket: ");
                continue;
            }
            if (connect(try_socket.fd, p->ai_addr, p->ai_addrlen) == -1) {
                last_error = last_err("Connect gui: ");
                continue;
            }
            ssock = std::move(try_socket);
        }

        if (ssock.fd == -1) {
            std::cerr << "Couldn't create socket. " << last_error << std::endl;
            return 1;
        }
    }

    if (fcntl(ssock.fd, F_SETFL, O_NONBLOCK) < 0) {
        std::cerr << last_err("Fcntl: ") << std::endl;
        return 1;
    }

    /* GUI socket */
    Socket gsock;
    {
        struct addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        AddrInfo ginfo(&ga[0], &gp[0], hints);

        if (!ginfo.info) {
            std::cerr << "gui getaddrinfo: " << ginfo.err << std::endl;
            return 1;
        }

        std::string last_error;
        for (addrinfo *p = ginfo.info; p != NULL && gsock.fd == -1; p = p->ai_next) {

            Socket try_socket;
            if ((try_socket.fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
                last_error = last_err("Socket gui: ");
                continue;
            }
            if (connect(try_socket.fd, p->ai_addr, p->ai_addrlen) == -1) {
                last_error = last_err("Connect gui: ");
                continue;
            }
            gsock = std::move(try_socket);
        }

        if (gsock.fd == -1) {
            std::cerr << "Couldn't create gui socket. " << last_error << std::endl;
            return 1;
        }
    }

    if (fcntl(gsock.fd, F_SETFL, O_NONBLOCK) < 0) {
        std::cerr << last_err("Fcntl gui: ") << std::endl;
        return 1;
    }
    {
        int flag = 1;
        int result = setsockopt(gsock.fd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
        if (result < 0){
            std::cerr << last_err("Could switch off Nagle's algorithm: ") << std::endl;
        }
    }

    /* Adjust signal handling */
    if (signal(SIGINT, catch_int) == SIG_ERR) {
        std::cerr << "Couldn't change signal handling" << std::endl;
    }

    /* Timer */
    try {
        create_timer(registered_clock, TIMEOUT_NS, timer_handler);
    }
    catch (UtilsError const &e) {
        std::cerr << e.what();
        return 1;
    }

    /* Communication kicks off */
    bool write_more_to_server = false, write_more_to_gui = false;
    pollfd pollsocket[2];
    pollfd &serverp = pollsocket[0];
    pollfd &guip = pollsocket[1];
    serverp.fd = ssock.fd;
    guip.fd = gsock.fd;

    /* Buffering */
    std::string gbuf(MAX_FROM_GUI_SIZE, '\0');
    size_t ggot = 0;

    while(!finish) {
        for (size_t i = 0; i < 2; i++) {
            pollsocket[i].revents = 0;
        }
        serverp.events = (!write_more_to_server)? POLLIN : (POLLIN | POLLOUT);
        guip.events = (!write_more_to_gui)? POLLIN : (POLLIN | POLLOUT);
        int ret = poll(pollsocket, 2, -1);
        if (ret <= 0 && !clock_interrupt) {
            continue;
        }
        /* Read messages */
        if (guip.revents & POLLIN) {
            size_t rec;
            if((rec = recv(gsock.fd, &gbuf[ggot], gbuf.size() - ggot, 0)) <= 0) {
                std::cerr << "GUI disconnected" << std::endl;
                return 1;
            }
            process_gui_response(gbuf, ggot, rec);
        }
        if (serverp.revents & POLLIN) {

        }
        if (clock_interrupt || (write_more_to_server && (serverp.revents & POLLOUT))) {
            std::string ssbuf = to_server_message();
            clock_interrupt = false;
            write_more_to_server = false;
            if (send(ssock.fd, &ssbuf[0], ssbuf.size(), 0) == -1) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    write_more_to_server = true;
                }
                else {
                    std::cerr << last_err("Socket error on sending. ") << std::endl;
                    return 1;
                }
            }
        }
        // if messages to gui send them here
    }

    return 0;
}

