#ifndef SIECI_II_GENERATOR_H
#define SIECI_II_GENERATOR_H

#include <cstdint>

class Generator {
    uint64_t r;
public:
    static uint64_t const MOD = 4294967291;
    static uint64_t const GEN = 279470273;

    Generator(uint32_t);

    uint32_t next();
};

#endif //SIECI_II_GENERATOR_H
