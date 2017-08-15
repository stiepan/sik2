#include "generator.h"

Generator::Generator(uint32_t seed) : r{seed % MOD} {}

uint32_t Generator::next() {
    auto old_r = r;
    r = (r * GEN) % MOD;
    return old_r;
}
