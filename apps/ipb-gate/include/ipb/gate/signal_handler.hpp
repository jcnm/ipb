#pragma once

#include <atomic>

namespace ipb {
namespace gate {

/**
 * @brief Signal handler for graceful shutdown and configuration reload
 */
class SignalHandler {
public:
    SignalHandler();
    ~SignalHandler();

    /**
     * @brief Install signal handlers for the application
     */
    void install_handlers();

    /**
     * @brief Handle a received signal
     * @param signal The signal number
     */
    void handle_signal(int signal);

    /**
     * @brief Check if shutdown has been requested
     * @return true if shutdown was requested, false otherwise
     */
    bool is_shutdown_requested() const;

    /**
     * @brief Reset the shutdown request flag
     */
    void reset_shutdown_request();

private:
    std::atomic<bool> shutdown_requested_;
};

}  // namespace gate
}  // namespace ipb
