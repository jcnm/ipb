/**
 * @file endpoint.cpp
 * @brief EndPoint implementation for non-inline methods
 */

#include <ipb/common/endpoint.hpp>

#include <algorithm>
#include <charconv>
#include <functional>

namespace ipb::common {

EndPoint EndPoint::from_url(std::string_view url) {
    EndPoint ep;

    if (url.empty()) {
        return ep;
    }

    // Parse protocol scheme
    auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return ep;
    }

    std::string_view scheme = url.substr(0, scheme_end);
    std::string_view rest   = url.substr(scheme_end + 3);

    // Determine protocol from scheme
    if (scheme == "tcp") {
        ep.set_protocol(Protocol::TCP);
    } else if (scheme == "udp") {
        ep.set_protocol(Protocol::UDP);
    } else if (scheme == "http") {
        ep.set_protocol(Protocol::HTTP);
    } else if (scheme == "https") {
        ep.set_protocol(Protocol::HTTPS);
        ep.set_security_level(SecurityLevel::TLS);
    } else if (scheme == "ws") {
        ep.set_protocol(Protocol::WEBSOCKET);
    } else if (scheme == "wss") {
        ep.set_protocol(Protocol::WEBSOCKET);
        ep.set_security_level(SecurityLevel::TLS);
    } else if (scheme == "mqtt") {
        ep.set_protocol(Protocol::MQTT);
    } else if (scheme == "mqtts") {
        ep.set_protocol(Protocol::MQTT);
        ep.set_security_level(SecurityLevel::TLS);
    } else if (scheme == "unix") {
        ep.set_protocol(Protocol::UNIX_SOCKET);
        ep.set_path(rest);
        return ep;
    } else if (scheme == "pipe") {
        ep.set_protocol(Protocol::NAMED_PIPE);
        ep.set_path(rest);
        return ep;
    } else if (scheme == "serial") {
        ep.set_protocol(Protocol::SERIAL);
        ep.set_path(rest);
        return ep;
    } else {
        ep.set_protocol(Protocol::CUSTOM);
    }

    // Parse credentials (user:pass@)
    auto at_pos = rest.find('@');
    if (at_pos != std::string_view::npos) {
        std::string_view credentials = rest.substr(0, at_pos);
        rest                         = rest.substr(at_pos + 1);

        auto colon_pos = credentials.find(':');
        if (colon_pos != std::string_view::npos) {
            ep.set_username(credentials.substr(0, colon_pos));
            ep.set_password(credentials.substr(colon_pos + 1));
        } else {
            ep.set_username(credentials);
        }
    }

    // Parse host and port
    auto path_start = rest.find('/');
    std::string_view host_port =
        (path_start != std::string_view::npos) ? rest.substr(0, path_start) : rest;

    if (path_start != std::string_view::npos) {
        ep.set_path(rest.substr(path_start));
    }

    // Handle IPv6 addresses [host]:port
    if (!host_port.empty() && host_port[0] == '[') {
        auto bracket_end = host_port.find(']');
        if (bracket_end != std::string_view::npos) {
            ep.set_host(host_port.substr(1, bracket_end - 1));

            if (bracket_end + 1 < host_port.size() && host_port[bracket_end + 1] == ':') {
                std::string_view port_str = host_port.substr(bracket_end + 2);
                uint16_t port             = 0;
                std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
                ep.set_port(port);
            }
        }
    } else {
        // Regular host:port
        auto port_pos = host_port.rfind(':');
        if (port_pos != std::string_view::npos) {
            ep.set_host(host_port.substr(0, port_pos));
            std::string_view port_str = host_port.substr(port_pos + 1);
            uint16_t port             = 0;
            std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
            ep.set_port(port);
        } else {
            ep.set_host(host_port);

            // Set default ports based on protocol
            switch (ep.protocol()) {
                case Protocol::HTTP:
                    ep.set_port(80);
                    break;
                case Protocol::HTTPS:
                    ep.set_port(443);
                    break;
                case Protocol::MQTT:
                    ep.set_port(ep.security_level() == SecurityLevel::TLS ? 8883 : 1883);
                    break;
                case Protocol::WEBSOCKET:
                    ep.set_port(ep.security_level() == SecurityLevel::TLS ? 443 : 80);
                    break;
                default:
                    break;
            }
        }
    }

    return ep;
}

size_t EndPoint::hash() const noexcept {
    size_t h = 0;

    // Combine hashes using FNV-1a style combination
    auto hash_combine = [](size_t& seed, size_t value) {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };

    hash_combine(h, std::hash<int>{}(static_cast<int>(protocol_)));
    hash_combine(h, std::hash<std::string>{}(host_));
    hash_combine(h, std::hash<uint16_t>{}(port_));
    hash_combine(h, std::hash<std::string>{}(path_));

    return h;
}

// Real-time namespace implementations
namespace rt {

bool CPUAffinity::set_thread_affinity([[maybe_unused]] std::thread::id thread_id,
                                      [[maybe_unused]] int cpu_id) noexcept {
#ifdef __linux__
    // Linux implementation would require converting thread::id to pthread_t
    // This is platform-specific and may not be portable
    return false;
#else
    return false;
#endif
}

bool CPUAffinity::set_current_thread_affinity([[maybe_unused]] int cpu_id) noexcept {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    return sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0;
#else
    return false;
#endif
}

int CPUAffinity::get_cpu_count() noexcept {
    return static_cast<int>(std::thread::hardware_concurrency());
}

std::vector<int> CPUAffinity::get_available_cpus() noexcept {
    std::vector<int> cpus;
    int count = get_cpu_count();
    cpus.reserve(count);
    for (int i = 0; i < count; ++i) {
        cpus.push_back(i);
    }
    return cpus;
}

bool CPUAffinity::isolate_cpu([[maybe_unused]] int cpu_id) noexcept {
#ifdef __linux__
    // Would require modifying kernel parameters or using cgroups
    // Not typically done at runtime
    return false;
#else
    return false;
#endif
}

bool ThreadPriority::set_thread_priority([[maybe_unused]] std::thread::id thread_id,
                                         [[maybe_unused]] Level priority) noexcept {
#ifdef __linux__
    // Linux implementation would require converting thread::id to pthread_t
    return false;
#else
    return false;
#endif
}

bool ThreadPriority::set_current_thread_priority([[maybe_unused]] Level priority) noexcept {
#ifdef __linux__
    struct sched_param param;
    int policy;

    if (priority == Level::REALTIME) {
        policy               = SCHED_FIFO;
        param.sched_priority = 99;
    } else if (priority >= Level::HIGH) {
        policy               = SCHED_FIFO;
        param.sched_priority = static_cast<int>(priority);
    } else {
        policy               = SCHED_OTHER;
        param.sched_priority = 0;
    }

    return pthread_setschedparam(pthread_self(), policy, &param) == 0;
#else
    return false;
#endif
}

bool ThreadPriority::set_realtime_priority([[maybe_unused]] std::thread::id thread_id,
                                           [[maybe_unused]] int priority) noexcept {
#ifdef __linux__
    return false;
#else
    return false;
#endif
}

bool ThreadPriority::set_current_realtime_priority([[maybe_unused]] int priority) noexcept {
#ifdef __linux__
    struct sched_param param;
    param.sched_priority = std::clamp(priority, 1, 99);
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
#else
    return false;
#endif
}

}  // namespace rt

}  // namespace ipb::common
