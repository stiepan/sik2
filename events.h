#ifndef II_EVENTS_H
#define II_EVENTS_H
#include "utils.h"

namespace Event {

    template<typename IntegerType>
    std::string serialize(IntegerType n)
    {
        IntegerType hn = bswap(n);
        return std::string(reinterpret_cast<char *>(&hn), sizeof(hn));
    }
    char serialize(char);
    std::string serialize(std::string);

    void serialize_args(std::stringstream &ss);

    template<typename Type, typename ...Types>
    void serialize_args(std::stringstream &ss, Type &arg, Types &...args) {
        ss << serialize(arg);
        serialize_args(ss, args...);
    }

    template<typename ...Types>
    std::string serialize(Types &...args) {
        std::stringstream ss;
        serialize_args(ss, args...);
        return ss.str();
    }

    template<typename IntegerType>
    IntegerType parse(char const *str)
    {
        IntegerType n = *reinterpret_cast<IntegerType const *>(str);
        return bswap(n);
    }

    struct SerializableEvent {
        virtual std::string serialize() = 0;
    };

    template <char t>
    struct EventType {
        char type;
        EventType() : type{t} {}
    };

    /* Server to client events */
    struct NewGame : public EventType<0>, public SerializableEvent {
        uint32_t maxx, maxy;
        std::string player_names;

        std::string serialize();
    };

    struct Pixel : public EventType<1>, public SerializableEvent {
        char player_number;
        uint32_t x, y;

        std::string serialize();
    };

    struct PlayerEliminated : public EventType<2>, public SerializableEvent {
        char player_number;

        std::string serialize();
    };

    struct GameOver : public EventType<3>, public SerializableEvent {

        std::string serialize();
    };

    /* Client to server events */
    struct ClientEvent : public SerializableEvent {
        uint64_t session_id;
        int8_t turn_direction;
        uint32_t next_expected_event_no;
        std::string player_name;

        std::string serialize();
        bool parse(std::string const &);
    };
}

#endif //II_EVENTS_H
