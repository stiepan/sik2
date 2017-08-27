#include <netinet/tcp.h>
#include "utils.h"
#include "events.h"

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
    std::string player_name;
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

    /* Communication kicks off */
    

    /* Purpose specific validation of arguments */
    return 0;
}

