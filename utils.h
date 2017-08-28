#ifndef II_UTILS_H
#define II_UTILS_H
#include <iostream>

#include <string>
#include <sstream>
#include <exception>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

uint32_t const NANOSPERS = 1000000000;
size_t const MAX_FROM_SERVER_DATAGRAM_SIZE = 512;
size_t const MAX_FROM_CLIENT_DATAGRAM_SIZE = 77;
uint32_t const REQUIRED_PLAYERS = 2;

bool operator==(sockaddr_storage const &a1, sockaddr_storage const &a2);

/* Parsing and validation */

class UtilsError : public std::runtime_error {

public:
    UtilsError(char const *);
};

// https://stackoverflow.com/questions/809902/64-bit-ntohl-in-c
template <typename T>
constexpr T bswap (T value, char* ptr=0)
{
    return
#if __BYTE_ORDER == __LITTLE_ENDIAN
        ptr = reinterpret_cast<char*>(&value), std::reverse (ptr, ptr + sizeof(T)),
#endif
        value;
}

uint32_t str2uint32_t(std::string);

std::string last_err(std::string);

bool is_valid_port(uint32_t port_number);

/* Connections */

struct AddrInfo {
    addrinfo *info;
    std::string err;
    AddrInfo(char const * node, char const * port, addrinfo const &hints);
    ~AddrInfo();
};

struct Socket {
    int fd;
    Socket();
    Socket(Socket &&);
    Socket& operator=(Socket &&);
    ~Socket();
};

void create_timer(timer_t &timer, uint64_t nanosecs, void (*handler)(int, siginfo_t *, void *));
bool disarm_timer(timer_t timer, itimerspec &old);
bool resume_timer(timer_t timer, itimerspec &resume);

uint64_t milliseconds_since_epoch();

#endif //II_UTILS_H
