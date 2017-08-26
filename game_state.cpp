#define _USE_MATH_DEFINES
#include "game_state.h"


Generator r(0);

GameState::GameState(uint32_t seed)
{
    r = Generator(seed);
}

GameProgress Round::is_active()
{
    return GameProgress{false, false};
}

GameProgress ActiveRound::is_active()
{
    return GameProgress{true, !round_finished};
}

GameProgress GameState::has_active_round()
{
    return round.is_active();
}

bool GameState::want_to_write()
{
    return false;
}

void GameState::cycle()
{

}

void GameState::got_message(std::string &buffer, sockaddr_storage &addr, uint64_t rec_time)
{
    Event::ClientEvent e;
    // simply drop incorrect messages
    if (!e.parse(buffer)) {
        std::cerr << "Dropping incorrect message" << std::endl;
        return;
    }
    disconnect_inactive(rec_time);
    connect_or_update_player(e, addr, rec_time);
    update_game_state_on_player_message();
}

void GameState::disconnect_inactive(uint64_t threshold)
{
    size_t i = 0;
    while (i < players.size()) {
        if (threshold - players[i].last_contact < INACTIVITY_TOLERANCE) {
            ++i;
            continue;
        }
        disconnect_player(i);
    }
}

void GameState::disconnect_player(size_t id)
{
    if (id < players.size() - 1) {
        std::swap(players[id], players[players.size() - 1]);
    }
    players.pop_back();
    std::cerr << "Disconnected player" << std::endl;
}

void GameState::notify_player(Player &p)
{
    auto inserted = pending.insert(p.inner_id);
    if (inserted.second) {
        pending_queue.push(p.inner_id);
    }
}

void GameState::notify_players()
{
    for (auto &p : players) {
        notify_player(p);
    }
}

void GameState::connect_player(
        Event::ClientEvent const &e, sockaddr_storage &addr, uint64_t rec_time)
{
    if (players.size() >= MAX_PLAYERS) {
        return;
    }
    players.push_back(Player{e, addr, rec_time, inner_counter++});
    std::cerr << "Connected new player" << std::endl;
    notify_player(players[players.size() - 1]);
}

void GameState::connect_or_update_player(
        Event::ClientEvent const &e, sockaddr_storage &addr, uint64_t rec_time)
{
    for (size_t i = 0; i < players.size(); ++i) {
        Player &p = players[i];
        if (!(p.sockaddr == addr)) {
            continue;
        }
        if (p.session_id > e.session_id) {
            return;
        } else if (p.session_id < e.session_id) {
            disconnect_player(i);
            connect_player(e, addr, rec_time);
            return;
        } else {
            p.last_contact = rec_time;
            p.expected_no = e.next_expected_event_no;
            p.pressed_arrow |= (e.turn_direction != 0);
            if (!p.lurking && std::get<1>(round.is_active())) {
                ActiveRound &around = dynamic_cast<ActiveRound&>(round);
                around.direction(p.snake_id, e.turn_direction);
            }
            notify_player(p);
            return;
        }
    }
    connect_player(e, addr, rec_time);
}


void GameState::update_game_state_on_player_message()
{
    if (std::get<1>(round.is_active())) {
        return;
    }
    ActiveRound &around = dynamic_cast<ActiveRound&>(round);
    if (around.game_over_raised) {
        around.game_over_raised = false;
        for (auto &p : players) {
            p.pressed_arrow = false;
        }
        return;
    }
    size_t counter = 0;
    for (auto &p : players) {
        if (!p.name.length() || !p.pressed_arrow) {
            continue;
        }
        counter++;
        if (counter >= 2) {
            start_new_round();
            return;
        }
    }
}

void GameState::start_new_round()
{
    
}

Player::Player(Event::ClientEvent const &e, sockaddr_storage &addr, uint64_t rec_time,
               uint64_t inner_id)
        : lurking{true}, pressed_arrow{e.turn_direction != 0}, name{e.player_name}, inner_id{inner_id},
          expected_no{e.next_expected_event_no}, last_contact{rec_time}, sockaddr{addr},
          session_id{e.session_id} {}

Player::Player() = default;

Round::~Round() = default;

ActiveRound::ActiveRound(uint32_t gs, uint32_t ts, uint32_t mx, uint32_t my)
        : board{gs, ts, mx, my}, game_id{r.next()} {}

void ActiveRound::direction(size_t snake_id, uint32_t direction)
{
    snakes[snake_id].last_turn_direction = direction;
}

bool ActiveRound::recent()
{
    return recent_events;
}

std::vector<char> const &ActiveRound::history()
{
    return events_history;
}

std::vector<size_t> const &ActiveRound::history_indx()
{
    return events_positions;
}

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
                game_over_raised = true;
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
