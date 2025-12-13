/**
 * @file config.cpp
 * @brief Configuration loading for IPB Bridge
 */

#include "bridge.hpp"

#ifdef IPB_BRIDGE_HAS_YAML
#include <yaml-cpp/yaml.h>
#endif

#include <fstream>
#include <sstream>

namespace ipb::bridge {

#ifdef IPB_BRIDGE_HAS_YAML

namespace {

template <typename T>
T get_value(const YAML::Node& node, const std::string& key, T default_value) {
    if (node[key]) {
        try {
            return node[key].as<T>();
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

std::chrono::milliseconds get_ms(const YAML::Node& node, const std::string& key,
                                 std::chrono::milliseconds default_value) {
    if (node[key]) {
        try {
            return std::chrono::milliseconds(node[key].as<int64_t>());
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

}  // anonymous namespace

common::Result<BridgeConfig> load_config_yaml(const std::string& path) {
    try {
        YAML::Node root = YAML::LoadFile(path);

        BridgeConfig config;
        config.instance_id = get_value<std::string>(root, "instance_id", "");
        config.name        = get_value<std::string>(root, "name", "IPB Bridge");

        // Watchdog
        if (root["watchdog"]) {
            const auto& wd          = root["watchdog"];
            config.enable_watchdog  = get_value(wd, "enabled", true);
            config.watchdog_timeout = get_ms(wd, "timeout", std::chrono::milliseconds{30000});
        }

        // Forwarding
        if (root["forwarding"]) {
            const auto& fwd           = root["forwarding"];
            config.round_robin_sinks  = get_value(fwd, "round_robin", false);
            config.drop_on_sink_error = get_value(fwd, "drop_on_error", false);
        }

        // Limits
        if (root["limits"]) {
            const auto& lim       = root["limits"];
            config.max_sources    = get_value<uint32_t>(lim, "max_sources", 16);
            config.max_sinks      = get_value<uint32_t>(lim, "max_sinks", 8);
            config.max_queue_size = get_value<uint32_t>(lim, "max_queue_size", 1000);
        }

        // Logging
        config.log_level = get_value<std::string>(root, "log_level", "info");

        return common::Result<BridgeConfig>(std::move(config));

    } catch (const YAML::Exception& yaml_ex) {
        return common::Result<BridgeConfig>(common::ErrorCode::CONFIG_PARSE_ERROR,
                                            std::string("YAML error: ") + yaml_ex.what());
    } catch (const std::exception& ex) {
        return common::Result<BridgeConfig>(common::ErrorCode::OS_ERROR,
                                            std::string("Error loading config: ") + ex.what());
    }
}

#else

// Stub for builds without YAML support
common::Result<BridgeConfig> load_config_yaml(const std::string& /*path*/) {
    return common::Result<BridgeConfig>(common::ErrorCode::NOT_IMPLEMENTED,
                                        "YAML support not compiled in");
}

#endif

// Load configuration from file (auto-detect format)
common::Result<BridgeConfig> load_bridge_config(const std::string& path) {
    // Check file exists
    std::ifstream file(path);
    if (!file.good()) {
        return common::Result<BridgeConfig>(common::ErrorCode::NOT_FOUND,
                                            "Configuration file not found: " + path);
    }
    file.close();

    // Detect format by extension
    size_t dot = path.rfind('.');
    if (dot != std::string::npos) {
        std::string ext = path.substr(dot);
        if (ext == ".yaml" || ext == ".yml") {
            return load_config_yaml(path);
        }
        // JSON support could be added here
    }

    // Default to YAML
    return load_config_yaml(path);
}

}  // namespace ipb::bridge
