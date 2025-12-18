/**
 * @file config_loader_embedded.cpp
 * @brief Lightweight configuration loader for embedded systems
 *
 * Uses:
 * - rapidyaml (ryml) for YAML parsing - fast, header-friendly, low memory
 * - cJSON for JSON parsing - minimal C library
 */

#include <ipb/core/config/config_loader_embedded.hpp>

// Rapidyaml (ryml) - lightweight YAML parser
#ifdef IPB_CONFIG_USE_RYML
#include <ryml.hpp>
#include <ryml_std.hpp>
#endif

// cJSON - minimal JSON parser
#ifdef IPB_CONFIG_USE_CJSON
#include <cjson/cJSON.h>
#endif

// Fallback to standard libraries if embedded libs not available
// NOTE: In EMBEDDED build mode (IPB_BUILD_EMBEDDED), we don't include these
// fallbacks since yaml-cpp/jsoncpp aren't available
#if !defined(IPB_CONFIG_USE_RYML) && !defined(IPB_BUILD_EMBEDDED)
#include <yaml-cpp/yaml.h>
#endif

#if !defined(IPB_CONFIG_USE_CJSON) && !defined(IPB_BUILD_EMBEDDED)
#include <json/json.h>
#endif

#include <algorithm>
#include <chrono>
#include <fstream>

namespace ipb::core::config {

// ============================================================================
// MEMORY TRACKING
// ============================================================================

namespace {

thread_local size_t g_current_memory = 0;
thread_local size_t g_peak_memory    = 0;

void reset_memory_tracking() {
    g_current_memory = 0;
    g_peak_memory    = 0;
}

void track_allocation(size_t size) {
    g_current_memory += size;
    if (g_current_memory > g_peak_memory) {
        g_peak_memory = g_current_memory;
    }
}

void track_deallocation(size_t size) {
    if (size <= g_current_memory) {
        g_current_memory -= size;
    }
}

}  // anonymous namespace

// ============================================================================
// RAPIDYAML PARSING HELPERS
// ============================================================================

#ifdef IPB_CONFIG_USE_RYML

namespace {

template <typename T>
T ryml_get(const ryml::ConstNodeRef& node, const char* key, T default_value) {
    if (!node.valid() || !node.is_map()) {
        return default_value;
    }
    if (!node.has_child(ryml::to_csubstr(key))) {
        return default_value;
    }
    auto child = node[ryml::to_csubstr(key)];
    if (!child.valid() || !child.has_val()) {
        return default_value;
    }

    T result;
    child >> result;
    return result;
}

template <>
std::string ryml_get<std::string>(const ryml::ConstNodeRef& node, const char* key,
                                  std::string default_value) {
    if (!node.valid() || !node.is_map()) {
        return default_value;
    }
    if (!node.has_child(ryml::to_csubstr(key))) {
        return default_value;
    }
    auto child = node[ryml::to_csubstr(key)];
    if (!child.valid() || !child.has_val()) {
        return default_value;
    }

    auto val = child.val();
    return std::string(val.str, val.len);
}

template <>
bool ryml_get<bool>(const ryml::ConstNodeRef& node, const char* key, bool default_value) {
    if (!node.valid() || !node.is_map()) {
        return default_value;
    }
    if (!node.has_child(ryml::to_csubstr(key))) {
        return default_value;
    }
    auto child = node[ryml::to_csubstr(key)];
    if (!child.valid() || !child.has_val()) {
        return default_value;
    }

    auto val = child.val();
    std::string str(val.str, val.len);
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str == "true" || str == "yes" || str == "1" || str == "on";
}

std::vector<std::string> ryml_get_string_array(const ryml::ConstNodeRef& node, const char* key) {
    std::vector<std::string> result;
    if (!node.valid() || !node.is_map()) {
        return result;
    }
    if (!node.has_child(ryml::to_csubstr(key))) {
        return result;
    }
    auto child = node[ryml::to_csubstr(key)];
    if (!child.valid() || !child.is_seq()) {
        return result;
    }

    for (const auto& item : child) {
        if (item.has_val()) {
            auto val = item.val();
            result.emplace_back(val.str, val.len);
        }
    }
    return result;
}

std::map<std::string, std::string> ryml_get_string_map(const ryml::ConstNodeRef& node,
                                                       const char* key) {
    std::map<std::string, std::string> result;
    if (!node.valid() || !node.is_map()) {
        return result;
    }
    if (!node.has_child(ryml::to_csubstr(key))) {
        return result;
    }
    auto child = node[ryml::to_csubstr(key)];
    if (!child.valid() || !child.is_map()) {
        return result;
    }

    for (const auto& item : child) {
        if (item.has_key() && item.has_val()) {
            auto k                            = item.key();
            auto v                            = item.val();
            result[std::string(k.str, k.len)] = std::string(v.str, v.len);
        }
    }
    return result;
}

// Forward declarations for ryml parsers
ScoopConfig parse_scoop_config_ryml(const ryml::ConstNodeRef& node);
SinkConfig parse_sink_config_ryml(const ryml::ConstNodeRef& node);
RouterConfig parse_router_config_ryml(const ryml::ConstNodeRef& node);
LoggingConfig parse_logging_config_ryml(const ryml::ConstNodeRef& node);
MetricsConfig parse_metrics_config_ryml(const ryml::ConstNodeRef& node);
SecurityConfig parse_security_config_ryml(const ryml::ConstNodeRef& node);
SchedulerConfig parse_scheduler_config_ryml(const ryml::ConstNodeRef& node);

LoggingConfig parse_logging_config_ryml(const ryml::ConstNodeRef& node) {
    LoggingConfig config;
    if (!node.valid())
        return config;

    config.level          = ryml_get<std::string>(node, "level", "info");
    config.format         = ryml_get<std::string>(node, "format", "text");
    config.file_path      = ryml_get<std::string>(node, "file_path", "");
    config.max_file_size  = ryml_get<size_t>(node, "max_file_size", 10 * 1024 * 1024);
    config.max_files      = ryml_get<size_t>(node, "max_files", 5);
    config.enable_console = ryml_get<bool>(node, "enable_console", true);
    config.enable_file    = ryml_get<bool>(node, "enable_file", false);

    return config;
}

MetricsConfig parse_metrics_config_ryml(const ryml::ConstNodeRef& node) {
    MetricsConfig config;
    if (!node.valid())
        return config;

    config.enabled                = ryml_get<bool>(node, "enabled", true);
    config.collection_interval_ms = ryml_get<uint32_t>(node, "collection_interval_ms", 1000);
    config.history_size           = ryml_get<size_t>(node, "history_size", 1000);
    config.enable_histograms      = ryml_get<bool>(node, "enable_histograms", true);

    return config;
}

SecurityConfig parse_security_config_ryml(const ryml::ConstNodeRef& node) {
    SecurityConfig config;
    if (!node.valid())
        return config;

    config.enable_tls  = ryml_get<bool>(node, "enable_tls", false);
    config.cert_file   = ryml_get<std::string>(node, "cert_file", "");
    config.key_file    = ryml_get<std::string>(node, "key_file", "");
    config.ca_file     = ryml_get<std::string>(node, "ca_file", "");
    config.verify_peer = ryml_get<bool>(node, "verify_peer", true);

    return config;
}

SchedulerConfig parse_scheduler_config_ryml(const ryml::ConstNodeRef& node) {
    SchedulerConfig config;
    if (!node.valid())
        return config;

    config.enabled                  = ryml_get<bool>(node, "enabled", true);
    config.enable_realtime_priority = ryml_get<bool>(node, "enable_realtime_priority", false);
    config.realtime_priority        = ryml_get<int>(node, "realtime_priority", 50);
    config.worker_threads           = ryml_get<size_t>(node, "worker_threads", 0);
    config.max_tasks                = ryml_get<size_t>(node, "max_tasks", 10000);
    config.preemptive               = ryml_get<bool>(node, "preemptive", true);

    auto deadline_us        = ryml_get<uint64_t>(node, "default_deadline_us", 1000);
    config.default_deadline = std::chrono::microseconds(deadline_us);

    auto watchdog_ms        = ryml_get<uint64_t>(node, "watchdog_timeout_ms", 5000);
    config.watchdog_timeout = std::chrono::milliseconds(watchdog_ms);

    return config;
}

RouteFilterConfig parse_route_filter_ryml(const ryml::ConstNodeRef& node) {
    RouteFilterConfig config;
    if (!node.valid())
        return config;

    config.address_pattern     = ryml_get<std::string>(node, "address_pattern", "");
    config.protocol_ids        = ryml_get_string_array(node, "protocol_ids");
    config.quality_levels      = ryml_get_string_array(node, "quality_levels");
    config.tags                = ryml_get_string_array(node, "tags");
    config.enable_value_filter = ryml_get<bool>(node, "enable_value_filter", false);
    config.value_condition     = ryml_get<std::string>(node, "value_condition", "");

    return config;
}

RouteDestinationConfig parse_route_destination_ryml(const ryml::ConstNodeRef& node) {
    RouteDestinationConfig config;
    if (!node.valid())
        return config;

    config.sink_id       = ryml_get<std::string>(node, "sink_id", "");
    config.priority      = ryml_get<uint32_t>(node, "priority", 0);
    config.weight        = ryml_get<uint32_t>(node, "weight", 100);
    config.failover_only = ryml_get<bool>(node, "failover_only", false);

    return config;
}

RouteConfig parse_route_config_ryml(const ryml::ConstNodeRef& node) {
    RouteConfig config;
    if (!node.valid())
        return config;

    config.id       = ryml_get<std::string>(node, "id", "");
    config.name     = ryml_get<std::string>(node, "name", "");
    config.enabled  = ryml_get<bool>(node, "enabled", true);
    config.priority = ryml_get<uint32_t>(node, "priority", 0);

    // Legacy fields
    config.source_pattern       = ryml_get<std::string>(node, "source_pattern", "");
    config.sink_ids             = ryml_get_string_array(node, "sink_ids");
    config.transform_expression = ryml_get<std::string>(node, "transform_expression", "");

    // Enhanced filter
    if (node.has_child("filter")) {
        config.filter = parse_route_filter_ryml(node["filter"]);
    }

    // Destinations
    if (node.has_child("destinations")) {
        auto dests = node["destinations"];
        if (dests.is_seq()) {
            for (const auto& dest : dests) {
                config.destinations.push_back(parse_route_destination_ryml(dest));
            }
        }
    }

    return config;
}

RouterConfig parse_router_config_ryml(const ryml::ConstNodeRef& node) {
    RouterConfig config;
    if (!node.valid())
        return config;

    config.id                    = ryml_get<std::string>(node, "id", "default-router");
    config.worker_threads        = ryml_get<size_t>(node, "worker_threads", 4);
    config.queue_size            = ryml_get<size_t>(node, "queue_size", 10000);
    config.batch_size            = ryml_get<size_t>(node, "batch_size", 100);
    config.routing_table_size    = ryml_get<size_t>(node, "routing_table_size", 1000);
    config.enable_metrics        = ryml_get<bool>(node, "enable_metrics", true);
    config.enable_load_balancing = ryml_get<bool>(node, "enable_load_balancing", false);

    auto timeout_ms        = ryml_get<uint64_t>(node, "default_timeout_ms", 5000);
    config.default_timeout = std::chrono::milliseconds(timeout_ms);

    // Routes
    if (node.has_child("routes")) {
        auto routes = node["routes"];
        if (routes.is_seq()) {
            for (const auto& route : routes) {
                config.routes.push_back(parse_route_config_ryml(route));
            }
        }
    }

    return config;
}

ScoopConfig parse_scoop_config_ryml(const ryml::ConstNodeRef& node) {
    ScoopConfig config;
    if (!node.valid())
        return config;

    config.id      = ryml_get<std::string>(node, "id", "");
    config.name    = ryml_get<std::string>(node, "name", "");
    config.type    = ryml_get<std::string>(node, "type", "");
    config.enabled = ryml_get<bool>(node, "enabled", true);

    auto poll_ms         = ryml_get<uint64_t>(node, "poll_interval_ms", 1000);
    config.poll_interval = std::chrono::milliseconds(poll_ms);

    auto timeout_ms = ryml_get<uint64_t>(node, "timeout_ms", 5000);
    config.timeout  = std::chrono::milliseconds(timeout_ms);

    config.retry_count = ryml_get<uint32_t>(node, "retry_count", 3);
    config.buffer_size = ryml_get<size_t>(node, "buffer_size", 1000);

    // Connection parameters
    if (node.has_child("connection")) {
        config.connection_params = ryml_get_string_map(node, "connection");
    }

    // Custom parameters
    if (node.has_child("parameters")) {
        config.custom_params = ryml_get_string_map(node, "parameters");
    }

    return config;
}

SinkConfig parse_sink_config_ryml(const ryml::ConstNodeRef& node) {
    SinkConfig config;
    if (!node.valid())
        return config;

    config.id      = ryml_get<std::string>(node, "id", "");
    config.name    = ryml_get<std::string>(node, "name", "");
    config.type    = ryml_get<std::string>(node, "type", "");
    config.enabled = ryml_get<bool>(node, "enabled", true);

    auto timeout_ms = ryml_get<uint64_t>(node, "timeout_ms", 5000);
    config.timeout  = std::chrono::milliseconds(timeout_ms);

    config.retry_count = ryml_get<uint32_t>(node, "retry_count", 3);
    config.buffer_size = ryml_get<size_t>(node, "buffer_size", 1000);
    config.batch_size  = ryml_get<size_t>(node, "batch_size", 100);

    auto flush_ms         = ryml_get<uint64_t>(node, "flush_interval_ms", 1000);
    config.flush_interval = std::chrono::milliseconds(flush_ms);

    // Connection parameters
    if (node.has_child("connection")) {
        config.connection_params = ryml_get_string_map(node, "connection");
    }

    // Custom parameters
    if (node.has_child("parameters")) {
        config.custom_params = ryml_get_string_map(node, "parameters");
    }

    return config;
}

ApplicationConfig parse_application_config_ryml(const ryml::ConstNodeRef& root) {
    ApplicationConfig config;

    config.name        = ryml_get<std::string>(root, "name", "ipb-gateway");
    config.version     = ryml_get<std::string>(root, "version", "1.0.0");
    config.instance_id = ryml_get<std::string>(root, "instance_id", "");

    // Logging
    if (root.has_child("logging")) {
        config.logging = parse_logging_config_ryml(root["logging"]);
    }

    // Metrics
    if (root.has_child("metrics")) {
        config.metrics = parse_metrics_config_ryml(root["metrics"]);
    }

    // Security
    if (root.has_child("security")) {
        config.security = parse_security_config_ryml(root["security"]);
    }

    // Scheduler
    if (root.has_child("scheduler")) {
        config.scheduler = parse_scheduler_config_ryml(root["scheduler"]);
    }

    // Router
    if (root.has_child("router")) {
        config.router = parse_router_config_ryml(root["router"]);
    }

    // Scoops
    if (root.has_child("scoops")) {
        auto scoops = root["scoops"];
        if (scoops.is_seq()) {
            for (const auto& scoop : scoops) {
                config.scoops.push_back(parse_scoop_config_ryml(scoop));
            }
        }
    }

    // Sinks
    if (root.has_child("sinks")) {
        auto sinks = root["sinks"];
        if (sinks.is_seq()) {
            for (const auto& sink : sinks) {
                config.sinks.push_back(parse_sink_config_ryml(sink));
            }
        }
    }

    return config;
}

}  // anonymous namespace

#endif  // IPB_CONFIG_USE_RYML

// ============================================================================
// CJSON PARSING HELPERS
// ============================================================================

#ifdef IPB_CONFIG_USE_CJSON

namespace {

template <typename T>
T cjson_get(const cJSON* node, const char* key, T default_value);

template <>
std::string cjson_get<std::string>(const cJSON* node, const char* key, std::string default_value) {
    if (!node)
        return default_value;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(node, key);
    if (!item || !cJSON_IsString(item))
        return default_value;
    return item->valuestring ? item->valuestring : default_value;
}

template <>
bool cjson_get<bool>(const cJSON* node, const char* key, bool default_value) {
    if (!node)
        return default_value;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(node, key);
    if (!item)
        return default_value;
    if (cJSON_IsBool(item))
        return cJSON_IsTrue(item);
    return default_value;
}

template <>
int cjson_get<int>(const cJSON* node, const char* key, int default_value) {
    if (!node)
        return default_value;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(node, key);
    if (!item || !cJSON_IsNumber(item))
        return default_value;
    return item->valueint;
}

template <>
uint32_t cjson_get<uint32_t>(const cJSON* node, const char* key, uint32_t default_value) {
    return static_cast<uint32_t>(cjson_get<int>(node, key, static_cast<int>(default_value)));
}

template <>
size_t cjson_get<size_t>(const cJSON* node, const char* key, size_t default_value) {
    if (!node)
        return default_value;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(node, key);
    if (!item || !cJSON_IsNumber(item))
        return default_value;
    return static_cast<size_t>(item->valuedouble);
}

template <>
uint64_t cjson_get<uint64_t>(const cJSON* node, const char* key, uint64_t default_value) {
    if (!node)
        return default_value;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(node, key);
    if (!item || !cJSON_IsNumber(item))
        return default_value;
    return static_cast<uint64_t>(item->valuedouble);
}

std::vector<std::string> cjson_get_string_array(const cJSON* node, const char* key) {
    std::vector<std::string> result;
    if (!node)
        return result;

    const cJSON* arr = cJSON_GetObjectItemCaseSensitive(node, key);
    if (!arr || !cJSON_IsArray(arr))
        return result;

    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, arr) {
        if (cJSON_IsString(item) && item->valuestring) {
            result.push_back(item->valuestring);
        }
    }
    return result;
}

std::map<std::string, std::string> cjson_get_string_map(const cJSON* node, const char* key) {
    std::map<std::string, std::string> result;
    if (!node)
        return result;

    const cJSON* obj = cJSON_GetObjectItemCaseSensitive(node, key);
    if (!obj || !cJSON_IsObject(obj))
        return result;

    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, obj) {
        if (item->string && cJSON_IsString(item) && item->valuestring) {
            result[item->string] = item->valuestring;
        }
    }
    return result;
}

// RAII wrapper for cJSON
struct CJsonGuard {
    cJSON* json;
    explicit CJsonGuard(cJSON* j) : json(j) {}
    ~CJsonGuard() {
        if (json)
            cJSON_Delete(json);
    }
    CJsonGuard(const CJsonGuard&)            = delete;
    CJsonGuard& operator=(const CJsonGuard&) = delete;
};

// Forward declarations for cJSON parsers
ScoopConfig parse_scoop_config_cjson(const cJSON* node);
SinkConfig parse_sink_config_cjson(const cJSON* node);
RouterConfig parse_router_config_cjson(const cJSON* node);
ApplicationConfig parse_application_config_cjson(const cJSON* root);

LoggingConfig parse_logging_config_cjson(const cJSON* node) {
    LoggingConfig config;
    if (!node)
        return config;

    config.level          = cjson_get<std::string>(node, "level", "info");
    config.format         = cjson_get<std::string>(node, "format", "text");
    config.file_path      = cjson_get<std::string>(node, "file_path", "");
    config.max_file_size  = cjson_get<size_t>(node, "max_file_size", 10 * 1024 * 1024);
    config.max_files      = cjson_get<size_t>(node, "max_files", 5);
    config.enable_console = cjson_get<bool>(node, "enable_console", true);
    config.enable_file    = cjson_get<bool>(node, "enable_file", false);

    return config;
}

ScoopConfig parse_scoop_config_cjson(const cJSON* node) {
    ScoopConfig config;
    if (!node)
        return config;

    config.id      = cjson_get<std::string>(node, "id", "");
    config.name    = cjson_get<std::string>(node, "name", "");
    config.type    = cjson_get<std::string>(node, "type", "");
    config.enabled = cjson_get<bool>(node, "enabled", true);

    auto poll_ms         = cjson_get<uint64_t>(node, "poll_interval_ms", 1000);
    config.poll_interval = std::chrono::milliseconds(poll_ms);

    config.connection_params = cjson_get_string_map(node, "connection");
    config.custom_params     = cjson_get_string_map(node, "parameters");

    return config;
}

SinkConfig parse_sink_config_cjson(const cJSON* node) {
    SinkConfig config;
    if (!node)
        return config;

    config.id      = cjson_get<std::string>(node, "id", "");
    config.name    = cjson_get<std::string>(node, "name", "");
    config.type    = cjson_get<std::string>(node, "type", "");
    config.enabled = cjson_get<bool>(node, "enabled", true);

    config.connection_params = cjson_get_string_map(node, "connection");
    config.custom_params     = cjson_get_string_map(node, "parameters");

    return config;
}

RouteConfig parse_route_config_cjson(const cJSON* node) {
    RouteConfig config;
    if (!node)
        return config;

    config.id             = cjson_get<std::string>(node, "id", "");
    config.name           = cjson_get<std::string>(node, "name", "");
    config.enabled        = cjson_get<bool>(node, "enabled", true);
    config.priority       = cjson_get<uint32_t>(node, "priority", 0);
    config.source_pattern = cjson_get<std::string>(node, "source_pattern", "");
    config.sink_ids       = cjson_get_string_array(node, "sink_ids");

    return config;
}

RouterConfig parse_router_config_cjson(const cJSON* node) {
    RouterConfig config;
    if (!node)
        return config;

    config.id             = cjson_get<std::string>(node, "id", "default-router");
    config.worker_threads = cjson_get<size_t>(node, "worker_threads", 4);
    config.queue_size     = cjson_get<size_t>(node, "queue_size", 10000);

    const cJSON* routes = cJSON_GetObjectItemCaseSensitive(node, "routes");
    if (routes && cJSON_IsArray(routes)) {
        const cJSON* route = nullptr;
        cJSON_ArrayForEach(route, routes) {
            config.routes.push_back(parse_route_config_cjson(route));
        }
    }

    return config;
}

ApplicationConfig parse_application_config_cjson(const cJSON* root) {
    ApplicationConfig config;
    if (!root)
        return config;

    config.name    = cjson_get<std::string>(root, "name", "ipb-gateway");
    config.version = cjson_get<std::string>(root, "version", "1.0.0");

    const cJSON* logging = cJSON_GetObjectItemCaseSensitive(root, "logging");
    if (logging) {
        config.logging = parse_logging_config_cjson(logging);
    }

    const cJSON* router = cJSON_GetObjectItemCaseSensitive(root, "router");
    if (router) {
        config.router = parse_router_config_cjson(router);
    }

    const cJSON* scoops = cJSON_GetObjectItemCaseSensitive(root, "scoops");
    if (scoops && cJSON_IsArray(scoops)) {
        const cJSON* scoop = nullptr;
        cJSON_ArrayForEach(scoop, scoops) {
            config.scoops.push_back(parse_scoop_config_cjson(scoop));
        }
    }

    const cJSON* sinks = cJSON_GetObjectItemCaseSensitive(root, "sinks");
    if (sinks && cJSON_IsArray(sinks)) {
        const cJSON* sink = nullptr;
        cJSON_ArrayForEach(sink, sinks) {
            config.sinks.push_back(parse_sink_config_cjson(sink));
        }
    }

    return config;
}

}  // anonymous namespace

#endif  // IPB_CONFIG_USE_CJSON

// ============================================================================
// EMBEDDED CONFIG LOADER IMPLEMENTATION
// ============================================================================

EmbeddedConfigLoader::EmbeddedConfigLoader() : constraints_{}, allocator_{}, last_stats_{} {}

EmbeddedConfigLoader::EmbeddedConfigLoader(const EmbeddedConfigConstraints& constraints)
    : constraints_(constraints), allocator_{}, last_stats_{} {
    if (constraints_.use_static_buffers && constraints_.static_buffer_size > 0) {
        static_buffer_ = std::make_unique<char[]>(constraints_.static_buffer_size);
    }
}

EmbeddedConfigLoader::EmbeddedConfigLoader(const EmbeddedConfigConstraints& constraints,
                                           const EmbeddedAllocator& allocator)
    : constraints_(constraints), allocator_(allocator), last_stats_{} {
    if (constraints_.use_static_buffers && constraints_.static_buffer_size > 0) {
        static_buffer_ = std::make_unique<char[]>(constraints_.static_buffer_size);
    }
}

EmbeddedConfigLoader::~EmbeddedConfigLoader() = default;

EmbeddedConfigLoader::EmbeddedConfigLoader(EmbeddedConfigLoader&&) noexcept            = default;
EmbeddedConfigLoader& EmbeddedConfigLoader::operator=(EmbeddedConfigLoader&&) noexcept = default;

void EmbeddedConfigLoader::set_constraints(const EmbeddedConfigConstraints& constraints) {
    constraints_ = constraints;
    if (constraints_.use_static_buffers && constraints_.static_buffer_size > 0) {
        static_buffer_ = std::make_unique<char[]>(constraints_.static_buffer_size);
    }
}

void EmbeddedConfigLoader::set_allocator(const EmbeddedAllocator& allocator) {
    allocator_ = allocator;
}

void EmbeddedConfigLoader::release_resources() {
    // Release static buffer if configured
    if (constraints_.release_parser_after_load) {
        static_buffer_.reset();
    }
    // Reset stats
    last_stats_ = {};
}

bool EmbeddedConfigLoader::check_constraints(std::string_view content) const {
    if (content.size() > constraints_.max_file_size) {
        return false;
    }
    // Estimate memory usage (rough: 3x content size for parsing overhead)
    if (content.size() * 3 > constraints_.max_memory_bytes) {
        return false;
    }
    return true;
}

bool EmbeddedConfigLoader::validate_constraints(std::string_view content) {
    if (content.size() > constraints_.max_file_size) {
        last_stats_.constraints_exceeded = true;
        last_stats_.constraint_error     = "File size exceeds maximum";
        return false;
    }
    return true;
}

void EmbeddedConfigLoader::update_stats(size_t memory_used, size_t parse_time_us) {
    last_stats_.peak_memory_usage = memory_used;
    last_stats_.parse_time_us     = parse_time_us;
}

common::Result<std::string> EmbeddedConfigLoader::read_file_constrained(
    const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return common::Result<std::string>(common::ErrorCode::FILE_NOT_FOUND,
                                           "Cannot open file: " + path.string());
    }

    auto size = file.tellg();
    if (static_cast<size_t>(size) > constraints_.max_file_size) {
        return common::Result<std::string>(common::ErrorCode::INVALID_ARGUMENT,
                                           "File size exceeds maximum: " + std::to_string(size) +
                                               " > " + std::to_string(constraints_.max_file_size));
    }

    last_stats_.file_size = static_cast<size_t>(size);

    file.seekg(0);
    std::string content;
    content.resize(static_cast<size_t>(size));
    file.read(content.data(), size);

    return common::Result<std::string>(std::move(content));
}

// ============================================================================
// FILE LOADING
// ============================================================================

common::Result<ApplicationConfig> EmbeddedConfigLoader::load_application(
    const std::filesystem::path& path, ConfigFormat format) {
    auto content_result = read_file_constrained(path);
    if (!content_result.is_success()) {
        return common::Result<ApplicationConfig>(content_result.error_code(),
                                                 content_result.error_message());
    }

    if (format == ConfigFormat::AUTO) {
        format = detect_format(path);
    }

    return parse_application(content_result.value(), format);
}

common::Result<ScoopConfig> EmbeddedConfigLoader::load_scoop(const std::filesystem::path& path,
                                                             ConfigFormat format) {
    auto content_result = read_file_constrained(path);
    if (!content_result.is_success()) {
        return common::Result<ScoopConfig>(content_result.error_code(),
                                           content_result.error_message());
    }

    if (format == ConfigFormat::AUTO) {
        format = detect_format(path);
    }

    return parse_scoop(content_result.value(), format);
}

common::Result<SinkConfig> EmbeddedConfigLoader::load_sink(const std::filesystem::path& path,
                                                           ConfigFormat format) {
    auto content_result = read_file_constrained(path);
    if (!content_result.is_success()) {
        return common::Result<SinkConfig>(content_result.error_code(),
                                          content_result.error_message());
    }

    if (format == ConfigFormat::AUTO) {
        format = detect_format(path);
    }

    return parse_sink(content_result.value(), format);
}

common::Result<RouterConfig> EmbeddedConfigLoader::load_router(const std::filesystem::path& path,
                                                               ConfigFormat format) {
    auto content_result = read_file_constrained(path);
    if (!content_result.is_success()) {
        return common::Result<RouterConfig>(content_result.error_code(),
                                            content_result.error_message());
    }

    if (format == ConfigFormat::AUTO) {
        format = detect_format(path);
    }

    return parse_router(content_result.value(), format);
}

common::Result<std::vector<ScoopConfig>> EmbeddedConfigLoader::load_scoops_from_directory(
    const std::filesystem::path& dir_path, ConfigFormat format) {
    std::vector<ScoopConfig> configs;

    if (!std::filesystem::exists(dir_path)) {
        return common::Result<std::vector<ScoopConfig>>(
            common::ErrorCode::FILE_NOT_FOUND, "Directory not found: " + dir_path.string());
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext != ".yaml" && ext != ".yml" && ext != ".json")
            continue;

        auto result = load_scoop(entry.path(), format);
        if (result.is_success()) {
            configs.push_back(result.value());
        }
    }

    return common::Result<std::vector<ScoopConfig>>(std::move(configs));
}

common::Result<std::vector<SinkConfig>> EmbeddedConfigLoader::load_sinks_from_directory(
    const std::filesystem::path& dir_path, ConfigFormat format) {
    std::vector<SinkConfig> configs;

    if (!std::filesystem::exists(dir_path)) {
        return common::Result<std::vector<SinkConfig>>(common::ErrorCode::FILE_NOT_FOUND,
                                                       "Directory not found: " + dir_path.string());
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext != ".yaml" && ext != ".yml" && ext != ".json")
            continue;

        auto result = load_sink(entry.path(), format);
        if (result.is_success()) {
            configs.push_back(result.value());
        }
    }

    return common::Result<std::vector<SinkConfig>>(std::move(configs));
}

// ============================================================================
// STRING PARSING
// ============================================================================

common::Result<ApplicationConfig> EmbeddedConfigLoader::parse_application(std::string_view content,
                                                                          ConfigFormat format) {
    if (!validate_constraints(content)) {
        return common::Result<ApplicationConfig>(common::ErrorCode::INVALID_ARGUMENT,
                                                 last_stats_.constraint_error);
    }

    auto start = std::chrono::high_resolution_clock::now();
    reset_memory_tracking();

    if (format == ConfigFormat::AUTO) {
        format = detect_format_from_content(content);
    }

    try {
        ApplicationConfig config;

        if (format == ConfigFormat::JSON) {
#ifdef IPB_CONFIG_USE_CJSON
            cJSON* json = cJSON_Parse(content.data());
            if (!json) {
                return common::Result<ApplicationConfig>(common::ErrorCode::PARSE_ERROR,
                                                         "Failed to parse JSON");
            }
            CJsonGuard guard(json);
            config = parse_application_config_cjson(json);
#else
            // No JSON parser available in embedded mode
            return common::Result<ApplicationConfig>(
                common::ErrorCode::NOT_IMPLEMENTED,
                "JSON parsing requires cJSON library in embedded mode");
#endif
        } else {
#ifdef IPB_CONFIG_USE_RYML
            ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(content));
            config          = parse_application_config_ryml(tree.rootref());
#else
            // No YAML parser available in embedded mode
            return common::Result<ApplicationConfig>(
                common::ErrorCode::NOT_IMPLEMENTED,
                "YAML parsing requires rapidyaml library in embedded mode");
#endif
        }

        auto end      = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        update_stats(g_peak_memory, duration.count());

        if (constraints_.release_parser_after_load) {
            // Resources are automatically released after parsing
        }

        return common::Result<ApplicationConfig>(std::move(config));

    } catch (const std::exception& e) {
        return common::Result<ApplicationConfig>(common::ErrorCode::PARSE_ERROR,
                                                 std::string("Parse error: ") + e.what());
    }
}

common::Result<ScoopConfig> EmbeddedConfigLoader::parse_scoop(std::string_view content,
                                                              ConfigFormat format) {
    if (!validate_constraints(content)) {
        return common::Result<ScoopConfig>(common::ErrorCode::INVALID_ARGUMENT,
                                           last_stats_.constraint_error);
    }

    if (format == ConfigFormat::AUTO) {
        format = detect_format_from_content(content);
    }

    try {
        ScoopConfig config;

        if (format == ConfigFormat::JSON) {
#ifdef IPB_CONFIG_USE_CJSON
            cJSON* json = cJSON_Parse(content.data());
            if (!json) {
                return common::Result<ScoopConfig>(common::ErrorCode::PARSE_ERROR,
                                                   "Failed to parse JSON");
            }
            CJsonGuard guard(json);
            config = parse_scoop_config_cjson(json);
#else
            return common::Result<ScoopConfig>(common::ErrorCode::NOT_IMPLEMENTED,
                                               "JSON parsing requires cJSON in embedded mode");
#endif
        } else {
#ifdef IPB_CONFIG_USE_RYML
            ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(content));
            config          = parse_scoop_config_ryml(tree.rootref());
#else
            return common::Result<ScoopConfig>(common::ErrorCode::NOT_IMPLEMENTED,
                                               "YAML parsing requires ryml in embedded mode");
#endif
        }

        return common::Result<ScoopConfig>(std::move(config));

    } catch (const std::exception& e) {
        return common::Result<ScoopConfig>(common::ErrorCode::PARSE_ERROR,
                                           std::string("Parse error: ") + e.what());
    }
}

common::Result<SinkConfig> EmbeddedConfigLoader::parse_sink(std::string_view content,
                                                            ConfigFormat format) {
    if (!validate_constraints(content)) {
        return common::Result<SinkConfig>(common::ErrorCode::INVALID_ARGUMENT,
                                          last_stats_.constraint_error);
    }

    if (format == ConfigFormat::AUTO) {
        format = detect_format_from_content(content);
    }

    try {
        SinkConfig config;

        if (format == ConfigFormat::JSON) {
#ifdef IPB_CONFIG_USE_CJSON
            cJSON* json = cJSON_Parse(content.data());
            if (!json) {
                return common::Result<SinkConfig>(common::ErrorCode::PARSE_ERROR,
                                                  "Failed to parse JSON");
            }
            CJsonGuard guard(json);
            config = parse_sink_config_cjson(json);
#else
            return common::Result<SinkConfig>(common::ErrorCode::NOT_IMPLEMENTED,
                                              "JSON parsing requires cJSON in embedded mode");
#endif
        } else {
#ifdef IPB_CONFIG_USE_RYML
            ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(content));
            config          = parse_sink_config_ryml(tree.rootref());
#else
            return common::Result<SinkConfig>(common::ErrorCode::NOT_IMPLEMENTED,
                                              "YAML parsing requires ryml in embedded mode");
#endif
        }

        return common::Result<SinkConfig>(std::move(config));

    } catch (const std::exception& e) {
        return common::Result<SinkConfig>(common::ErrorCode::PARSE_ERROR,
                                          std::string("Parse error: ") + e.what());
    }
}

common::Result<RouterConfig> EmbeddedConfigLoader::parse_router(std::string_view content,
                                                                ConfigFormat format) {
    if (!validate_constraints(content)) {
        return common::Result<RouterConfig>(common::ErrorCode::INVALID_ARGUMENT,
                                            last_stats_.constraint_error);
    }

    if (format == ConfigFormat::AUTO) {
        format = detect_format_from_content(content);
    }

    try {
        RouterConfig config;

        if (format == ConfigFormat::JSON) {
#ifdef IPB_CONFIG_USE_CJSON
            cJSON* json = cJSON_Parse(content.data());
            if (!json) {
                return common::Result<RouterConfig>(common::ErrorCode::PARSE_ERROR,
                                                    "Failed to parse JSON");
            }
            CJsonGuard guard(json);
            config = parse_router_config_cjson(json);
#else
            return common::Result<RouterConfig>(common::ErrorCode::NOT_IMPLEMENTED,
                                                "JSON parsing requires cJSON in embedded mode");
#endif
        } else {
#ifdef IPB_CONFIG_USE_RYML
            ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(content));
            config          = parse_router_config_ryml(tree.rootref());
#else
            return common::Result<RouterConfig>(common::ErrorCode::NOT_IMPLEMENTED,
                                                "YAML parsing requires ryml in embedded mode");
#endif
        }

        return common::Result<RouterConfig>(std::move(config));

    } catch (const std::exception& e) {
        return common::Result<RouterConfig>(common::ErrorCode::PARSE_ERROR,
                                            std::string("Parse error: ") + e.what());
    }
}

// ============================================================================
// SERIALIZATION (Minimal for embedded - prefer JSON)
// ============================================================================

common::Result<std::string> EmbeddedConfigLoader::serialize_application(
    const ApplicationConfig& /*config*/, ConfigFormat /*format*/) {
    return common::Result<std::string>(common::ErrorCode::NOT_IMPLEMENTED,
                                       "Serialization not supported in embedded mode");
}

common::Result<std::string> EmbeddedConfigLoader::serialize_scoop(const ScoopConfig& /*config*/,
                                                                  ConfigFormat /*format*/) {
    return common::Result<std::string>(common::ErrorCode::NOT_IMPLEMENTED,
                                       "Serialization not supported in embedded mode");
}

common::Result<std::string> EmbeddedConfigLoader::serialize_sink(const SinkConfig& /*config*/,
                                                                 ConfigFormat /*format*/) {
    return common::Result<std::string>(common::ErrorCode::NOT_IMPLEMENTED,
                                       "Serialization not supported in embedded mode");
}

common::Result<std::string> EmbeddedConfigLoader::serialize_router(const RouterConfig& /*config*/,
                                                                   ConfigFormat /*format*/) {
    return common::Result<std::string>(common::ErrorCode::NOT_IMPLEMENTED,
                                       "Serialization not supported in embedded mode");
}

// ============================================================================
// FILE SAVING (Not supported in embedded mode)
// ============================================================================

common::Result<void> EmbeddedConfigLoader::save_application(const ApplicationConfig& /*config*/,
                                                            const std::filesystem::path& /*path*/,
                                                            ConfigFormat /*format*/) {
    return common::Result<void>(common::ErrorCode::NOT_IMPLEMENTED,
                                "File saving not supported in embedded mode");
}

common::Result<void> EmbeddedConfigLoader::save_scoop(const ScoopConfig& /*config*/,
                                                      const std::filesystem::path& /*path*/,
                                                      ConfigFormat /*format*/) {
    return common::Result<void>(common::ErrorCode::NOT_IMPLEMENTED,
                                "File saving not supported in embedded mode");
}

common::Result<void> EmbeddedConfigLoader::save_sink(const SinkConfig& /*config*/,
                                                     const std::filesystem::path& /*path*/,
                                                     ConfigFormat /*format*/) {
    return common::Result<void>(common::ErrorCode::NOT_IMPLEMENTED,
                                "File saving not supported in embedded mode");
}

// ============================================================================
// VALIDATION
// ============================================================================

common::Result<void> EmbeddedConfigLoader::validate(const ApplicationConfig& config) {
    if (config.name.empty()) {
        return common::Result<void>(common::ErrorCode::INVALID_ARGUMENT,
                                    "Application name is required");
    }
    return common::Result<void>();
}

common::Result<void> EmbeddedConfigLoader::validate(const ScoopConfig& config) {
    if (config.id.empty()) {
        return common::Result<void>(common::ErrorCode::INVALID_ARGUMENT, "Scoop ID is required");
    }
    if (config.type.empty()) {
        return common::Result<void>(common::ErrorCode::INVALID_ARGUMENT, "Scoop type is required");
    }
    return common::Result<void>();
}

common::Result<void> EmbeddedConfigLoader::validate(const SinkConfig& config) {
    if (config.id.empty()) {
        return common::Result<void>(common::ErrorCode::INVALID_ARGUMENT, "Sink ID is required");
    }
    if (config.type.empty()) {
        return common::Result<void>(common::ErrorCode::INVALID_ARGUMENT, "Sink type is required");
    }
    return common::Result<void>();
}

common::Result<void> EmbeddedConfigLoader::validate(const RouterConfig& config) {
    if (config.worker_threads == 0) {
        return common::Result<void>(common::ErrorCode::INVALID_ARGUMENT,
                                    "Router must have at least one worker thread");
    }
    return common::Result<void>();
}

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

std::unique_ptr<ConfigLoader> create_embedded_config_loader() {
    return std::make_unique<EmbeddedConfigLoader>();
}

std::unique_ptr<ConfigLoader> create_embedded_config_loader(
    const EmbeddedConfigConstraints& constraints) {
    return std::make_unique<EmbeddedConfigLoader>(constraints);
}

std::unique_ptr<ConfigLoader> create_config_loader_for_platform(
    common::DeploymentPlatform platform, const EmbeddedConfigConstraints& constraints) {
    switch (platform) {
        case common::DeploymentPlatform::EMBEDDED_BARE_METAL:
        case common::DeploymentPlatform::EMBEDDED_RTOS:
        case common::DeploymentPlatform::EMBEDDED_LINUX:
            return std::make_unique<EmbeddedConfigLoader>(constraints);

        case common::DeploymentPlatform::EDGE_GATEWAY:
        case common::DeploymentPlatform::EDGE_MOBILE:
        case common::DeploymentPlatform::SERVER_STANDARD:
        case common::DeploymentPlatform::SERVER_CLOUD:
        case common::DeploymentPlatform::SERVER_CONTAINERIZED:
        default:
            // In EMBEDDED build mode, always use embedded config loader
            // (full config loader requires yaml-cpp + jsoncpp which aren't available)
#if defined(IPB_BUILD_EMBEDDED)
            return std::make_unique<EmbeddedConfigLoader>(constraints);
#else
            return create_config_loader();
#endif
    }
}

}  // namespace ipb::core::config
