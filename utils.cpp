#include "utils.h"

// inspired by https://opensource.apple.com/source/postfix/postfix-197/postfix/src/util/sock_addr.c
bool operator==(sockaddr_storage const &a1, sockaddr_storage const &a2)
{
    if (a1.ss_family != a2.ss_family) {
        return false;
    }
    switch (a1.ss_family) {
        case AF_INET: {
            auto *pa1 = reinterpret_cast<sockaddr_in const *>(&a1);
            auto *pa2 = reinterpret_cast<sockaddr_in const *>(&a2);
            return pa1->sin_port == pa2->sin_port && pa1->sin_addr.s_addr == pa2->sin_addr.s_addr;
        }
        case AF_INET6: {
            auto *pa1 = reinterpret_cast<sockaddr_in6 const *>(&a1);
            auto *pa2 = reinterpret_cast<sockaddr_in6 const *>(&a2);
            return pa1->sin6_port == pa2->sin6_port && (memcmp(
                    reinterpret_cast<char const *>(&pa1->sin6_addr.s6_addr),
                    reinterpret_cast<char const *>(&pa2->sin6_addr.s6_addr),
                    sizeof(pa1->sin6_addr.s6_addr)) == 0);
        }
        default:
            std::cerr << "Unknown connection type" << std::endl;
            return true;
    }
}

UtilsError::UtilsError(char const * str) : std::runtime_error(str) {}

uint32_t str2uint32_t(std::string str)
{
    uint32_t parsed;
    std::istringstream istr{str};
    istr >> parsed;
    if (istr.fail() || std::to_string(parsed) != str) {
        throw UtilsError("expected integer as an argument");
    }
    return parsed;
}

bool is_valid_port(uint32_t port_number)
{
    return port_number <= 65535;
}

std::string last_err(std::string pref="")
{
    std::stringstream ss;
    ss << pref << strerror(errno);
    return ss.str();
}

Socket::Socket() : fd{-1} {}

Socket& Socket::operator=(Socket &&socket)
{
    fd = socket.fd;
    socket.fd = -1;
    return *this;
}

Socket::~Socket()
{
    if (fd != -1) {
        close(fd);
    }
}

AddrInfo::AddrInfo(char const * node, char const * port, addrinfo const &hints): info{nullptr}
{
    int rv;
    if ((rv = getaddrinfo(node, port, &hints, &info)) != 0) {
        err = gai_strerror(rv);
        info = nullptr;
    }
}

AddrInfo::~AddrInfo()
{
    if (info) {
        freeaddrinfo(info);
    }
}

// Based on man 2 timer_create
void create_timer(timer_t &timer, uint64_t nanosecs, void (*handler)(int, siginfo_t *, void *))
{
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGRTMIN, &sa, NULL) == -1) {
        throw UtilsError("Could not set timer signal handler");
    }

    /* Create the timer */
    sigevent sev;
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = &timer;
    if (timer_create(CLOCK_REALTIME, &sev, &timer) == -1) {
        throw UtilsError("Could not create the timer");
    }

    /* Start the timer */
    itimerspec its;
    its.it_value.tv_sec = nanosecs / NANOSPERS;
    its.it_value.tv_nsec = nanosecs % NANOSPERS;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    if (timer_settime(timer, 0, &its, NULL) == -1) {
        throw UtilsError("Could not start the timer");
    }
}

bool disarm_timer(timer_t timer, itimerspec &old)
{
    itimerspec its;
    if(timer_gettime(timer, &its) == -1) {
        return false;
    }
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 0;
    if (timer_settime(timer, 0, &its, &old) == -1) {
        return false;
    }
    return true;
}

bool resume_timer(timer_t timer, itimerspec &resume)
{
    resume.it_value.tv_sec = resume.it_interval.tv_sec;
    resume.it_value.tv_nsec = resume.it_interval.tv_nsec;
    if (timer_settime(timer, 0, &resume, NULL) == -1) {
        return false;
    }
    return true;
}

uint64_t milliseconds_since_epoch()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return UINT64_C(1000) * tv.tv_sec + tv.tv_usec / UINT64_C(1000);
}


