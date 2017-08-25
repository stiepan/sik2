#include "utils.h"

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
    std::cout << "disarm " << old.it_interval.tv_nsec << " " << old.it_interval.tv_sec << std::endl;
    return true;
}

bool resume_timer(timer_t timer, itimerspec &resume)
{
    resume.it_value.tv_sec = resume.it_interval.tv_sec;
    resume.it_value.tv_nsec = resume.it_interval.tv_nsec;
    std::cout << "resume " << resume.it_interval.tv_nsec << " " << resume.it_interval.tv_sec << std::endl;
    if (timer_settime(timer, 0, &resume, NULL) == -1) {
        return false;
    }
    return true;
}
