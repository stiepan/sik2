#define _USE_MATH_DEFINES
#include "game_state.h"


Generator r(0);

GameState::GameState(uint32_t seed) : players{MAX_PLAYERS}
{
    r = Generator(seed);
}

Round::~Round() = default;

ActiveRound::ActiveRound(uint32_t gs, uint32_t ts, uint32_t mx, uint32_t my)
        : board{gs, ts, mx, my}, game_id{r.next()} {}

void ActiveRound::event(Event::SerializableEvent &e)
{
    recent_events = true;
    std::string es = e.serialize();
    uint32_t length = es.length() + 12;
    uint32_t event_no = events_positions.size();
    std::string s = Event::serialize(length, event_no, es);
    uint32_t crc = crc32(0, reinterpret_cast<unsigned char *>(&s[0]), s.length());
    s.resize(s.length() + 4);
    *reinterpret_cast<uint32_t *>(&s[s.length() - 4]) = crc;
    events_positions.push_back(events_history.size());
    events_history.insert(events_history.end(), s.begin(), s.end());
}

void ActiveRound::new_game()
{
    Event::NewGame e;
    e.maxx = board.maxx;
    e.maxy = board.maxy;
    std::stringstream ss;
    for (auto &snake : snakes) {
        ss << snake.name << " ";
    }
    e.player_names = ss.str();
    event(e);
}

void ActiveRound::game_over()
{
    Event::GameOver e;
    event(e);
}

void ActiveRound::pixel(Snake &s, size_t player)
{
    Event::Pixel e;
    Position p = s.position();
    e.x = std::get<0>(p);
    e.y = std::get<1>(p);
    e.player_number = player;
    event(e);
}

void ActiveRound::player_eliminated(size_t player)
{
    Event::PlayerEliminated e;
    e.player_number = player;
    event(e);
}

void ActiveRound::move(Snake &s, size_t player)
{
    auto old_position = s.position();
    s.direction += s.last_turn_direction * board.turning_speed;
    s.direction %= 360;
    s.x += sin(M_PI * s.direction / 180.);
    s.y += cos(M_PI * s.direction / 180.);
    auto new_position = s.position();
    if (old_position != new_position) {
        auto inserted = board.taken_pxls.insert(new_position);
        if (inserted.second) {
            pixel(s, player);
        }
        else {
            player_eliminated(player);
            s.eliminated = true;
            ++eliminated;
            if (snakes.size() - eliminated <= 1) {
                game_over();
                round_finished = true;
            }
        }
    }
}

void ActiveRound::cycle()
{
    size_t player = 0;
    for (auto &snake: snakes) {
        move(snake, player++);
    }
}

ActiveRound::Board::Board(uint32_t gs, uint32_t ts, uint32_t mx, uint32_t my)
        : game_speed{gs}, turning_speed{ts}, maxx{mx}, maxy{my} {}

ActiveRound::Snake::Snake(std::string name, int32_t direction, Board const &board)
        : x{r.next() % board.maxx + 0.5}, y{r.next() % board.maxy + 0.5},
          direction{r.next() % 360}, last_turn_direction{direction}, name{name} {};

Position ActiveRound::Snake::position() {
    return Position{static_cast<uint32_t>(x), static_cast<uint32_t>(y)};
}


