#include "ipb/gate/orchestrator.hpp"

#include <ipb/common/debug.hpp>
#include <ipb/common/error.hpp>
#include <ipb/common/platform.hpp>

#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>

#include <json/json.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ipb/sink/console/console_sink.hpp"
#include "ipb/sink/syslog/syslog_sink.hpp"

namespace ipb::gate {

using namespace common::debug;

namespace {
const char* const LOG_CAT = "GENERAL";
}  // anonymous namespace

IPBOrchestrator::IPBOrchestrator(const std::string& config_file_path)
    : config_file_path_(config_file_path) {
    if (config_file_path_.empty()) {
        config_file_path_ = "/etc/ipb/gateway.yaml";
    }
}

IPBOrchestrator::~IPBOrchestrator() {
    if (running_.load()) {
        stop();
    }
    shutdown();
}

common::Result<void> IPBOrchestrator::initialize() {
    IPB_SPAN_CAT("Orchestrator::initialize", LOG_CAT);
    IPB_LOG_INFO(LOG_CAT, "Initializing IPB Orchestrator...");

    try {
        // Load configuration
        IPB_LOG_DEBUG(LOG_CAT, "Loading configuration from: " << config_file_path_);
        auto load_result = load_config();
        if (IPB_UNLIKELY(!load_result.is_success())) {
            IPB_LOG_ERROR(LOG_CAT, "Failed to load configuration: " << load_result.message());
            return load_result;
        }

        // Validate configuration
        IPB_LOG_DEBUG(LOG_CAT, "Validating configuration...");
        auto validate_result = validate_config();
        if (IPB_UNLIKELY(!validate_result.is_success())) {
            IPB_LOG_ERROR(LOG_CAT,
                          "Configuration validation failed: " << validate_result.message());
            return validate_result;
        }

        // Setup real-time scheduling if enabled
        if (config_.scheduler.enable_realtime_priority) {
            IPB_LOG_INFO(LOG_CAT, "Setting up real-time scheduling with priority "
                                      << config_.scheduler.realtime_priority);
            setup_realtime_scheduling();
        }

        // Setup CPU affinity if enabled
        if (config_.scheduler.enable_cpu_affinity) {
            IPB_LOG_INFO(LOG_CAT, "Setting up CPU affinity");
            setup_cpu_affinity();
        }

        // Initialize router using core config
        IPB_LOG_DEBUG(LOG_CAT, "Initializing router...");
        router::RouterConfig router_config = router::RouterConfig::default_config();
        // Apply configuration from core::config::RouterConfig
        router_config.scheduler.worker_threads = config_.router.worker_threads;
        router_config.enable_tracing           = config_.router.enable_zero_copy;

        router_                  = std::make_unique<router::Router>(router_config);
        auto router_start_result = router_->start();
        if (!router_start_result.is_success()) {
            return router_start_result;
        }

        // Setup rule engine for routing
        setup_rule_engine();

        // Load adapters
        auto scoops_result = load_scoops();
        if (!scoops_result.is_success()) {
            return scoops_result;
        }

        // Load sinks
        auto sinks_result = load_sinks();
        if (!sinks_result.is_success()) {
            return sinks_result;
        }

        // Setup routing rules
        auto routing_result = setup_routing();
        if (!routing_result.is_success()) {
            return routing_result;
        }

        // Setup MQTT command interface if enabled
        if (config_.command_interface.enabled) {
            setup_mqtt_commands();
        }

        // Setup signal handlers
        setup_signal_handlers();

        return common::Result<void>();

    } catch (const std::exception& e) {
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR,
                                    "Failed to initialize orchestrator: " + std::string(e.what()));
    }
}

common::Result<void> IPBOrchestrator::start() {
    IPB_SPAN_CAT("Orchestrator::start", LOG_CAT);

    if (IPB_UNLIKELY(running_.load())) {
        IPB_LOG_WARN(LOG_CAT, "Orchestrator is already running");
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR,
                                    "Orchestrator is already running");
    }

    IPB_LOG_INFO(LOG_CAT, "Starting IPB Orchestrator...");

    try {
        running_.store(true);
        shutdown_requested_.store(false);

        // Start router
        IPB_LOG_DEBUG(LOG_CAT, "Starting router...");
        auto router_start_result = router_->start();
        if (IPB_UNLIKELY(!router_start_result.is_success())) {
            running_.store(false);
            IPB_LOG_ERROR(LOG_CAT, "Failed to start router: " << router_start_result.message());
            return router_start_result;
        }

        // Start all adapters
        IPB_LOG_DEBUG(LOG_CAT, "Starting " << scoops_.size() << " scoops...");
        for (auto& [scoop_id, adapter] : scoops_) {
            auto start_result = start_scoop(scoop_id);
            if (!start_result.is_success()) {
                IPB_LOG_ERROR(LOG_CAT, "Failed to start scoop " << scoop_id << ": "
                                                                << start_result.message());
                metrics_.scoop_errors.fetch_add(1);
            } else {
                IPB_LOG_DEBUG(LOG_CAT, "Started scoop: " << scoop_id);
            }
        }

        // Start all sinks
        IPB_LOG_DEBUG(LOG_CAT, "Starting " << sinks_.size() << " sinks...");
        for (auto& [sink_id, sink] : sinks_) {
            auto start_result = start_sink(sink_id);
            if (!start_result.is_success()) {
                IPB_LOG_ERROR(LOG_CAT,
                              "Failed to start sink " << sink_id << ": " << start_result.message());
                metrics_.sink_errors.fetch_add(1);
            } else {
                IPB_LOG_DEBUG(LOG_CAT, "Started sink: " << sink_id);
            }
        }

        // Start maintenance thread
        maintenance_thread_ = std::thread(&IPBOrchestrator::maintenance_loop, this);

        // Start config monitor thread if hot reload is enabled
        if (config_.hot_reload.enabled) {
            config_monitor_thread_ = std::thread(&IPBOrchestrator::monitor_config_file, this);
        }

        // Start MQTT command thread if enabled
        if (config_.command_interface.enabled) {
            mqtt_command_thread_ = std::thread(&IPBOrchestrator::mqtt_command_loop, this);
        }

        // Start metrics thread if monitoring is enabled
        if (config_.monitoring.prometheus.enabled) {
            metrics_thread_ = std::thread(&IPBOrchestrator::metrics_loop, this);
        }

        // Reset metrics
        metrics_ = GatewayMetrics{};

        return common::Result<void>();

    } catch (const std::exception& e) {
        running_.store(false);
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR,
                                    "Failed to start orchestrator: " + std::string(e.what()));
    }
}

common::Result<void> IPBOrchestrator::stop() {
    IPB_SPAN_CAT("Orchestrator::stop", LOG_CAT);

    if (IPB_UNLIKELY(!running_.load())) {
        IPB_LOG_DEBUG(LOG_CAT, "Orchestrator stop called but not running");
        return common::Result<void>();
    }

    IPB_LOG_INFO(LOG_CAT, "Stopping IPB Orchestrator...");

    try {
        running_.store(false);

        // Stop all threads
        IPB_LOG_DEBUG(LOG_CAT, "Stopping maintenance thread...");
        if (maintenance_thread_.joinable()) {
            maintenance_thread_.join();
        }

        if (config_monitor_thread_.joinable()) {
            IPB_LOG_DEBUG(LOG_CAT, "Stopping config monitor thread...");
            config_monitor_thread_.join();
        }

        if (mqtt_command_thread_.joinable()) {
            IPB_LOG_DEBUG(LOG_CAT, "Stopping MQTT command thread...");
            command_queue_condition_.notify_all();
            mqtt_command_thread_.join();
        }

        if (metrics_thread_.joinable()) {
            IPB_LOG_DEBUG(LOG_CAT, "Stopping metrics thread...");
            metrics_thread_.join();
        }

        // Stop all adapters
        IPB_LOG_DEBUG(LOG_CAT, "Stopping scoops...");
        for (auto& [scoop_id, adapter] : scoops_) {
            auto stop_result = stop_scoop(scoop_id);
            if (!stop_result.is_success()) {
                IPB_LOG_WARN(LOG_CAT,
                             "Failed to stop scoop " << scoop_id << ": " << stop_result.message());
            }
        }

        // Stop all sinks
        IPB_LOG_DEBUG(LOG_CAT, "Stopping sinks...");
        for (auto& [sink_id, sink] : sinks_) {
            auto stop_result = stop_sink(sink_id);
            if (!stop_result.is_success()) {
                IPB_LOG_WARN(LOG_CAT,
                             "Failed to stop sink " << sink_id << ": " << stop_result.message());
            }
        }

        // Stop router
        if (router_) {
            IPB_LOG_DEBUG(LOG_CAT, "Stopping router...");
            auto router_stop_result = router_->stop();
            if (!router_stop_result.is_success()) {
                IPB_LOG_WARN(LOG_CAT, "Failed to stop router: " << router_stop_result.message());
            }
        }

        IPB_LOG_INFO(LOG_CAT, "IPB Orchestrator stopped");
        return common::Result<void>();

    } catch (const std::exception& e) {
        IPB_LOG_ERROR(LOG_CAT, "Exception during orchestrator stop: " << e.what());
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR,
                                    "Failed to stop orchestrator: " + std::string(e.what()));
    }
}

common::Result<void> IPBOrchestrator::shutdown() {
    shutdown_requested_.store(true);

    auto stop_result = stop();
    if (!stop_result.is_success()) {
        return stop_result;
    }

    try {
        // Shutdown all components
        for (auto& [scoop_id, adapter] : scoops_) {
            if (adapter) {
                adapter->disconnect();
            }
        }
        scoops_.clear();

        for (auto& [sink_id, sink] : sinks_) {
            if (sink) {
                sink->stop();
            }
        }
        sinks_.clear();

        if (router_) {
            router_->stop();
            router_.reset();
        }

        return common::Result<void>();

    } catch (const std::exception& e) {
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR,
                                    "Failed to shutdown orchestrator: " + std::string(e.what()));
    }
}

bool IPBOrchestrator::is_healthy() const {
    if (!running_.load()) {
        return false;
    }

    // Check router health
    if (!router_ || !router_->is_healthy()) {
        return false;
    }

    // Check adapter health
    for (const auto& [scoop_id, adapter] : scoops_) {
        if (!adapter->is_healthy()) {
            return false;
        }
    }

    // Check sink health
    for (const auto& [sink_id, sink] : sinks_) {
        if (!sink->is_healthy()) {
            return false;
        }
    }

    return true;
}

common::Result<void> IPBOrchestrator::load_config() {
    try {
        std::lock_guard<std::mutex> lock(config_mutex_);

        // Create config loader if not already created
        if (!config_loader_) {
            config_loader_ = core::config::create_config_loader();
        }

        // Load configuration using core ConfigLoader
        auto result = config_loader_->load_application(config_file_path_);
        if (!result.is_success()) {
            return common::Result<void>(result.error_code(), result.error_message());
        }

        config_ = result.value();

        IPB_LOG_INFO(LOG_CAT, "Configuration loaded successfully");
        IPB_LOG_DEBUG(LOG_CAT, "  Instance ID: " << config_.instance_id);
        IPB_LOG_DEBUG(LOG_CAT, "  Scoops: " << config_.scoops.size());
        IPB_LOG_DEBUG(LOG_CAT, "  Sinks: " << config_.sinks.size());
        IPB_LOG_DEBUG(LOG_CAT, "  Routes: " << config_.router.routes.size());

        return common::Result<void>();

    } catch (const std::exception& e) {
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR,
                                    "Failed to load configuration: " + std::string(e.what()));
    }
}

common::Result<void> IPBOrchestrator::validate_config() const {
    // Use core config loader validation
    if (config_loader_) {
        auto result = config_loader_->validate(config_);
        if (!result.is_success()) {
            return result;
        }
    }

    // Additional gateway-specific validation
    if (config_.scheduler.enable_realtime_priority) {
        if (config_.scheduler.realtime_priority < 1 || config_.scheduler.realtime_priority > 99) {
            return common::Result<void>(common::ErrorCode::INVALID_ARGUMENT,
                                        "Real-time priority must be between 1 and 99");
        }
    }

    return common::Result<void>();
}

common::Result<void> IPBOrchestrator::load_sinks() {
    try {
        sinks_.clear();

        for (const auto& sink_config : config_.sinks) {
            if (!sink_config.enabled) {
                IPB_LOG_DEBUG(LOG_CAT, "Skipping disabled sink: " << sink_config.id);
                continue;
            }

            auto sink = create_sink(sink_config);
            if (!sink) {
                return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR,
                                            "Failed to create sink: " + sink_config.id);
            }

            // Initialize sink with empty path (config already applied)
            auto init_result = sink->initialize("");
            if (!init_result.is_success()) {
                return common::Result<void>(
                    common::ErrorCode::UNKNOWN_ERROR,
                    "Failed to initialize sink " + sink_config.id + ": " + init_result.message());
            }

            sinks_[sink_config.id] = sink;
            IPB_LOG_INFO(LOG_CAT, "Loaded sink: " << sink_config.id);
        }

        return common::Result<void>();

    } catch (const std::exception& e) {
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR,
                                    "Failed to load sinks: " + std::string(e.what()));
    }
}

std::shared_ptr<common::ISink> IPBOrchestrator::create_sink(
    const core::config::SinkConfig& config) {
    try {
        // Get protocol type string for matching
        std::string type_str;
        switch (config.protocol_type) {
            case common::ProtocolType::CUSTOM:
                // Check protocol_settings for type hint
                if (auto it = config.protocol_settings.find("type");
                    it != config.protocol_settings.end()) {
                    if (auto* str = std::get_if<std::string>(&it->second)) {
                        type_str = *str;
                    }
                }
                break;
            default:
                // Use protocol type name
                type_str = config.name;  // Fallback to name
                break;
        }

        // Also check name for common sink types
        std::string name_lower = config.name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

        if (name_lower.find("console") != std::string::npos || type_str == "console") {
            // Create console sink configuration
            sink::console::ConsoleSinkConfig console_config;

            // Map format from core config
            if (config.format.format == "json") {
                console_config.output_format = sink::console::OutputFormat::JSON;
            } else if (config.format.format == "csv") {
                console_config.output_format = sink::console::OutputFormat::CSV;
            } else if (config.format.format == "table") {
                console_config.output_format = sink::console::OutputFormat::TABLE;
            } else if (config.format.format == "colored") {
                console_config.output_format = sink::console::OutputFormat::COLORED;
            } else {
                console_config.output_format = sink::console::OutputFormat::PLAIN;
            }

            // Check protocol_settings for additional options
            for (const auto& [key, value] : config.protocol_settings) {
                if (auto* bval = std::get_if<bool>(&value)) {
                    if (key == "enable_file_output")
                        console_config.enable_file_output = *bval;
                    else if (key == "enable_async")
                        console_config.enable_async_output = *bval;
                    else if (key == "enable_statistics")
                        console_config.enable_statistics = *bval;
                } else if (auto* sval = std::get_if<std::string>(&value)) {
                    if (key == "output_file")
                        console_config.output_file_path = *sval;
                }
            }

            return std::make_shared<sink::console::ConsoleSink>(console_config);
        } else if (name_lower.find("syslog") != std::string::npos || type_str == "syslog") {
            // Create syslog sink configuration
            sink::syslog::SyslogSinkConfig syslog_config;

            syslog_config.ident = config.name;

            // Map format
            if (config.format.format == "rfc5424") {
                syslog_config.format = sink::syslog::SyslogFormat::RFC5424;
            } else if (config.format.format == "json") {
                syslog_config.format = sink::syslog::SyslogFormat::JSON;
            } else if (config.format.format == "plain") {
                syslog_config.format = sink::syslog::SyslogFormat::PLAIN;
            } else {
                syslog_config.format = sink::syslog::SyslogFormat::RFC3164;
            }

            // Check connection for remote syslog
            if (!config.connection.endpoint.host.empty()) {
                syslog_config.enable_remote_syslog = true;
                syslog_config.remote_host          = config.connection.endpoint.host;
                syslog_config.remote_port          = config.connection.endpoint.port;
            }

            // Check protocol_settings for additional options
            for (const auto& [key, value] : config.protocol_settings) {
                if (auto* sval = std::get_if<std::string>(&value)) {
                    if (key == "facility") {
                        if (*sval == "local0")
                            syslog_config.facility = sink::syslog::SyslogFacility::LOCAL0;
                        else if (*sval == "local1")
                            syslog_config.facility = sink::syslog::SyslogFacility::LOCAL1;
                        else if (*sval == "daemon")
                            syslog_config.facility = sink::syslog::SyslogFacility::DAEMON;
                        else if (*sval == "user")
                            syslog_config.facility = sink::syslog::SyslogFacility::USER;
                    }
                } else if (auto* bval = std::get_if<bool>(&value)) {
                    if (key == "enable_async")
                        syslog_config.enable_async_logging = *bval;
                }
            }

            return std::make_shared<sink::syslog::SyslogSink>(syslog_config);
        }

        // TODO: Add other sink types (kafka, zmq, etc.)
        IPB_LOG_WARN(LOG_CAT, "Unknown sink type for: " << config.id << " (" << config.name << ")");
        return nullptr;

    } catch (const std::exception& e) {
        IPB_LOG_ERROR(LOG_CAT, "Failed to create sink " << config.id << ": " << e.what());
        return nullptr;
    }
}

common::Result<void> IPBOrchestrator::start_sink(const std::string& sink_id) {
    auto it = sinks_.find(sink_id);
    if (it == sinks_.end()) {
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR, "Sink not found: " + sink_id);
    }

    return it->second->start();
}

common::Result<void> IPBOrchestrator::stop_sink(const std::string& sink_id) {
    auto it = sinks_.find(sink_id);
    if (it == sinks_.end()) {
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR, "Sink not found: " + sink_id);
    }

    return it->second->stop();
}

void IPBOrchestrator::setup_realtime_scheduling() {
    struct sched_param param;
    param.sched_priority = config_.scheduler.realtime_priority;

    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        std::cerr << "Warning: Failed to set real-time scheduling priority" << std::endl;
    }
}

void IPBOrchestrator::setup_cpu_affinity() {
    if (config_.scheduler.cpu_cores.empty()) {
        // Auto-detect available cores
        int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
        for (int i = 0; i < num_cores; ++i) {
            config_.scheduler.cpu_cores.push_back(i);
        }
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    for (int core : config_.scheduler.cpu_cores) {
        CPU_SET(core, &cpuset);
    }

    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        std::cerr << "Warning: Failed to set CPU affinity" << std::endl;
    }
}

void IPBOrchestrator::setup_signal_handlers() {
    // Signal handling will be implemented in main.cpp
}

void IPBOrchestrator::maintenance_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(10));

        if (running_.load()) {
            health_check();
        }
    }
}

void IPBOrchestrator::health_check() {
    // Perform health checks on all components
    // This is a simplified implementation

    if (!router_ || !router_->is_healthy()) {
        std::cerr << "Warning: Router is not healthy" << std::endl;
    }

    for (const auto& [scoop_id, adapter] : scoops_) {
        if (!adapter->is_healthy()) {
            std::cerr << "Warning: Scoop " << scoop_id << " is not healthy" << std::endl;
        }
    }

    for (const auto& [sink_id, sink] : sinks_) {
        if (!sink->is_healthy()) {
            std::cerr << "Warning: Sink " << sink_id << " is not healthy" << std::endl;
        }
    }
}

void IPBOrchestrator::monitor_config_file() {
    auto last_modification = get_file_modification_time(config_file_path_);

    while (running_.load()) {
        std::this_thread::sleep_for(config_.hot_reload.check_interval);

        if (running_.load()) {
            auto current_modification = get_file_modification_time(config_file_path_);

            if (current_modification > last_modification) {
                std::cout << "Configuration file changed, reloading..." << std::endl;
                auto reload_result = reload_config();
                if (!reload_result.is_success()) {
                    std::cerr << "Failed to reload configuration: " << reload_result.message()
                              << std::endl;
                } else {
                    std::cout << "Configuration reloaded successfully" << std::endl;
                }
                last_modification = current_modification;
            }
        }
    }
}

std::chrono::steady_clock::time_point IPBOrchestrator::get_file_modification_time(
    const std::string& file_path) const {
    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) == 0) {
        return std::chrono::steady_clock::time_point(std::chrono::seconds(file_stat.st_mtime));
    }
    return std::chrono::steady_clock::time_point{};
}

common::Result<void> IPBOrchestrator::reload_config() {
    try {
        // Save current configuration
        GatewayConfig old_config = config_;

        // Load new configuration
        auto load_result = load_config();
        if (!load_result.is_success()) {
            config_ = old_config;  // Restore old configuration
            return load_result;
        }

        // Validate new configuration
        auto validate_result = validate_config();
        if (!validate_result.is_success()) {
            config_ = old_config;  // Restore old configuration
            return validate_result;
        }

        // TODO: Apply configuration changes without full restart
        // For now, just log that configuration was reloaded
        std::cout << "Configuration reloaded (full restart required for all changes)" << std::endl;

        return common::Result<void>();

    } catch (const std::exception& e) {
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR,
                                    "Failed to reload configuration: " + std::string(e.what()));
    }
}

// Stub implementations for missing methods
common::Result<void> IPBOrchestrator::load_scoops() {
    try {
        scoops_.clear();

        for (const auto& scoop_config : config_.scoops) {
            if (!scoop_config.enabled) {
                IPB_LOG_DEBUG(LOG_CAT, "Skipping disabled scoop: " << scoop_config.id);
                continue;
            }

            auto scoop = create_scoop(scoop_config);
            if (!scoop) {
                IPB_LOG_WARN(LOG_CAT, "Failed to create scoop: " << scoop_config.id);
                continue;  // Non-fatal, continue loading other scoops
            }

            scoops_[scoop_config.id] = scoop;
            IPB_LOG_INFO(LOG_CAT, "Loaded scoop: " << scoop_config.id);
        }

        return common::Result<void>();

    } catch (const std::exception& e) {
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR,
                                    "Failed to load scoops: " + std::string(e.what()));
    }
}

void IPBOrchestrator::setup_rule_engine() {
    IPB_LOG_DEBUG(LOG_CAT, "Setting up rule engine...");

    // Create rule engine with default config
    core::RuleEngineConfig re_config;
    re_config.max_rules    = config_.router.routing_table_size;
    re_config.enable_cache = true;
    re_config.cache_size   = 10000;

    rule_engine_ = std::make_unique<core::RuleEngine>(re_config);

    IPB_LOG_INFO(LOG_CAT, "Rule engine initialized");
}

common::Result<void> IPBOrchestrator::setup_routing() {
    IPB_LOG_DEBUG(LOG_CAT, "Setting up routing rules...");

    if (!rule_engine_) {
        return common::Result<void>(common::ErrorCode::INVALID_STATE,
                                    "Rule engine not initialized");
    }

    // Convert RouteConfig from core::config to RuleEngine RoutingRules
    for (const auto& route : config_.router.routes) {
        if (!route.enabled) {
            IPB_LOG_DEBUG(LOG_CAT, "Skipping disabled route: " << route.id);
            continue;
        }

        // Build routing rule using RuleBuilder
        auto builder = core::RuleBuilder()
                           .name(route.name.empty() ? route.id : route.name)
                           .priority(static_cast<core::RulePriority>(route.priority));

        // Set pattern from either enhanced filter or legacy source_pattern
        std::string pattern = core::config::ConfigConverter::get_pattern(route);
        if (!pattern.empty()) {
            builder.match_pattern(pattern);
        }

        // Add quality filter if specified
        if (!route.filter.quality_levels.empty()) {
            for (const auto& quality_str : route.filter.quality_levels) {
                if (quality_str == "GOOD") {
                    builder.match_quality(common::Quality::GOOD);
                } else if (quality_str == "BAD") {
                    builder.match_quality(common::Quality::BAD);
                } else if (quality_str == "UNCERTAIN") {
                    builder.match_quality(common::Quality::UNCERTAIN);
                }
            }
        }

        // Note: Protocol filter by string ID not yet supported in RuleEngine
        // (RuleEngine expects uint16_t protocol codes)

        // Get sink IDs
        auto sink_ids = core::config::ConfigConverter::get_sink_ids(route);
        for (const auto& sink_id : sink_ids) {
            builder.route_to(sink_id);
        }

        // Build and add rule
        auto rule        = builder.build();
        uint32_t rule_id = rule_engine_->add_rule(rule);
        if (rule_id > 0) {
            IPB_LOG_INFO(LOG_CAT, "Added routing rule: " << route.id << " (id=" << rule_id
                                                         << ") -> " << sink_ids.size()
                                                         << " sink(s)");
        } else {
            IPB_LOG_WARN(LOG_CAT, "Failed to add routing rule " << route.id);
        }
    }

    IPB_LOG_INFO(LOG_CAT,
                 "Routing setup complete with " << config_.router.routes.size() << " rules");
    return common::Result<void>();
}

common::Result<void> IPBOrchestrator::apply_routing_rules() {
    // Re-apply routing rules (used during hot reload)
    if (rule_engine_) {
        rule_engine_->clear_rules();
    }
    return setup_routing();
}

std::shared_ptr<common::IProtocolSource> IPBOrchestrator::create_scoop(
    const core::config::ScoopConfig& config) {
    // TODO: Implement scoop creation based on protocol type
    IPB_LOG_DEBUG(LOG_CAT, "Creating scoop: " << config.id << " (type: " << config.name << ")");
    // Scoop implementations would be loaded here based on protocol type
    return nullptr;
}

common::Result<void> IPBOrchestrator::start_scoop(const std::string& scoop_id) {
    auto it = scoops_.find(scoop_id);
    if (it == scoops_.end()) {
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR,
                                    "Scoop not found: " + scoop_id);
    }

    return it->second->connect();
}

common::Result<void> IPBOrchestrator::stop_scoop(const std::string& scoop_id) {
    auto it = scoops_.find(scoop_id);
    if (it == scoops_.end()) {
        return common::Result<void>(common::ErrorCode::UNKNOWN_ERROR,
                                    "Scoop not found: " + scoop_id);
    }

    it->second->disconnect();
    return common::Result<void>();
}

void IPBOrchestrator::setup_mqtt_commands() {
    // TODO: Implement MQTT command setup
}

void IPBOrchestrator::mqtt_command_loop() {
    // TODO: Implement MQTT command loop
}

void IPBOrchestrator::metrics_loop() {
    // TODO: Implement metrics loop
}

// Factory implementations
std::unique_ptr<IPBOrchestrator> OrchestratorFactory::create(const std::string& config_file) {
    return std::make_unique<IPBOrchestrator>(config_file);
}

std::unique_ptr<IPBOrchestrator> OrchestratorFactory::create_default() {
    return std::make_unique<IPBOrchestrator>();
}

std::unique_ptr<IPBOrchestrator> OrchestratorFactory::create_test() {
    auto orchestrator = std::make_unique<IPBOrchestrator>();
    // TODO: Setup test configuration
    return orchestrator;
}

}  // namespace ipb::gate
