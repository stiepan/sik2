#ifndef II_UTILS_H
#define II_UTILS_H
#include <string>
#include <sstream>
#include <exception>

class ConversionError : public std::runtime_error {
public:
    ConversionError(char const *);
};

uint32_t str2uint32_t(std::string);

#endif //II_UTILS_H
