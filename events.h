#ifndef II_EVENTS_H
#define II_EVENTS_H
#include "utils.h"

namespace Event {

    template<typename ...Types>
    std::string parse(Types ...args) {
        std::stringstream ss;
        for (auto &arg : {args...}) {
            ss << parse(arg);
        }
        return ss.str();
    }

    template<typename IntegerType>
    std::string parse(IntegerType n)
    {
        IntegerType hn = htonT(n);
        return std::string(reinterpret_cast<char *>(&hn), sizeof(hn));
    }

    char parse(char);
    std::string parse(std::string);

    struct NewGame {
        char type;
        uint32_t maxx, maxy;
        std::string player_names;

        std::string parse();
    };

    struct Pixel {
        char type;
        char player_number;
        uint32_t x, y;

        std::string parse();
    };

    struct PlayerEliminated {
        char type;
        char player_number;

        std::string parse();
    };

    struct GameOver {
        char type;

        std::string parse();
    };
}

#endif //II_EVENTS_H
