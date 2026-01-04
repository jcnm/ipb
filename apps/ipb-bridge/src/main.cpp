/**
 * @file main.cpp
 * @brief IPB Bridge - Entry point
 *
 * Lightweight industrial protocol bridge for edge/embedded deployments.
 */

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <ipb/common/platform.hpp>

#include "getopt_compat.hpp"

#include "bridge.hpp"

#ifndef IPB_BRIDGE_VERSION
#define IPB_BRIDGE_VERSION "1.0.0"
#endif

namespace {

// Global bridge instance for signal handling
ipb::bridge::Bridge* g_bridge    = nullptr;
volatile sig_atomic_t g_shutdown = 0;

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        g_shutdown = 1;
        if (g_bridge) {
            g_bridge->stop();
        }
    }
#if defined(IPB_BRIDGE_WATCHDOG) && !defined(_WIN32)
    else if (signum == SIGALRM) {
        if (g_bridge) {
            g_bridge->feed_watchdog();
        }
    }
#endif
}

void print_version() {
    std::printf("IPB Bridge %s\n", IPB_BRIDGE_VERSION);
    std::printf("Industrial Protocol Bridge for Edge/Embedded\n");
#ifdef IPB_BRIDGE_MINIMAL
    std::printf("Build: Minimal\n");
#else
    std::printf("Build: Standard\n");
#endif
#ifdef IPB_BRIDGE_WATCHDOG
    std::printf("Features: Watchdog enabled\n");
#endif
}

void print_usage(const char* program) {
    std::printf("Usage: %s [OPTIONS]\n\n", program);
    std::printf("Options:\n");
    std::printf("  -c, --config <file>    Configuration file (YAML)\n");
    std::printf("  -t, --test             Test configuration and exit\n");
    std::printf("  -d, --daemon           Run as daemon\n");
    std::printf("  -v, --verbose          Increase verbosity\n");
    std::printf("  -q, --quiet            Decrease verbosity\n");
    std::printf("  -V, --version          Print version and exit\n");
    std::printf("  -h, --help             Print this help\n");
    std::printf("\n");
    std::printf("Examples:\n");
    std::printf("  %s -c /etc/ipb/bridge.yaml\n", program);
    std::printf("  %s -c bridge.yaml -t\n", program);
    std::printf("\n");
    std::printf("Environment:\n");
    std::printf("  IPB_CONFIG             Default configuration file path\n");
    std::printf("  IPB_LOG_LEVEL          Log level (trace,debug,info,warn,error)\n");
}

void print_stats(const ipb::bridge::BridgeStats& stats) {
    std::printf("\nBridge Statistics:\n");
    std::printf("  Messages received:  %lu\n",
                static_cast<unsigned long>(stats.messages_received.load()));
    std::printf("  Messages forwarded: %lu\n",
                static_cast<unsigned long>(stats.messages_forwarded.load()));
    std::printf("  Messages dropped:   %lu\n",
                static_cast<unsigned long>(stats.messages_dropped.load()));
    std::printf("  Errors:             %lu\n", static_cast<unsigned long>(stats.errors.load()));
    std::printf("  Uptime:             %lu seconds\n",
                static_cast<unsigned long>(stats.uptime_seconds.load()));
    std::printf("  Active sources:     %u\n", static_cast<unsigned>(stats.active_sources.load()));
    std::printf("  Active sinks:       %u\n", static_cast<unsigned>(stats.active_sinks.load()));
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
    // Command line options
    static struct option long_options[] = {
        {"config",  required_argument, nullptr, 'c'},
        {"test",    no_argument,       nullptr, 't'},
        {"daemon",  no_argument,       nullptr, 'd'},
        {"verbose", no_argument,       nullptr, 'v'},
        {"quiet",   no_argument,       nullptr, 'q'},
        {"version", no_argument,       nullptr, 'V'},
        {"help",    no_argument,       nullptr, 'h'},
        {nullptr,   0,                 nullptr, 0  }
    };

    std::string config_path;
    bool test_only   = false;
    bool daemon_mode = false;
    int verbosity    = 0;

    // Parse command line
    int opt;
    while ((opt = getopt_long(argc, argv, "c:tdvqVh", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'c':
                config_path = optarg;
                break;
            case 't':
                test_only = true;
                break;
            case 'd':
                daemon_mode = true;
                break;
            case 'v':
                verbosity++;
                break;
            case 'q':
                verbosity--;
                break;
            case 'V':
                print_version();
                return 0;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Check for config from environment
    if (config_path.empty()) {
        auto env_config = ipb::common::platform::get_env("IPB_CONFIG");
        if (!env_config.empty()) {
            config_path = env_config;
        }
    }

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
#ifndef _WIN32
    std::signal(SIGTERM, signal_handler);
#endif
#if defined(IPB_BRIDGE_WATCHDOG) && !defined(_WIN32)
    std::signal(SIGALRM, signal_handler);
#endif

    // Create bridge
    ipb::bridge::Bridge bridge;
    g_bridge = &bridge;

    // Initialize
    ipb::common::Result<void> result;

    if (!config_path.empty()) {
        if (verbosity >= 0) {
            std::printf("Loading configuration: %s\n", config_path.c_str());
        }
        result = bridge.initialize_from_file(config_path);
    } else {
        if (verbosity >= 0) {
            std::printf("Using default configuration\n");
        }
        ipb::bridge::BridgeConfig config;
        result = bridge.initialize(config);
    }

    if (!result) {
        std::fprintf(stderr, "Error: %s\n", result.error_message().c_str());
        return 1;
    }

    // Test mode - just validate and exit
    if (test_only) {
        std::printf("Configuration OK\n");
        return 0;
    }

    // Daemon mode
    if (daemon_mode) {
#ifdef __unix__
        pid_t pid = fork();
        if (pid < 0) {
            std::perror("fork");
            return 1;
        }
        if (pid > 0) {
            // Parent exits
            return 0;
        }
        // Child continues
        setsid();
        // Redirect standard streams to /dev/null for daemon mode
        // Use variables to suppress unused result warnings (GCC requires this)
        [[maybe_unused]] FILE* null_stdin  = std::freopen("/dev/null", "r", stdin);
        [[maybe_unused]] FILE* null_stdout = std::freopen("/dev/null", "w", stdout);
        [[maybe_unused]] FILE* null_stderr = std::freopen("/dev/null", "w", stderr);
#else
        std::fprintf(stderr, "Warning: daemon mode not supported on this platform\n");
#endif
    }

    // Start bridge
    result = bridge.start();
    if (!result) {
        std::fprintf(stderr, "Error starting bridge: %s\n", result.error_message().c_str());
        return 1;
    }

    if (verbosity >= 0 && !daemon_mode) {
        std::printf("IPB Bridge started\n");
        std::printf("Press Ctrl+C to stop\n\n");
    }

    // Main loop
    while (!g_shutdown && bridge.state() != ipb::bridge::BridgeState::STOPPED) {
        bridge.tick();

        // Feed watchdog periodically
#ifdef IPB_BRIDGE_WATCHDOG
        bridge.feed_watchdog();
#endif

        // Check health
        if (!bridge.is_healthy()) {
            if (verbosity >= 0) {
                std::fprintf(stderr, "Warning: Bridge not healthy\n");
            }
        }

        // Small sleep to prevent CPU spinning
        // In production, this would use proper event-driven I/O
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Shutdown
    bridge.stop();

    if (verbosity >= 0 && !daemon_mode) {
        print_stats(bridge.stats());
        std::printf("\nIPB Bridge stopped\n");
    }

    g_bridge = nullptr;
    return 0;
}
