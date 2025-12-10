#include <ipb/router/router.hpp>
#include <algorithm>
#include <chrono>

namespace ipb {
namespace router {

Router::Router() 
    : running_(false)
    , stats_{}
{
    // Initialize statistics
    stats_.messages_routed = 0;
    stats_.messages_dropped = 0;
    stats_.total_latency_us = 0;
    stats_.start_time = std::chrono::steady_clock::now();
}

Router::~Router() {
    stop();
}

bool Router::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (running_) {
        return true;
    }
    
    running_ = true;
    stats_.start_time = std::chrono::steady_clock::now();
    
    // Start worker threads
    for (size_t i = 0; i < std::thread::hardware_concurrency(); ++i) {
        worker_threads_.emplace_back(&Router::worker_thread, this);
    }
    
    return true;
}

void Router::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    
    // Notify all workers
    condition_.notify_all();
    
    // Join worker threads
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
}

bool Router::add_rule(const RoutingRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check for duplicate rule names
    auto it = std::find_if(rules_.begin(), rules_.end(),
        [&rule](const RoutingRule& r) { return r.get_name() == rule.get_name(); });
    
    if (it != rules_.end()) {
        return false; // Rule with same name already exists
    }
    
    rules_.push_back(rule);
    return true;
}

bool Router::remove_rule(const std::string& rule_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(rules_.begin(), rules_.end(),
        [&rule_name](const RoutingRule& r) { return r.get_name() == rule_name; });
    
    if (it != rules_.end()) {
        rules_.erase(it);
        return true;
    }
    
    return false;
}

void Router::route_message(const ipb::common::DataPoint& data_point) {
    auto start_time = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        message_queue_.push(data_point);
    }
    
    condition_.notify_one();
    
    // Update statistics
    auto end_time = std::chrono::steady_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.messages_routed++;
    stats_.total_latency_us += latency.count();
}

RouterStatistics Router::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    RouterStatistics stats = stats_;
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - stats_.start_time);
    stats.uptime_seconds = uptime.count();
    
    if (stats_.messages_routed > 0) {
        stats.average_latency_us = stats_.total_latency_us / stats_.messages_routed;
    }
    
    return stats;
}

void Router::worker_thread() {
    while (true) {
        ipb::common::DataPoint data_point("", "", 0);
        bool has_message = false;
        
        // Wait for message or stop signal
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { return !message_queue_.empty() || !running_; });
            
            if (!running_ && message_queue_.empty()) {
                break;
            }
            
            if (!message_queue_.empty()) {
                data_point = message_queue_.front();
                message_queue_.pop();
                has_message = true;
            }
        }
        
        if (has_message) {
            process_message(data_point);
        }
    }
}

void Router::process_message(const ipb::common::DataPoint& data_point) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    bool routed = false;
    
    // Apply routing rules
    for (const auto& rule : rules_) {
        if (rule.matches(data_point)) {
            // Route to destinations specified by the rule
            auto destinations = rule.get_destinations();
            for (const auto& dest : destinations) {
                // TODO: Implement actual routing to sinks
                // For now, just mark as routed
                routed = true;
            }
        }
    }
    
    if (!routed) {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.messages_dropped++;
    }
}

} // namespace router
} // namespace ipb

