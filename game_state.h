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
using PendingPlayer = std::tuple<size_t, uint64_t>;

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
extern Generator r;

class Round {
public:
    virtual ~Round(); // don't really plan to use pointers, yet just in case
};

class ActiveRound : public Round {
    struct Board;
    struct Snake {
        bool eliminated;
        double x, y; // current position on board
        uint32_t direction; //current direction snake will move in
        int32_t last_turn_direction; //last valid turn_direction:{-1,0,1} submitted by player
        std::string name; //associated player name

        Snake(std::string name, int32_t, Board const &);
        Position position();
    };

    struct Board {
        uint32_t const game_speed, turning_speed;
        uint32_t const maxx, maxy;
        std::unordered_set<Position, HashTuple<uint32_t, uint32_t>::Hash<TWOTO16, 1>> taken_pxls;

        Board(uint32_t, uint32_t, uint32_t, uint32_t);
    } board;

    uint32_t const game_id;

    /* Snakes */
    std::vector<Snake> snakes;
    size_t eliminated;

    /* History */
    bool recent_events, round_finished;
    std::vector<char> events_history;
    std::vector<size_t> events_positions;

public:
    ActiveRound(uint32_t gs, uint32_t ts, uint32_t mx, uint32_t my);

    bool recent();
    bool finished();
    std::vector<char> const &history();
    std::vector<size_t> const &history_indx();

    /* Events generators */
    void event(Event::SerializableEvent &e);
    void new_game();
    void game_over();
    void pixel(Snake &s, size_t player);
    void player_eliminated(size_t player);
    void move(Snake &s, size_t player);
    void cycle();
};


struct Player {
    bool lurking; //if true player doesn't take part in round
    std::string name; //if empty player will lurk in any round
    uint32_t turn, expected_no;
    time_t last_contact;
    size_t snake_id; //every non-lurking player has their snake during round

    /* socket address and session_id identifies player over the net */
    sockaddr_storage sockaddr;
    uint64_t session_id;
    uint64_t slot_uses;
};

class GameState {
    /* Players */
    std::vector<Player> players;
    size_t no_connected;
    size_t no_eager;
    size_t no_ready;

    /* Sending queue */
    std::queue<PendingPlayer> pending;
    std::unordered_set<PendingPlayer, HashTuple<size_t, uint64_t>::Hash<TWOTO32, 1>> pending_set;
    bool head_in_progress;
    size_t head_expected_no;

    /* Round */
    Round round;

public:
    GameState(uint32_t seed);
};
#endif //II_GAME_STATE_H
