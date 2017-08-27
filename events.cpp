#include "events.h"

void Event::serialize_args(std::stringstream &ss) {}

std::string Event::serialize(char c)
{
    return std::string{c};
}

std::string Event::serialize(std::string str)
{
    return str;
}

std::string Event::NewGame::serialize()
{
    for (auto &c : player_names) {
        if (c < 33 || c > 126) {
            c = '\0';
        }
    }
    return Event::serialize(type, maxx, maxy, player_names);
}

std::string Event::Pixel::serialize()
{
    return Event::serialize(type, player_number, x, y);
}

std::string Event::PlayerEliminated::serialize()
{
    return Event::serialize(type, player_number);
}

std::string Event::GameOver::serialize()
{
    return Event::serialize(type);
}

std::string Event::ClientEvent::serialize()
{
    return Event::serialize(session_id, turn_direction, next_expected_event_no, player_name);
}

bool Event::ClientEvent::parse(std::string const &str)
{
    if (str.length() < 13) {
        return false;
    }
    session_id = Event::parse<uint64_t>(&str[0]);
    turn_direction = str[8];
    if (turn_direction != -1 && turn_direction != 0 && turn_direction != 1) {
        return false;
    }
    next_expected_event_no = Event::parse<uint32_t>(&str[9]);
    player_name = str.substr(13, str.length() - 13);
    for (auto s : player_name) {
        if (s < 33 || s > 126) {
           return false;
        }
    }
    return true;
}
