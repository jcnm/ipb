#pragma once

#include <string>

#include <sys/types.h>

namespace ipb {
namespace gate {

/**
 * @brief Utility class for daemon operations
 */
class DaemonUtils {
public:
    DaemonUtils();
    ~DaemonUtils();

    /**
     * @brief Daemonize the current process
     * @return true if successful, false otherwise
     */
    static bool daemonize();

    /**
     * @brief Write the current process ID to a file
     * @param pid_file Path to the PID file
     * @return true if successful, false otherwise
     */
    static bool write_pid_file(const std::string& pid_file);

    /**
     * @brief Remove the PID file
     * @param pid_file Path to the PID file
     * @return true if successful, false otherwise
     */
    static bool remove_pid_file(const std::string& pid_file);

    /**
     * @brief Read PID from file
     * @param pid_file Path to the PID file
     * @return PID if successful, -1 otherwise
     */
    static pid_t read_pid_file(const std::string& pid_file);

    /**
     * @brief Check if a process is running
     * @param pid Process ID to check
     * @return true if running, false otherwise
     */
    static bool is_process_running(pid_t pid);

    /**
     * @brief Create directory if it doesn't exist
     * @param path Directory path
     * @return true if successful or already exists, false otherwise
     */
    static bool create_directory(const std::string& path);
};

}  // namespace gate
}  // namespace ipb
