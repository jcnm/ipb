/**
 * @file bridge.cpp
 * @brief IPB Bridge implementation
 */

#include "bridge.hpp"

#include <algorithm>
#include <thread>

namespace ipb::bridge {

Bridge::Bridge()
    : start_time_(std::chrono::steady_clock::now()), last_watchdog_feed_(start_time_) {}

Bridge::~Bridge() {
    stop();
}

common::Result<void> Bridge::initialize(const BridgeConfig& config) {
    if (state_ != BridgeState::STOPPED) {
        return common::Result<void>(common::ErrorCode::INVALID_STATE, "Bridge already initialized");
    }

    state_ = BridgeState::INITIALIZING;

    // Apply configuration
    watchdog_timeout_  = config.watchdog_timeout;
    round_robin_sinks_ = config.round_robin_sinks;

    // Reserve capacity
    sources_.reserve(config.max_sources);
    sinks_.reserve(config.max_sinks);

    state_ = BridgeState::STOPPED;
    return common::Result<void>();
}

common::Result<void> Bridge::initialize_from_file(const std::string& config_path) {
    // For minimal builds without YAML support
#ifndef IPB_BRIDGE_HAS_YAML
    (void)config_path;
    // Use default configuration
    BridgeConfig config;
    return initialize(config);
#else
    // Load configuration from file (implementation in config.cpp)
    // For now, use defaults
    (void)config_path;
    BridgeConfig config;
    return initialize(config);
#endif
}

common::Result<void> Bridge::start() {
    if (state_ == BridgeState::RUNNING) {
        return common::Result<void>();  // Already running
    }

    if (state_ != BridgeState::STOPPED && state_ != BridgeState::PAUSED) {
        return common::Result<void>(common::ErrorCode::INVALID_STATE,
                                    "Cannot start bridge from current state");
    }

    // Start all sources
    for (auto& source : sources_) {
        auto result = source->start();
        if (!result) {
            handle_error("Failed to start source: " + source->id());
            // Continue anyway - partial operation
        } else {
            stats_.active_sources.fetch_add(1);
        }
    }

    // Start all sinks
    for (auto& sink : sinks_) {
        auto result = sink->start();
        if (!result) {
            handle_error("Failed to start sink: " + sink->id());
            // Continue anyway
        } else {
            stats_.active_sinks.fetch_add(1);
        }
    }

    start_time_         = std::chrono::steady_clock::now();
    last_watchdog_feed_ = start_time_;
    paused_             = false;
    state_              = BridgeState::RUNNING;

    return common::Result<void>();
}

void Bridge::stop() {
    if (state_ == BridgeState::STOPPED || state_ == BridgeState::SHUTDOWN) {
        return;
    }

    state_ = BridgeState::SHUTDOWN;

    // Stop all sources first (stop incoming data)
    for (auto& source : sources_) {
        source->stop();
    }
    stats_.active_sources = 0;

    // Flush and stop sinks
    for (auto& sink : sinks_) {
        sink->flush();  // Best effort flush
        sink->stop();
    }
    stats_.active_sinks = 0;

    state_ = BridgeState::STOPPED;
}

void Bridge::pause() {
    if (state_ == BridgeState::RUNNING) {
        paused_ = true;
        state_  = BridgeState::PAUSED;
    }
}

void Bridge::resume() {
    if (state_ == BridgeState::PAUSED) {
        paused_ = false;
        state_  = BridgeState::RUNNING;
    }
}

void Bridge::run() {
    while (state_ == BridgeState::RUNNING || state_ == BridgeState::PAUSED) {
        if (!tick()) {
            // No work done - sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Update uptime
        auto now    = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
        stats_.uptime_seconds.store(static_cast<uint64_t>(uptime.count()));
    }
}

bool Bridge::tick() {
    if (state_ != BridgeState::RUNNING && state_ != BridgeState::PAUSED) {
        return false;
    }

    // Check watchdog
#ifdef IPB_BRIDGE_WATCHDOG
    auto now = std::chrono::steady_clock::now();
    if (now - last_watchdog_feed_ > watchdog_timeout_) {
        handle_error("Watchdog timeout");
        state_ = BridgeState::ERROR;
        return false;
    }
#endif

    // In a real implementation, this would process queued data
    // For now, data is processed synchronously via callbacks
    return false;  // No work done
}

common::Result<void> Bridge::add_source(std::unique_ptr<DataSource> source) {
    if (!source) {
        return common::Result<void>(common::ErrorCode::INVALID_ARGUMENT, "Null source");
    }

    // Check for duplicate ID
    for (const auto& existing : sources_) {
        if (existing->id() == source->id()) {
            return common::Result<void>(common::ErrorCode::ALREADY_EXISTS,
                                        "Source with ID already exists: " + source->id());
        }
    }

    // Set up callback
    source->set_callback([this](const common::DataPoint& data) { this->data_received(data); });

    sources_.push_back(std::move(source));
    return common::Result<void>();
}

common::Result<void> Bridge::add_sink(std::unique_ptr<DataSink> sink) {
    if (!sink) {
        return common::Result<void>(common::ErrorCode::INVALID_ARGUMENT, "Null sink");
    }

    // Check for duplicate ID
    for (const auto& existing : sinks_) {
        if (existing->id() == sink->id()) {
            return common::Result<void>(common::ErrorCode::ALREADY_EXISTS,
                                        "Sink with ID already exists: " + sink->id());
        }
    }

    sinks_.push_back(std::move(sink));
    return common::Result<void>();
}

common::Result<void> Bridge::remove_source(const std::string& id) {
    auto it = std::find_if(sources_.begin(), sources_.end(),
                           [&id](const auto& s) { return s->id() == id; });

    if (it == sources_.end()) {
        return common::Result<void>(common::ErrorCode::NOT_FOUND, "Source not found: " + id);
    }

    (*it)->stop();
    sources_.erase(it);

    if (state_ == BridgeState::RUNNING) {
        stats_.active_sources.fetch_sub(1);
    }

    return common::Result<void>();
}

common::Result<void> Bridge::remove_sink(const std::string& id) {
    auto it =
        std::find_if(sinks_.begin(), sinks_.end(), [&id](const auto& s) { return s->id() == id; });

    if (it == sinks_.end()) {
        return common::Result<void>(common::ErrorCode::NOT_FOUND, "Sink not found: " + id);
    }

    (*it)->flush();
    (*it)->stop();
    sinks_.erase(it);

    if (state_ == BridgeState::RUNNING) {
        stats_.active_sinks.fetch_sub(1);
    }

    // Adjust round-robin index if needed
    if (current_sink_index_ >= sinks_.size()) {
        current_sink_index_ = 0;
    }

    return common::Result<void>();
}

void Bridge::feed_watchdog() {
    last_watchdog_feed_ = std::chrono::steady_clock::now();
}

bool Bridge::is_healthy() const {
    if (state_ == BridgeState::ERROR) {
        return false;
    }

    // Check if we have at least one source and sink
    if (sources_.empty() || sinks_.empty()) {
        return false;
    }

    // Check if at least one source and sink are running
    bool has_running_source = false;
    bool has_running_sink   = false;

    for (const auto& source : sources_) {
        if (source->is_running()) {
            has_running_source = true;
            break;
        }
    }

    for (const auto& sink : sinks_) {
        if (sink->is_running()) {
            has_running_sink = true;
            break;
        }
    }

    return has_running_source && has_running_sink;
}

void Bridge::data_received(const common::DataPoint& data) {
    stats_.messages_received.fetch_add(1);

    if (paused_) {
        stats_.messages_dropped.fetch_add(1);
        return;
    }

    forward_data(data);
}

void Bridge::forward_data(const common::DataPoint& data) {
    if (sinks_.empty()) {
        stats_.messages_dropped.fetch_add(1);
        return;
    }

    if (round_robin_sinks_) {
        // Send to one sink (round-robin)
        auto& sink  = sinks_[current_sink_index_];
        auto result = sink->send(data);
        if (result) {
            stats_.messages_forwarded.fetch_add(1);
        } else {
            stats_.errors.fetch_add(1);
        }

        current_sink_index_ = (current_sink_index_ + 1) % sinks_.size();
    } else {
        // Send to all sinks
        bool forwarded = false;
        for (auto& sink : sinks_) {
            auto result = sink->send(data);
            if (result) {
                forwarded = true;
            } else {
                stats_.errors.fetch_add(1);
            }
        }

        if (forwarded) {
            stats_.messages_forwarded.fetch_add(1);
        } else {
            stats_.messages_dropped.fetch_add(1);
        }
    }
}

void Bridge::handle_error(const std::string& message) {
    last_error_ = message;
    stats_.errors.fetch_add(1);
}

}  // namespace ipb::bridge
