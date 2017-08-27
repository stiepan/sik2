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

int main(int argc, char *argv[]) {

    /* Parsing arguments */
    std::string player_name;
    std::string servera, guia;

    if (argc < 3 || argc > 4) {
        std::cerr << "Usage " << argv[0]
                  << "player_name game_server_host[:port] [ui_server_host[:port]]" << std::endl;
        return 1;
    }

    player_name = argv[1];

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

    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    /* Purpose specific validation of arguments */
    std::cout << (1^2) << std::endl;
    return 0;
}

