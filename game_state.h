#ifndef II_GAME_STATE_H
#define II_GAME_STATE_H

#include <vector>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <ctime>
#include <tuple>
#include <queue>
#include <cmath>
#include <zlib.h>
#include "utils.h"
#include "events.h"
#include "generator.h"

using Position = std::tuple<uint32_t, uint32_t>;
using GameProgress = std::tuple<bool, bool>;
using EagerPlayer = std::tuple<std::string, int32_t, size_t>;

template<typename ...IntegerTypes>
struct HashTuple {
    template <uint64_t c, uint64_t... consts>
    struct Hash {
        static size_t constexpr ith = HashTuple<IntegerTypes...>::Hash<consts...>::ith + 1;
        static size_t sum(std::tuple<IntegerTypes...> const &p) {
            return c * std::get<ith>(p) + HashTuple<IntegerTypes...>::Hash<consts...>::sum(p);
        }
        size_t operator()(std::tuple<IntegerTypes...> const &p) const {
           return sum(p);
        }
    };
    template <uint64_t c>
    struct Hash<c> {
        static size_t constexpr ith = 0;
        static size_t sum(std::tuple<IntegerTypes...> const &p) {
            return c * std::get<ith>(p);
        }
    };
};


uint32_t const TWOTO16 = 65536;
uint64_t const TWOTO32 = 4294967296L;
size_t const MAX_PLAYERS = 42;
uint64_t const INACTIVITY_TOLERANCE = 2000;

extern Generator r;

struct Board {
    uint32_t game_speed, turning_speed;
    uint32_t maxx, maxy;
    std::unordered_set<Position, HashTuple<uint32_t, uint32_t>::Hash<TWOTO16, 1>> taken_pxls;

    Board(uint32_t, uint32_t, uint32_t, uint32_t);
    Board();
};

class Round {
    struct Snake {
        bool eliminated;
        double x, y; // current position on board
        int32_t direction; //current direction snake will move in
        int32_t last_turn_direction; //last valid turn_direction:{-1,0,1} submitted by player
        std::string name; //associated player name

        Snake(std::string name, int32_t, Board const &);
        Position position();
    };

    Board board;
    uint32_t game_id;

    /* Snakes */
    std::vector<Snake> snakes;
    size_t eliminated;

    /* History */
    bool round_finished;
    std::vector<char> events_history;
    std::vector<size_t> events_positions;

public:
    Round(Board &board, std::vector<EagerPlayer> &eager);
    Round();

    bool recent_events, game_over_raised;

    uint32_t get_game_id();
    GameProgress is_active();
    std::vector<char> const &history();
    std::vector<size_t> const &history_indx();

    /* Events generators */
    void event(Event::SerializableEvent &e);
    void new_game();
    void game_over();
    void pixel(Snake &s, size_t player);
    void player_eliminated(size_t player);
    void register_move(Position const &new_position, Snake &s, size_t player);
    void move(Snake &s, size_t player);
    void direction(size_t snake_id, uint32_t direction);
    void cycle();
};


struct Player {
    bool lurking; //if true player doesn't take part in round
    bool pressed_arrow;
    int32_t last_turn_direction;
    std::string name; //if empty player will lurk in any round
    uint64_t inner_id;
    uint32_t expected_no;
    uint64_t last_contact;
    size_t snake_id; //every non-lurking player has their snake during round

    /* socket address and session_id identifies player over the net */
    sockaddr_storage sockaddr;
    uint64_t session_id;
    Player(Event::ClientEvent const &e, sockaddr_storage &addr, uint64_t rec_time, uint64_t inner);
    Player();
};

class GameState {
    /* Players */
    std::vector<Player> players;
    std::unordered_set<std::string> reserved_names;
    uint64_t inner_counter;

    void disconnect_player(size_t id);
    void disconnect_inactive(uint64_t threshold);
    void connect_player(Event::ClientEvent const &e, sockaddr_storage &addr, uint64_t rec_time);
    void connect_or_update_player(
            Event::ClientEvent const &e, sockaddr_storage &addr, uint64_t rec_time);
    void update_game_state_on_player_message();

    /* Sending queue */
    std::queue<uint64_t> pending_queue;
    std::unordered_set<uint64_t> pending;
    bool head_in_progress;
    size_t head_expected_no;

    void notify_player(Player &p);
    void notify_players();

    /* Round */
    Board board;
    Round round;
    void start_new_round();

public:
    GameState(uint32_t seed, uint32_t gs, uint32_t ts, uint32_t mx, uint32_t my);
    void got_message(std::string &buffer, sockaddr_storage &addr, uint64_t rec_time);
    void cycle();
    GameProgress has_active_round();
    size_t next_datagram(std::string &buffer, sockaddr_storage &addr);
    void mark_sent(size_t events_no);
    bool want_to_write();
};
#endif //II_GAME_STATE_H
