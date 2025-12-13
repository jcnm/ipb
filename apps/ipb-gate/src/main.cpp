#include "ipb/gate/orchestrator.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <csignal>
#include <cstdlib>
#include <getopt.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace {

// Version constant
constexpr const char* IPB_GATE_VERSION = "1.0.0";

// Global orchestrator instance for signal handling
std::unique_ptr<ipb::gate::IPBOrchestrator> g_orchestrator;

/**
 * @brief Signal handler for graceful shutdown
 */
void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", initiating graceful shutdown..." << std::endl;

    if (g_orchestrator && g_orchestrator->is_running()) {
        g_orchestrator->stop();
    }

    // Give some time for graceful shutdown
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (signal == SIGTERM || signal == SIGINT) {
        std::exit(0);
    }
}

/**
 * @brief Setup signal handlers
 */
void setup_signal_handlers() {
    std::signal(SIGINT, signal_handler);   // Ctrl+C
    std::signal(SIGTERM, signal_handler);  // Termination request
    std::signal(SIGHUP, signal_handler);   // Hangup (reload config)
    std::signal(SIGUSR1, signal_handler);  // User signal 1 (health check)
    std::signal(SIGUSR2, signal_handler);  // User signal 2 (metrics dump)
}

/**
 * @brief Print usage information
 */
void print_usage(const char* program_name) {
    std::cout << "IPB Gate - Industrial Protocol Bridge Gateway\n"
              << "Version: " << IPB_GATE_VERSION << "\n\n"
              << "Usage: " << program_name << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  -c, --config FILE     Configuration file path (required)\n"
              << "  -d, --daemon          Run as daemon\n"
              << "  -p, --pid-file FILE   PID file path (daemon mode)\n"
              << "  -l, --log-level LEVEL Log level (DEBUG, INFO, WARN, ERROR)\n"
              << "  -v, --verbose         Verbose output\n"
              << "  -q, --quiet           Quiet mode (errors only)\n"
              << "  -t, --test-config     Test configuration and exit\n"
              << "  -s, --status          Show system status\n"
              << "  -m, --metrics         Show performance metrics\n"
              << "  -h, --help            Show this help message\n"
              << "  --version             Show version information\n\n"
              << "Signals:\n"
              << "  SIGINT/SIGTERM        Graceful shutdown\n"
              << "  SIGHUP                Reload configuration\n"
              << "  SIGUSR1               Perform health check\n"
              << "  SIGUSR2               Dump metrics\n\n"
              << "Examples:\n"
              << "  " << program_name << " -c /etc/ipb/config.yaml\n"
              << "  " << program_name << " -c config.yaml -d -p /var/run/ipb-gate.pid\n"
              << "  " << program_name << " -c config.yaml -t\n"
              << std::endl;
}

/**
 * @brief Print version information
 */
void print_version() {
    std::cout << "IPB Gate " << IPB_GATE_VERSION << "\n"
              << "Industrial Protocol Bridge Gateway\n"
              << "Built with C++20, optimized for real-time performance\n"
              << "\nSupported protocols:\n"
              << "  - Modbus TCP/RTU\n"
              << "  - OPC UA\n"
              << "  - MQTT\n"
              << "\nSupported sinks:\n"
              << "  - Apache Kafka\n"
              << "  - ZeroMQ\n"
              << "  - Console\n"
              << "  - Syslog\n"
              << "\nFeatures:\n"
              << "  - EDF real-time scheduling\n"
              << "  - Lock-free data structures\n"
              << "  - Zero-copy optimizations\n"
              << "  - Hot configuration reload\n"
              << "  - Prometheus metrics\n"
              << "  - Comprehensive monitoring\n"
              << std::endl;
}

/**
 * @brief Check if file exists and is readable
 */
bool file_exists_and_readable(const std::string& file_path) {
    struct stat buffer;
    return (stat(file_path.c_str(), &buffer) == 0) && (access(file_path.c_str(), R_OK) == 0);
}

/**
 * @brief Create PID file for daemon mode
 */
bool create_pid_file(const std::string& pid_file_path) {
    std::ofstream pid_file(pid_file_path);
    if (!pid_file.is_open()) {
        std::cerr << "Error: Cannot create PID file: " << pid_file_path << std::endl;
        return false;
    }
    
    pid_file << getpid() << std::endl;
    pid_file.close();
    
    return true;
}

/**
 * @brief Remove PID file
 */
void remove_pid_file(const std::string& pid_file_path) {
    if (!pid_file_path.empty()) {
        std::remove(pid_file_path.c_str());
    }
}

/**
 * @brief Daemonize the process
 */
bool daemonize() {
    pid_t pid = fork();
    
    if (pid < 0) {
        std::cerr << "Error: Fork failed" << std::endl;
        return false;
    }
    
    if (pid > 0) {
        // Parent process exits
        std::exit(0);
    }
    
    // Child process continues
    if (setsid() < 0) {
        std::cerr << "Error: setsid failed" << std::endl;
        return false;
    }
    
    // Second fork to ensure we're not a session leader
    pid = fork();
    
    if (pid < 0) {
        std::cerr << "Error: Second fork failed" << std::endl;
        return false;
    }
    
    if (pid > 0) {
        std::exit(0);
    }
    
    // Change working directory to root
    if (chdir("/") < 0) {
        std::cerr << "Error: chdir failed" << std::endl;
        return false;
    }
    
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Redirect to /dev/null
    open("/dev/null", O_RDONLY); // stdin
    open("/dev/null", O_WRONLY); // stdout
    open("/dev/null", O_WRONLY); // stderr
    
    return true;
}

/**
 * @brief Test configuration file
 */
int test_configuration(const std::string& config_file_path) {
    std::cout << "Testing configuration file: " << config_file_path << std::endl;

    try {
        // Try to create and initialize an orchestrator with the config
        auto orchestrator = ipb::gate::OrchestratorFactory::create(config_file_path);
        if (!orchestrator) {
            std::cerr << "Error: Failed to create orchestrator" << std::endl;
            return 1;
        }

        auto init_result = orchestrator->initialize();
        if (!init_result.is_success()) {
            std::cerr << "Error: Configuration validation failed: "
                      << init_result.message() << std::endl;
            return 1;
        }

        std::cout << "Configuration is valid!" << std::endl;

        // Print configuration summary
        const auto& config = orchestrator->get_config();
        std::cout << "\nConfiguration Summary:" << std::endl;
        std::cout << "  Instance ID: " << config.instance_id << std::endl;
        std::cout << "  Log level: " << config.logging.level << std::endl;
        std::cout << "  Real-time scheduling: " << (config.scheduler.enable_realtime_priority ? "enabled" : "disabled") << std::endl;
        std::cout << "  Hot reload: " << (config.hot_reload.enabled ? "enabled" : "disabled") << std::endl;

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

/**
 * @brief Show system status
 */
int show_status(const std::string& config_file_path) {
    // This would typically connect to a running instance
    // For now, just show configuration info
    std::cout << "IPB Gate System Status" << std::endl;
    std::cout << "======================" << std::endl;

    try {
        auto orchestrator = ipb::gate::OrchestratorFactory::create(config_file_path);
        if (!orchestrator) {
            std::cerr << "Error: Cannot create orchestrator" << std::endl;
            return 1;
        }

        auto init_result = orchestrator->initialize();
        if (!init_result.is_success()) {
            std::cerr << "Error: Cannot load configuration: "
                      << init_result.message() << std::endl;
            return 1;
        }

        const auto& config = orchestrator->get_config();
        std::cout << "Configuration: " << config_file_path << std::endl;
        std::cout << "Instance: " << config.instance_id << std::endl;
        std::cout << "Status: Configuration loaded successfully" << std::endl;

        // TODO: Connect to running instance via IPC/socket to get real status
        std::cout << "\nNote: To get runtime status, the service must be running." << std::endl;

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

/**
 * @brief Show performance metrics
 */
int show_metrics(const std::string& config_file_path) {
    std::cout << "IPB Gate Performance Metrics" << std::endl;
    std::cout << "============================" << std::endl;
    
    // TODO: Connect to running instance to get real metrics
    std::cout << "Note: To get runtime metrics, the service must be running." << std::endl;
    std::cout << "Metrics would be available via Prometheus endpoint or IPC." << std::endl;
    
    return 0;
}

} // anonymous namespace

/**
 * @brief Main entry point
 */
int main(int argc, char* argv[]) {
    // Command line options
    std::string config_file_path;
    std::string pid_file_path;
    std::string log_level;
    bool daemon_mode = false;
    bool verbose = false;
    bool quiet = false;
    bool test_config = false;
    bool show_status_flag = false;
    bool show_metrics_flag = false;
    
    // Parse command line arguments
    static struct option long_options[] = {
        {"config",      required_argument, 0, 'c'},
        {"daemon",      no_argument,       0, 'd'},
        {"pid-file",    required_argument, 0, 'p'},
        {"log-level",   required_argument, 0, 'l'},
        {"verbose",     no_argument,       0, 'v'},
        {"quiet",       no_argument,       0, 'q'},
        {"test-config", no_argument,       0, 't'},
        {"status",      no_argument,       0, 's'},
        {"metrics",     no_argument,       0, 'm'},
        {"help",        no_argument,       0, 'h'},
        {"version",     no_argument,       0, 0},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "c:dp:l:vqtsmh", long_options, &option_index)) != -1) {
        switch (c) {
            case 'c':
                config_file_path = optarg;
                break;
            case 'd':
                daemon_mode = true;
                break;
            case 'p':
                pid_file_path = optarg;
                break;
            case 'l':
                log_level = optarg;
                break;
            case 'v':
                verbose = true;
                break;
            case 'q':
                quiet = true;
                break;
            case 't':
                test_config = true;
                break;
            case 's':
                show_status_flag = true;
                break;
            case 'm':
                show_metrics_flag = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 0:
                if (option_index == 10) { // --version
                    print_version();
                    return 0;
                }
                break;
            case '?':
                std::cerr << "Error: Unknown option. Use -h for help." << std::endl;
                return 1;
            default:
                break;
        }
    }
    
    // Validate required arguments
    if (config_file_path.empty()) {
        std::cerr << "Error: Configuration file is required. Use -c option." << std::endl;
        print_usage(argv[0]);
        return 1;
    }
    
    // Check if configuration file exists
    if (!file_exists_and_readable(config_file_path)) {
        std::cerr << "Error: Configuration file not found or not readable: " 
                  << config_file_path << std::endl;
        return 1;
    }
    
    // Handle special modes
    if (test_config) {
        return test_configuration(config_file_path);
    }
    
    if (show_status_flag) {
        return show_status(config_file_path);
    }
    
    if (show_metrics_flag) {
        return show_metrics(config_file_path);
    }
    
    // Setup signal handlers
    setup_signal_handlers();
    
    // Daemonize if requested
    if (daemon_mode) {
        if (!quiet) {
            std::cout << "Starting IPB Gate in daemon mode..." << std::endl;
        }
        
        if (!daemonize()) {
            std::cerr << "Error: Failed to daemonize" << std::endl;
            return 1;
        }
        
        // Create PID file
        if (!pid_file_path.empty()) {
            if (!create_pid_file(pid_file_path)) {
                return 1;
            }
        }
    }
    
    try {
        // Create orchestrator
        g_orchestrator = ipb::gate::OrchestratorFactory::create(config_file_path);
        if (!g_orchestrator) {
            std::cerr << "Error: Failed to create orchestrator" << std::endl;
            return 1;
        }
        
        if (!quiet) {
            std::cout << "IPB Gate starting..." << std::endl;
            std::cout << "Configuration: " << config_file_path << std::endl;
        }
        
        // Initialize orchestrator
        auto init_result = g_orchestrator->initialize();
        if (!init_result.is_success()) {
            std::cerr << "Error: Failed to initialize orchestrator: "
                      << init_result.message() << std::endl;
            return 1;
        }

        // Start orchestrator
        auto start_result = g_orchestrator->start();
        if (!start_result.is_success()) {
            std::cerr << "Error: Failed to start orchestrator: "
                      << start_result.message() << std::endl;
            return 1;
        }

        if (!quiet) {
            std::cout << "IPB Gate started successfully" << std::endl;
            if (verbose) {
                auto metrics = g_orchestrator->get_metrics();
                std::cout << "System metrics:" << std::endl;
                std::cout << "  Messages processed: " << metrics.messages_processed.load() << std::endl;
                std::cout << "  Router threads: " << g_orchestrator->get_config().router.worker_threads << std::endl;
                std::cout << "  RT scheduling: " << (g_orchestrator->get_config().scheduler.enable_realtime_priority ? "enabled" : "disabled") << std::endl;
            }
        }

        // Main loop - wait for shutdown signal
        while (g_orchestrator->is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Periodic health check in verbose mode
            if (verbose && !daemon_mode) {
                static auto last_health_check = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();

                if (now - last_health_check > std::chrono::seconds(10)) {
                    bool health = g_orchestrator->is_healthy();
                    std::cout << "System health: " << (health ? "OK" : "DEGRADED") << std::endl;
                    last_health_check = now;
                }
            }
        }

        if (!quiet) {
            std::cout << "IPB Gate shutting down..." << std::endl;
        }

        // Stop orchestrator
        auto stop_result = g_orchestrator->stop();
        if (!stop_result.is_success()) {
            std::cerr << "Warning: Error during shutdown: "
                      << stop_result.message() << std::endl;
        }

        // Final shutdown
        auto shutdown_result = g_orchestrator->shutdown();
        if (!shutdown_result.is_success()) {
            std::cerr << "Warning: Error during final shutdown: "
                      << shutdown_result.message() << std::endl;
        }
        
        if (!quiet) {
            std::cout << "IPB Gate stopped" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: Exception caught: " << e.what() << std::endl;
        
        // Cleanup PID file
        remove_pid_file(pid_file_path);
        
        return 1;
    } catch (...) {
        std::cerr << "Error: Unknown exception caught" << std::endl;
        
        // Cleanup PID file
        remove_pid_file(pid_file_path);
        
        return 1;
    }
    
    // Cleanup PID file
    remove_pid_file(pid_file_path);
    
    return 0;
}

