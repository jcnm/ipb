/**
 * @file getopt_compat.hpp
 * @brief Cross-platform getopt compatibility header
 *
 * Provides getopt_long functionality on Windows and uses the system
 * implementation on POSIX systems.
 */

#ifndef IPB_GETOPT_COMPAT_HPP
#define IPB_GETOPT_COMPAT_HPP

#ifdef _WIN32

// Windows implementation of getopt_long

#include <cstring>

// Option argument types
#define no_argument 0
#define required_argument 1
#define optional_argument 2

// Global variables (matching POSIX getopt)
extern "C" {
inline char* optarg = nullptr;
inline int optind   = 1;
inline int opterr   = 1;
inline int optopt   = 0;
}

struct option {
    const char* name;
    int has_arg;
    int* flag;
    int val;
};

/**
 * @brief Windows-compatible implementation of getopt_long
 *
 * This is a minimal implementation that handles the common use cases.
 */
inline int getopt_long(int argc, char* const argv[], const char* optstring, const struct option* longopts,
                       int* longindex) {
    static int optpos = 1;

    // Reset if we've processed all arguments
    if (optind >= argc || argv[optind] == nullptr) {
        return -1;
    }

    const char* arg = argv[optind];

    // Check for end of options
    if (arg[0] != '-' || arg[1] == '\0') {
        return -1;
    }

    // Check for "--" (end of options marker)
    if (arg[1] == '-' && arg[2] == '\0') {
        optind++;
        return -1;
    }

    // Handle long options (--option)
    if (arg[1] == '-') {
        const char* longopt = arg + 2;

        // Find '=' for attached argument
        const char* eq = std::strchr(longopt, '=');
        size_t namelen = eq ? static_cast<size_t>(eq - longopt) : std::strlen(longopt);

        // Search for matching long option
        for (int i = 0; longopts && longopts[i].name; i++) {
            if (std::strncmp(longopts[i].name, longopt, namelen) == 0 &&
                std::strlen(longopts[i].name) == namelen) {
                if (longindex) {
                    *longindex = i;
                }

                optind++;

                if (longopts[i].has_arg == required_argument) {
                    if (eq) {
                        optarg = const_cast<char*>(eq + 1);
                    } else if (optind < argc) {
                        optarg = argv[optind++];
                    } else {
                        if (opterr) {
                            std::fprintf(stderr, "%s: option '--%s' requires an argument\n", argv[0],
                                         longopts[i].name);
                        }
                        return '?';
                    }
                } else if (longopts[i].has_arg == optional_argument) {
                    optarg = eq ? const_cast<char*>(eq + 1) : nullptr;
                } else {
                    optarg = nullptr;
                }

                if (longopts[i].flag) {
                    *longopts[i].flag = longopts[i].val;
                    return 0;
                }
                return longopts[i].val;
            }
        }

        if (opterr) {
            std::fprintf(stderr, "%s: unrecognized option '--%.*s'\n", argv[0], static_cast<int>(namelen),
                         longopt);
        }
        optopt = 0;
        optind++;
        return '?';
    }

    // Handle short options (-x)
    char opt = arg[optpos];

    // Find option in optstring
    const char* match = std::strchr(optstring, opt);
    if (!match) {
        if (opterr) {
            std::fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], opt);
        }
        optopt = opt;

        // Move to next option or argument
        if (arg[optpos + 1] == '\0') {
            optind++;
            optpos = 1;
        } else {
            optpos++;
        }
        return '?';
    }

    // Check if option requires an argument
    if (match[1] == ':') {
        if (arg[optpos + 1] != '\0') {
            // Argument attached to option (-xARG)
            optarg = const_cast<char*>(&arg[optpos + 1]);
            optind++;
            optpos = 1;
        } else if (optind + 1 < argc) {
            // Argument is next argv entry (-x ARG)
            optarg = argv[optind + 1];
            optind += 2;
            optpos = 1;
        } else {
            if (opterr) {
                std::fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], opt);
            }
            optopt = opt;
            optind++;
            optpos = 1;
            return optstring[0] == ':' ? ':' : '?';
        }
    } else {
        optarg = nullptr;

        // Move to next option character or next argument
        if (arg[optpos + 1] == '\0') {
            optind++;
            optpos = 1;
        } else {
            optpos++;
        }
    }

    return opt;
}

#else

// POSIX systems: use the system getopt
#include <getopt.h>

#endif  // _WIN32

#endif  // IPB_GETOPT_COMPAT_HPP
