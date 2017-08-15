#include <cstdint>
#include <iostream>
#include <unistd.h>
#include <time.h>
#include "utils.h"
#include "game_state.h"


int main(int argc, char *argv[]) {


    /* Parsing arguments */
    uint32_t width = 800, height = 600,
            port = 12345, gspeed = 50, tspeed = 6,
            seed = static_cast<uint32_t >(time(NULL) % Generator::MOD);
    int opt;
    while ((opt = getopt(argc, argv, "W:H:p:s:t:r:")) != -1) {
        uint32_t parsed;
        if (optarg == NULL) {
            return 1;
        }
        try {
            parsed = str2uint32_t(optarg);
        } catch (ConversionError const &e) {
            std::cerr << static_cast<char>(opt) << ": " << e.what() << std::endl;
            return 1;
        }
        switch (opt) {
            case 'W':
                width = parsed;
                break;
            case 'H':
                height = parsed;
                break;
            case 'p':
                port = parsed;
                break;
            case 's':
                gspeed = parsed;
                break;
            case 't':
                tspeed = parsed;
                break;
            case 'r':
                seed = parsed;
                break;
            default:
                std::cerr << "Usage " << argv[0]
                          << " [-W n] [-H n] [-p n] [-s n] [-t n] [-r n]" << std::endl;
                return 1;
        }
    }

    return 0;
}
