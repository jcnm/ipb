#include <ipb/gate/daemon_utils.hpp>

#include <csignal>
#include <fstream>
#include <iostream>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ipb {
namespace gate {

DaemonUtils::DaemonUtils() = default;

DaemonUtils::~DaemonUtils() = default;

bool DaemonUtils::daemonize() {
#ifdef _WIN32
    // Windows doesn't support traditional Unix daemonization
    std::cerr << "Daemonization not supported on Windows" << std::endl;
    return false;
#else

    // Fork the first time
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "First fork failed" << std::endl;
        return false;
    }

    // Exit parent process
    if (pid > 0) {
        exit(0);
    }

    // Create new session
    if (setsid() < 0) {
        std::cerr << "setsid failed" << std::endl;
        return false;
    }

    // Fork the second time
    pid = fork();
    if (pid < 0) {
        std::cerr << "Second fork failed" << std::endl;
        return false;
    }

    // Exit first child
    if (pid > 0) {
        exit(0);
    }

    // Change working directory to root
    if (chdir("/") < 0) {
        std::cerr << "chdir to / failed" << std::endl;
        return false;
    }

    // Set file permissions
    // SECURITY: umask(027) ensures files are created with secure permissions
    // Owner: rwx (7), Group: rx (5), Others: none (0)
    // This prevents world-readable files which could expose sensitive data
    umask(027);

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect standard file descriptors to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) {
            close(fd);
        }
    }

    return true;
#endif
}

bool DaemonUtils::write_pid_file(const std::string& pid_file) {
    std::ofstream file(pid_file);
    if (!file.is_open()) {
        std::cerr << "Failed to open PID file: " << pid_file << std::endl;
        return false;
    }

    file << getpid() << std::endl;
    file.close();

    return true;
}

bool DaemonUtils::remove_pid_file(const std::string& pid_file) {
    if (unlink(pid_file.c_str()) != 0) {
        std::cerr << "Failed to remove PID file: " << pid_file << std::endl;
        return false;
    }
    return true;
}

pid_t DaemonUtils::read_pid_file(const std::string& pid_file) {
    std::ifstream file(pid_file);
    if (!file.is_open()) {
        return -1;
    }

    pid_t pid;
    file >> pid;
    file.close();

    return pid;
}

bool DaemonUtils::is_process_running(pid_t pid) {
#ifdef _WIN32
    // Windows implementation would be different
    return false;
#else
    return (kill(pid, 0) == 0);
#endif
}

bool DaemonUtils::create_directory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

#ifdef _WIN32
    return _mkdir(path.c_str()) == 0;
#else
    return mkdir(path.c_str(), 0755) == 0;
#endif
}

}  // namespace gate
}  // namespace ipb
