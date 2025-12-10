#include <ipb/gate/signal_handler.hpp>
#include <iostream>
#include <csignal>

namespace ipb {
namespace gate {

// Global signal handler instance
static SignalHandler* g_signal_handler = nullptr;

// C-style signal handler function
extern "C" void signal_handler_function(int signal) {
    if (g_signal_handler) {
        g_signal_handler->handle_signal(signal);
    }
}

SignalHandler::SignalHandler() 
    : shutdown_requested_(false)
{
    g_signal_handler = this;
}

SignalHandler::~SignalHandler() {
    g_signal_handler = nullptr;
}

void SignalHandler::install_handlers() {
    // Install signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler_function);   // Ctrl+C
    std::signal(SIGTERM, signal_handler_function);  // Termination request
    
    #ifndef _WIN32
    std::signal(SIGHUP, signal_handler_function);   // Hangup (reload config)
    std::signal(SIGUSR1, signal_handler_function);  // User signal 1
    std::signal(SIGUSR2, signal_handler_function);  // User signal 2
    #endif
}

void SignalHandler::handle_signal(int signal) {
    switch (signal) {
        case SIGINT:
        case SIGTERM:
            std::cout << "\nReceived shutdown signal (" << signal << "). Initiating graceful shutdown..." << std::endl;
            shutdown_requested_ = true;
            break;
            
        #ifndef _WIN32
        case SIGHUP:
            std::cout << "Received SIGHUP. Reloading configuration..." << std::endl;
            // TODO: Implement config reload
            break;
            
        case SIGUSR1:
            std::cout << "Received SIGUSR1. Printing statistics..." << std::endl;
            // TODO: Implement statistics printing
            break;
            
        case SIGUSR2:
            std::cout << "Received SIGUSR2. Toggling debug mode..." << std::endl;
            // TODO: Implement debug mode toggle
            break;
        #endif
            
        default:
            std::cout << "Received unknown signal: " << signal << std::endl;
            break;
    }
}

bool SignalHandler::is_shutdown_requested() const {
    return shutdown_requested_;
}

void SignalHandler::reset_shutdown_request() {
    shutdown_requested_ = false;
}

} // namespace gate
} // namespace ipb

