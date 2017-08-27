#define _USE_MATH_DEFINES
#include "game_state.h"


Generator r(0);

GameState::GameState(uint32_t seed, uint32_t gs, uint32_t ts, uint32_t mx, uint32_t my)
        : inner_counter{0}, head_in_progress{false}, head_expected_no{0}, board{gs, ts, mx, my}
{
    r = Generator(seed);
}

GameProgress Round::is_active()
{
    bool activated = snakes.size() > 0 ;
    return GameProgress{activated, activated && !round_finished};
}

GameProgress GameState::has_active_round()
{
    return round.is_active();
}

bool GameState::want_to_write()
{
    return !pending_queue.empty();
}

void GameState::cycle()
{
    round.recent_events = false;
    round.cycle();
    if (round.recent_events) {
        notify_players();
    }
}

size_t GameState::next_datagram(std::string &buffer, sockaddr_storage &addr)
{
    size_t i;
    uint64_t player_id;
    bool front_removed = false;
    while (!pending_queue.empty()) {
        player_id = pending_queue.front();
        for (i = 0; i < players.size(); i++) {
            if (players[i].inner_id == player_id) {
                break;
            }
        }
        if (i < players.size()) {
            break;
        }
        pending_queue.pop();
        front_removed = true;
    }
    if (pending_queue.empty()) {
        return 0;
    }
    if (front_removed || !head_in_progress) {
        head_in_progress = true;
        head_expected_no = players[i].expected_no;
    }
    auto &indxs = round.history_indx();
    auto &hist = round.history();
    if (head_expected_no >= indxs.size()) {
        return 0;
    }
    size_t it = head_expected_no;
    size_t mes_size = 0;
    do {
        mes_size += indxs[it] - ((it == 0)? 0 : indxs[it - 1]);
        ++it;
    } while (mes_size <= MAX_FROM_SERVER_DATAGRAM_SIZE - 4 && it < indxs.size());
    buffer = Event::serialize(round.get_game_id());
    buffer.insert(buffer.size(), &hist[(head_expected_no == 0)? 0 : indxs[head_expected_no - 1]], mes_size);
    addr = players[i].sockaddr;
    return it - head_expected_no;
}

void GameState::mark_sent(size_t events_no)
{
    head_expected_no += events_no;
    if (head_expected_no >= round.history_indx().size()) {
        head_in_progress = false;
        pending.erase(pending_queue.front());
        pending_queue.pop();
    }
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
    reserved_names.erase(players[id].name);
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
    if (e.player_name.length() && !reserved_names.insert(e.player_name).second) {
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
            p.last_turn_direction = e.turn_direction;
            p.pressed_arrow |= (e.turn_direction != 0);
            if (!p.lurking && std::get<1>(round.is_active())) {
                round.direction(p.snake_id, e.turn_direction);
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
    //with the conditions above it means some round has finished recently
    if (std::get<0>(round.is_active())) {
        if (round.game_over_raised) {
            round.game_over_raised = false;
            for (auto &p : players) {
                p.pressed_arrow = false;
            }
        }
    }
    size_t counter = 0;
    for (auto &p : players) {
        if (!p.name.length() || !p.pressed_arrow) {
            continue;
        }
        counter++;
        if (counter >= 1) {
            start_new_round();
            return;
        }
    }
}

void GameState::start_new_round()
{
    std::vector<EagerPlayer> eager;
    size_t i = 0;
    for (auto &p : players) {
        if (p.name.length() && p.pressed_arrow) {
            eager.push_back(EagerPlayer{p.name, p.last_turn_direction, i});
        }
        ++i;
    }
    std::sort(eager.begin(), eager.end());
    // restrict number of players so that their names fit in single datagram
    size_t fitting_no = 0;
    uint32_t slen = 28; //it's overhead of additional data
    for (auto &e : eager) {
        slen += std::get<0>(e).length() + 1;
        if (slen > MAX_FROM_SERVER_DATAGRAM_SIZE) {
            break;
        }
        ++fitting_no;
    }
    if (fitting_no < eager.size()) {
        std::cerr << "Limiting the number of players due to long names" << std::endl;
        eager.resize(fitting_no);
    }
    for (size_t j = 0; j < eager.size(); ++j) {
        Player &p = players[std::get<2>(eager[j])];
        p.snake_id = j;
        p.lurking = false;
    }
    round = Round{board, eager};
}

Player::Player(Event::ClientEvent const &e, sockaddr_storage &addr, uint64_t rec_time,
               uint64_t inner_id)
        : lurking{true}, pressed_arrow{e.turn_direction != 0}, last_turn_direction{e.turn_direction},
          name{e.player_name}, inner_id{inner_id}, expected_no{e.next_expected_event_no},
          last_contact{rec_time}, sockaddr{addr}, session_id{e.session_id} {}

Player::Player() = default;

Round::Round() = default;

Round::Round(Board &board, std::vector<EagerPlayer> &eager)
        : board{board}, game_id{r.next()}, eliminated{0}, round_finished{false}, recent_events{false},
          game_over_raised{false}
{
    for (auto &ep : eager) {
        snakes.push_back(Snake{std::get<0>(ep), std::get<1>(ep), board});
    }
    new_game();
    size_t i = 0;
    for (auto &s : snakes) {
        register_move(s.position(), s, i++);
    }
}

void Round::direction(size_t snake_id, uint32_t direction)
{
    snakes[snake_id].last_turn_direction = direction;
}

uint32_t Round::get_game_id()
{
    return game_id;
}

std::vector<char> const &Round::history()
{
    return events_history;
}

std::vector<size_t> const &Round::history_indx()
{
    return events_positions;
}

void Round::event(Event::SerializableEvent &e)
{
    recent_events = true;
    std::string es = e.serialize();
    uint32_t length = es.length() + 4;
    uint32_t event_no = events_positions.size();
    std::string s = Event::serialize(length, event_no, es);
    uint32_t crc = crc32(0, reinterpret_cast<unsigned char *>(&s[0]), s.length());
    s.resize(s.length() + 4);
    *reinterpret_cast<uint32_t *>(&s[s.length() - 4]) = crc;
    events_positions.push_back(events_history.size());
    events_history.insert(events_history.end(), s.begin(), s.end());
}

void Round::new_game()
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

void Round::game_over()
{
    Event::GameOver e;
    event(e);
}

void Round::pixel(Snake &s, size_t player)
{
    Event::Pixel e;
    Position p = s.position();
    e.x = std::get<0>(p);
    e.y = std::get<1>(p);
    e.player_number = player;
    event(e);
}

void Round::player_eliminated(size_t player)
{
    Event::PlayerEliminated e;
    e.player_number = player;
    event(e);
}


void Round::register_move(Position const &new_position, Snake &s, size_t player)
{
    if (std::get<0>(new_position) < board.maxx && std::get<1>(new_position) < board.maxy &&
            board.taken_pxls.insert(new_position).second) {
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

void Round::move(Snake &s, size_t player)
{
    auto old_position = s.position();
    s.direction += s.last_turn_direction * board.turning_speed;
    s.direction %= 360;
    s.x += sin(M_PI * s.direction / 180.);
    s.y += cos(M_PI * s.direction / 180.);
    auto new_position = s.position();
    if (old_position != new_position) {
        register_move(new_position, s, player);
    }
}

void Round::cycle()
{
    size_t player = 0;
    for (auto &snake: snakes) {
        move(snake, player++);
    }
}

Board::Board() = default;

Board::Board(uint32_t gs, uint32_t ts, uint32_t mx, uint32_t my)
        : game_speed{gs}, turning_speed{ts}, maxx{mx}, maxy{my} {}

Round::Snake::Snake(std::string name, int32_t direction, Board const &board)
        : eliminated{false}, x{r.next() % board.maxx + 0.5}, y{r.next() % board.maxy + 0.5},
          direction{r.next() % 360}, last_turn_direction{direction}, name{name}
{

};

Position Round::Snake::position() {
    return Position{static_cast<uint32_t>(x), static_cast<uint32_t>(y)};
}
