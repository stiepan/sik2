#include "utils.h"

ConversionError::ConversionError(char const * str)
        : std::runtime_error(str) {}

uint32_t str2uint32_t(std::string str) {
    uint32_t parsed;
    std::istringstream istr{str};
    istr >> parsed;
    if (istr.fail() || std::to_string(parsed) != str) {
        throw ConversionError("expected integer as an argument");
    }
    return parsed;
}
