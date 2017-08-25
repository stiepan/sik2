#include "events.h"

char Event::parse(char c)
{
   return c;
}

std::string Event::parse(std::string str)
{
    for (auto it = str.begin(); it < str.end(); ++it) {
        if (*it < '33' || *it > '126') {
            *it = '\0';
        }
        return str;
    }
}

std::string Event::NewGame::parse()
{
    return parse(type, maxx, maxy, player_names);
}

std::string Event::Pixel::parse()
{
    return parse(type, player_number, x, y);
}

std::string Event::PlayerEliminated::parse()
{
    return parse(type, player_number);
}

std::string Event::GameOver::parse()
{
    return parse(type);
}
