#include "game_state.h"
#include <iostream>

Generator r(0);

GameState::GameState(uint32_t seed)
        : no_connected{0}, no_eager{0}, no_ready{0}, the_eager_names_length{0},
          round{Round{}}
{
    r = Generator(seed);
}

Round::~Round() = default;

ActiveRound::ActiveRound(uint32_t gs, uint32_t ts, uint32_t mx, uint32_t my)
        : board{gs, ts, mx, my}, game_id {r.next()} {}

ActiveRound::Board::Board(uint32_t gs, uint32_t ts, uint32_t mx, uint32_t my)
        : game_speed{gs}, turning_speed{ts}, maxx{mx}, maxy{my} {}

ActiveRound::Snake::Snake() : active{false} {}

ActiveRound::Snake::Snake(int32_t direction, Board const &board)
        : active{true}, eliminated{false}, x{r.next() % board.maxx + 0.5},
          y{r.next() % board.maxy + 0.5}, direction{r.next() % 360},
          last_turn_direction{direction} {};

Position ActiveRound::Snake::position() {
    return Position{static_cast<uint32_t>(x), static_cast<uint32_t>(y)};
}


