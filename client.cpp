#include <vector>
#include <zlib.h>
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
int8_t turn_direction;
uint32_t next_expected_event_no;
uint64_t session_id;
std::string player_name;
uint32_t maxx, maxy;
std::vector<std::string> players;

int64_t game_id = -1;
bool active_round = false;

/* Game state messages to gui */
std::vector<char> gui_messages;
size_t head;

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

// returns next message number or 0 on error
uint32_t verify_message(std::string &datagram, size_t pos)
{
    if (pos + 4 >= datagram.size()) {
        return 0;
    }
    uint32_t len = Event::parse<uint32_t>(&datagram[pos]);
    if (pos + len + 8 > datagram.size()) {
        return 0;
    }
    uint32_t crc = crc32(0, reinterpret_cast<unsigned char *>(&datagram[pos]), len + 4);
    uint32_t rcrc = Event::parse<uint32_t>(&datagram[pos + len + 4]);
    if (crc != rcrc) {
        return 0;
    }
    return len;
}

void push_event_to_gui(std::stringstream &ss)
{
    ss << std::endl;
    auto s = ss.str();
    gui_messages.insert(gui_messages.end(), s.begin(), s.end());
}

bool new_game(std::string event_data)
{
    if (event_data.length() < 8) {
        return false;
    }
    maxx = Event::parse<uint32_t>(&event_data[0]);
    maxy = Event::parse<uint32_t>(&event_data[4]);
    players.clear();
    for (size_t it = 8, b_it=8; it < event_data.size(); it++) {
        if (event_data[it] == '\0') {
            if (it <= b_it) {
                return false;
            }
            players.push_back(std::string(event_data.begin() + b_it, event_data.begin() + it));
            b_it = it + 1;
        }
        else if (event_data[it] < 33 || event_data[it] > 126) {
           return false;
        }
    }
    if (players.size() < REQUIRED_PLAYERS) {
        return false;
    }
    std::stringstream ss;
    ss << "NEW_GAME " << maxx << " " << maxy << " ";
    for (auto &p : players) {
        ss << p << " ";
    }
    gui_messages.clear();
    head = 0;
    push_event_to_gui(ss);
    return true;
}

bool pixel(std::string event_data)
{
    if (event_data.length() != 9) {
        return false;
    }
    uint8_t player = Event::parse<uint8_t>(&event_data[0]);
    if (player >= players.size()) {
        return false;
    }
    uint32_t x = Event::parse<uint32_t>(&event_data[1]);
    uint32_t y = Event::parse<uint32_t>(&event_data[5]);
    if (x >= maxx || y >= maxy) {
        return false;
    }
    std::stringstream ss;
    ss << "PIXEL " << x << " " << y << " " << players[player];
    push_event_to_gui(ss);
    return true;
}

bool player_eliminated(std::string event_data)
{
    if (event_data.length() != 1) {
        return false;
    }
    uint8_t player = Event::parse<uint8_t>(&event_data[0]);
    if (player >= players.size()) {
        return false;
    }
    std::stringstream ss;
    ss << "PLAYER_ELIMINATED " << players[player];
    push_event_to_gui(ss);
    return true;
}

void got_message_from_server(std::string &datagram)
{
    if (datagram.length() < 4) {
        return;
    }
    uint32_t r_game_id = Event::parse<uint32_t>(&datagram[0]);
    if ((active_round && game_id != r_game_id) || (!active_round && game_id == r_game_id)) {
        return;
    }
    size_t it = 4, len = 0;
    while ((len = verify_message(datagram, it))) {
        uint32_t event_no = Event::parse<uint32_t>(&datagram[it + 4]);
        if (next_expected_event_no == event_no) {
            char mtype = datagram[it + 8];
            if (mtype == 0) {
                if (event_no == 0 && !active_round) {
                    if (!new_game(std::string(&datagram[it + 9], len - 5))) {
                        break;
                    }
                    next_expected_event_no = event_no + 1;
                    game_id = r_game_id;
                    active_round = true;
                }
            }
            else if (mtype == 1 && active_round) {
                if (!pixel(std::string(&datagram[it + 9], len - 5))) {
                    break;
                }
                next_expected_event_no = event_no + 1;
            }
            else if (mtype == 2 && active_round) {
                if (!player_eliminated(std::string(&datagram[it + 9], len - 5))) {
                    break;
                }
                next_expected_event_no = event_no + 1;
            }
            else if (mtype == 3 && active_round) {
                next_expected_event_no = 0;
                active_round = false;
                break;
            }
            else {
                break;
            }
        }
        it += len + 8;
    }
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
            std::cerr << "Incorrect port number" << std::endl;
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
                last_error = last_err("Connect: ");
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
    size_t max_datagram_size = MAX_FROM_SERVER_DATAGRAM_SIZE + 1;

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
            std::string sbuf(max_datagram_size, '\0');
            size_t len = recv(ssock.fd, &sbuf[0], MAX_FROM_SERVER_DATAGRAM_SIZE, 0);
            // simply ignore incorrect messages or errors
            if (len > 0 && len <= MAX_FROM_SERVER_DATAGRAM_SIZE) {
                sbuf.resize(len);
                got_message_from_server(sbuf);
            }
            else {
                std::cerr << "Droping incorrect message" << std::endl;
            }
        }
        if (clock_interrupt || (write_more_to_server && (serverp.revents & POLLOUT))) {
            std::string ssbuf = to_server_message();
            clock_interrupt = false;
            write_more_to_server = false;
            if (send(ssock.fd, &ssbuf[0], ssbuf.size(), 0) == -1) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    write_more_to_server = true;
                }
            }
        }
        if (head < gui_messages.size() || write_more_to_gui) {
            size_t len = send(gsock.fd, &gui_messages[head], gui_messages.size() - head, 0);
            write_more_to_gui = false;
            if (len == 0) {
                std::cerr << "GUI disconnected (write)" << std::endl;
                return 1;
            }
            else if (len < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    write_more_to_gui = true;
                }
                else {
                    std::cerr << "GUI write error" << std::endl;
                    return 1;
                }
            }
            else {
                head += len;
                write_more_to_gui = (head < gui_messages.size());
            }
        }
    }

    return 0;
}

