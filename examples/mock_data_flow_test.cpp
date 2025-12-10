/**
 * @file mock_data_flow_test.cpp
 * @brief Example demonstrating mock data flow through IPB router to MQTT and Console sinks
 * 
 * This example shows how to:
 * 1. Create mock data sources that simulate industrial protocols
 * 2. Configure the IPB router with routing rules
 * 3. Setup MQTT and Console sinks
 * 4. Route data from sources to sinks based on configurable rules
 * 5. Monitor performance and statistics
 */

#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/interfaces.hpp"
#include "ipb/router/router.hpp"
#include "ipb/sink/mqtt/mqtt_sink.hpp"
#include "ipb/sink/console/console_sink.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <vector>
#include <memory>
#include <atomic>
#include <signal.h>

using namespace ipb;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down gracefully..." << std::endl;
    g_running.store(false);
}

/**
 * Mock data source that simulates industrial sensor data
 */
class MockIndustrialSource {
public:
    MockIndustrialSource(const std::string& protocol_id, 
                        const std::string& base_address,
                        std::chrono::milliseconds update_interval)
        : protocol_id_(protocol_id)
        , base_address_(base_address)
        , update_interval_(update_interval)
        , running_(false)
        , generator_(std::random_device{}())
        , temperature_dist_(15.0, 35.0)
        , pressure_dist_(1.0, 5.0)
        , flow_dist_(10.0, 100.0)
        , bool_dist_(0.0, 1.0) {
    }
    
    ~MockIndustrialSource() {
        stop();
    }
    
    void start() {
        if (running_.load()) return;
        
        running_.store(true);
        worker_thread_ = std::thread(&MockIndustrialSource::worker_loop, this);
        
        std::cout << "Started mock source: " << protocol_id_ 
                  << " (update interval: " << update_interval_.count() << "ms)" << std::endl;
    }
    
    void stop() {
        if (!running_.load()) return;
        
        running_.store(false);
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        
        std::cout << "Stopped mock source: " << protocol_id_ << std::endl;
    }
    
    void set_data_callback(std::function<void(const common::DataPoint&)> callback) {
        data_callback_ = callback;
    }
    
    // Statistics
    uint64_t get_messages_generated() const { return messages_generated_.load(); }
    
private:
    void worker_loop() {
        while (running_.load() && g_running.load()) {
            generate_sensor_data();
            std::this_thread::sleep_for(update_interval_);
        }
    }
    
    void generate_sensor_data() {
        auto now = std::chrono::system_clock::now();
        
        // Generate temperature sensor data
        {
            common::DataPoint temp_point;
            temp_point.set_protocol_id(protocol_id_);
            temp_point.set_address(base_address_ + "/temperature_01");
            temp_point.set_timestamp(now);
            temp_point.set_quality(common::DataQuality::GOOD);
            temp_point.set_value(temperature_dist_(generator_));
            
            if (data_callback_) {
                data_callback_(temp_point);
            }
            messages_generated_.fetch_add(1);
        }
        
        // Generate pressure sensor data
        {
            common::DataPoint pressure_point;
            pressure_point.set_protocol_id(protocol_id_);
            pressure_point.set_address(base_address_ + "/pressure_01");
            pressure_point.set_timestamp(now);
            pressure_point.set_quality(common::DataQuality::GOOD);
            pressure_point.set_value(pressure_dist_(generator_));
            
            if (data_callback_) {
                data_callback_(pressure_point);
            }
            messages_generated_.fetch_add(1);
        }
        
        // Generate flow sensor data
        {
            common::DataPoint flow_point;
            flow_point.set_protocol_id(protocol_id_);
            flow_point.set_address(base_address_ + "/flow_rate_01");
            flow_point.set_timestamp(now);
            flow_point.set_quality(common::DataQuality::GOOD);
            flow_point.set_value(flow_dist_(generator_));
            
            if (data_callback_) {
                data_callback_(flow_point);
            }
            messages_generated_.fetch_add(1);
        }
        
        // Generate pump status (boolean)
        {
            common::DataPoint pump_point;
            pump_point.set_protocol_id(protocol_id_);
            pump_point.set_address(base_address_ + "/pump_status");
            pump_point.set_timestamp(now);
            pump_point.set_quality(common::DataQuality::GOOD);
            pump_point.set_value(bool_dist_(generator_) > 0.7); // 30% chance of being on
            
            if (data_callback_) {
                data_callback_(pump_point);
            }
            messages_generated_.fetch_add(1);
        }
        
        // Occasionally generate alarm data
        if (bool_dist_(generator_) > 0.95) { // 5% chance
            common::DataPoint alarm_point;
            alarm_point.set_protocol_id(protocol_id_);
            alarm_point.set_address(base_address_ + "/alarm_high_temp");
            alarm_point.set_timestamp(now);
            alarm_point.set_quality(common::DataQuality::GOOD);
            alarm_point.set_value(true);
            
            if (data_callback_) {
                data_callback_(alarm_point);
            }
            messages_generated_.fetch_add(1);
        }
    }
    
    std::string protocol_id_;
    std::string base_address_;
    std::chrono::milliseconds update_interval_;
    
    std::atomic<bool> running_;
    std::thread worker_thread_;
    std::function<void(const common::DataPoint&)> data_callback_;
    
    // Random number generation
    std::mt19937 generator_;
    std::uniform_real_distribution<double> temperature_dist_;
    std::uniform_real_distribution<double> pressure_dist_;
    std::uniform_real_distribution<double> flow_dist_;
    std::uniform_real_distribution<double> bool_dist_;
    
    std::atomic<uint64_t> messages_generated_{0};
};

/**
 * Statistics monitor for tracking system performance
 */
class StatisticsMonitor {
public:
    StatisticsMonitor(std::chrono::seconds interval = std::chrono::seconds{10})
        : interval_(interval), running_(false) {
    }
    
    ~StatisticsMonitor() {
        stop();
    }
    
    void start() {
        if (running_.load()) return;
        
        running_.store(true);
        monitor_thread_ = std::thread(&StatisticsMonitor::monitor_loop, this);
        
        std::cout << "Started statistics monitor (interval: " << interval_.count() << "s)" << std::endl;
    }
    
    void stop() {
        if (!running_.load()) return;
        
        running_.store(false);
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
        
        std::cout << "Stopped statistics monitor" << std::endl;
    }
    
    void add_source(const std::string& name, MockIndustrialSource* source) {
        sources_[name] = source;
    }
    
    void add_sink(const std::string& name, common::IIPBSink* sink) {
        sinks_[name] = sink;
    }
    
    void add_router(router::IPBRouter* router) {
        router_ = router;
    }
    
private:
    void monitor_loop() {
        auto last_time = std::chrono::steady_clock::now();
        
        while (running_.load() && g_running.load()) {
            std::this_thread::sleep_for(interval_);
            
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                current_time - last_time
            );
            
            print_statistics(elapsed);
            last_time = current_time;
        }
    }
    
    void print_statistics(std::chrono::seconds elapsed) {
        std::cout << "\n=== IPB System Statistics (last " << elapsed.count() << "s) ===" << std::endl;
        
        // Source statistics
        std::cout << "\nData Sources:" << std::endl;
        for (const auto& [name, source] : sources_) {
            std::cout << "  " << name << ": " 
                      << source->get_messages_generated() << " messages generated" << std::endl;
        }
        
        // Router statistics
        if (router_) {
            auto router_metrics = router_->get_metrics();
            std::cout << "\nRouter:" << std::endl;
            std::cout << "  Messages routed: " << router_metrics.messages_routed << std::endl;
            std::cout << "  Messages failed: " << router_metrics.messages_failed << std::endl;
            std::cout << "  Active rules: " << router_metrics.active_rules << std::endl;
            std::cout << "  Avg processing time: " 
                      << router_metrics.avg_processing_time.count() << "ns" << std::endl;
        }
        
        // Sink statistics
        std::cout << "\nSinks:" << std::endl;
        for (const auto& [name, sink] : sinks_) {
            auto metrics = sink->get_metrics();
            std::cout << "  " << name << ":" << std::endl;
            std::cout << "    Messages sent: " << metrics.messages_sent << std::endl;
            std::cout << "    Messages failed: " << metrics.messages_failed << std::endl;
            std::cout << "    Bytes sent: " << metrics.bytes_sent << std::endl;
            std::cout << "    Connected: " << (metrics.is_connected ? "Yes" : "No") << std::endl;
            std::cout << "    Healthy: " << (metrics.is_healthy ? "Yes" : "No") << std::endl;
            std::cout << "    Avg processing time: " 
                      << metrics.avg_processing_time.count() << "ns" << std::endl;
        }
        
        std::cout << "================================================\n" << std::endl;
    }
    
    std::chrono::seconds interval_;
    std::atomic<bool> running_;
    std::thread monitor_thread_;
    
    std::unordered_map<std::string, MockIndustrialSource*> sources_;
    std::unordered_map<std::string, common::IIPBSink*> sinks_;
    router::IPBRouter* router_ = nullptr;
};

/**
 * Main test application
 */
int main(int argc, char* argv[]) {
    // Setup signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "=== IPB Mock Data Flow Test ===" << std::endl;
    std::cout << "This example demonstrates data flow from mock sources through the IPB router to MQTT and Console sinks." << std::endl;
    std::cout << "Press Ctrl+C to stop gracefully.\n" << std::endl;
    
    try {
        // 1. Create and configure MQTT sink
        std::cout << "1. Setting up MQTT sink..." << std::endl;
        auto mqtt_config = sink::mqtt::MQTTSinkConfig::create_high_throughput();
        mqtt_config.connection.broker_url = "tcp://localhost:1883";
        mqtt_config.connection.client_id = "ipb_test_client";
        mqtt_config.messages.base_topic = "ipb/industrial/data";
        mqtt_config.messages.topic_strategy = sink::mqtt::MQTTTopicStrategy::HIERARCHICAL;
        mqtt_config.sink_id = "mqtt_industrial";
        
        auto mqtt_sink = std::make_unique<sink::mqtt::MQTTSink>(mqtt_config);
        auto mqtt_init_result = mqtt_sink->initialize();
        if (!mqtt_init_result.is_success()) {
            std::cout << "Warning: Failed to initialize MQTT sink: " 
                      << mqtt_init_result.get_error() << std::endl;
            std::cout << "Continuing without MQTT sink..." << std::endl;
            mqtt_sink.reset();
        } else {
            auto mqtt_start_result = mqtt_sink->start();
            if (!mqtt_start_result.is_success()) {
                std::cout << "Warning: Failed to start MQTT sink: " 
                          << mqtt_start_result.get_error() << std::endl;
                mqtt_sink.reset();
            } else {
                std::cout << "MQTT sink started successfully!" << std::endl;
            }
        }
        
        // 2. Create and configure Console sink
        std::cout << "2. Setting up Console sink..." << std::endl;
        auto console_config = sink::console::ConsoleSinkConfig::create_debug();
        console_config.format = sink::console::ConsoleFormat::COLORED;
        console_config.enable_file_output = true;
        console_config.output_file = "/tmp/ipb_test_output.log";
        console_config.sink_id = "console_debug";
        
        auto console_sink = std::make_unique<sink::console::ConsoleSink>(console_config);
        auto console_init_result = console_sink->initialize();
        if (!console_init_result.is_success()) {
            std::cerr << "Failed to initialize Console sink: " 
                      << console_init_result.get_error() << std::endl;
            return 1;
        }
        
        auto console_start_result = console_sink->start();
        if (!console_start_result.is_success()) {
            std::cerr << "Failed to start Console sink: " 
                      << console_start_result.get_error() << std::endl;
            return 1;
        }
        std::cout << "Console sink started successfully!" << std::endl;
        
        // 3. Create and configure router
        std::cout << "3. Setting up IPB router..." << std::endl;
        auto router_config = router::IPBRouterConfig::create_high_performance();
        router_config.enable_statistics = true;
        router_config.statistics_interval = std::chrono::seconds{5};
        
        auto ipb_router = std::make_unique<router::IPBRouter>(router_config);
        auto router_init_result = ipb_router->initialize();
        if (!router_init_result.is_success()) {
            std::cerr << "Failed to initialize router: " 
                      << router_init_result.get_error() << std::endl;
            return 1;
        }
        
        // Register sinks with router
        if (mqtt_sink) {
            auto mqtt_register_result = ipb_router->register_sink("mqtt_industrial", mqtt_sink.get());
            if (!mqtt_register_result.is_success()) {
                std::cout << "Warning: Failed to register MQTT sink: " 
                          << mqtt_register_result.get_error() << std::endl;
            }
        }
        
        auto console_register_result = ipb_router->register_sink("console_debug", console_sink.get());
        if (!console_register_result.is_success()) {
            std::cerr << "Failed to register Console sink: " 
                      << console_register_result.get_error() << std::endl;
            return 1;
        }
        
        // 4. Configure routing rules
        std::cout << "4. Configuring routing rules..." << std::endl;
        
        // Rule 1: Route temperature data to both sinks
        router::RoutingRule temp_rule;
        temp_rule.name = "temperature_routing";
        temp_rule.source_filter.address_pattern = ".*temperature.*";
        temp_rule.source_filter.protocol_ids = {"modbus", "opcua"};
        temp_rule.destinations.push_back({"console_debug", router::RoutingPriority::NORMAL});
        if (mqtt_sink) {
            temp_rule.destinations.push_back({"mqtt_industrial", router::RoutingPriority::HIGH});
        }
        temp_rule.enable_batching = false;
        
        auto temp_rule_result = ipb_router->add_routing_rule(temp_rule);
        if (!temp_rule_result.is_success()) {
            std::cerr << "Failed to add temperature routing rule: " 
                      << temp_rule_result.get_error() << std::endl;
        }
        
        // Rule 2: Route alarm data to both sinks with high priority
        router::RoutingRule alarm_rule;
        alarm_rule.name = "alarm_routing";
        alarm_rule.source_filter.address_pattern = ".*alarm.*";
        alarm_rule.destinations.push_back({"console_debug", router::RoutingPriority::CRITICAL});
        if (mqtt_sink) {
            alarm_rule.destinations.push_back({"mqtt_industrial", router::RoutingPriority::CRITICAL});
        }
        alarm_rule.enable_batching = false;
        
        auto alarm_rule_result = ipb_router->add_routing_rule(alarm_rule);
        if (!alarm_rule_result.is_success()) {
            std::cerr << "Failed to add alarm routing rule: " 
                      << alarm_rule_result.get_error() << std::endl;
        }
        
        // Rule 3: Route all other data to console only (with batching)
        router::RoutingRule default_rule;
        default_rule.name = "default_routing";
        default_rule.source_filter.address_pattern = ".*";
        default_rule.destinations.push_back({"console_debug", router::RoutingPriority::LOW});
        default_rule.enable_batching = true;
        default_rule.batch_size = 10;
        default_rule.batch_timeout = std::chrono::milliseconds{2000};
        
        auto default_rule_result = ipb_router->add_routing_rule(default_rule);
        if (!default_rule_result.is_success()) {
            std::cerr << "Failed to add default routing rule: " 
                      << default_rule_result.get_error() << std::endl;
        }
        
        // Start router
        auto router_start_result = ipb_router->start();
        if (!router_start_result.is_success()) {
            std::cerr << "Failed to start router: " 
                      << router_start_result.get_error() << std::endl;
            return 1;
        }
        std::cout << "Router started successfully!" << std::endl;
        
        // 5. Create mock data sources
        std::cout << "5. Setting up mock data sources..." << std::endl;
        
        // Modbus source (fast updates)
        auto modbus_source = std::make_unique<MockIndustrialSource>(
            "modbus", "plant_a/line_1", std::chrono::milliseconds{500}
        );
        
        // OPC UA source (medium updates)
        auto opcua_source = std::make_unique<MockIndustrialSource>(
            "opcua", "plant_b/reactor_1", std::chrono::milliseconds{1000}
        );
        
        // MQTT source (slow updates)
        auto mqtt_source = std::make_unique<MockIndustrialSource>(
            "mqtt", "plant_c/warehouse", std::chrono::milliseconds{2000}
        );
        
        // Connect sources to router
        modbus_source->set_data_callback([&ipb_router](const common::DataPoint& data_point) {
            ipb_router->route_data_point(data_point);
        });
        
        opcua_source->set_data_callback([&ipb_router](const common::DataPoint& data_point) {
            ipb_router->route_data_point(data_point);
        });
        
        mqtt_source->set_data_callback([&ipb_router](const common::DataPoint& data_point) {
            ipb_router->route_data_point(data_point);
        });
        
        // 6. Setup statistics monitoring
        std::cout << "6. Setting up statistics monitoring..." << std::endl;
        auto stats_monitor = std::make_unique<StatisticsMonitor>(std::chrono::seconds{15});
        stats_monitor->add_source("Modbus Source", modbus_source.get());
        stats_monitor->add_source("OPC UA Source", opcua_source.get());
        stats_monitor->add_source("MQTT Source", mqtt_source.get());
        stats_monitor->add_sink("Console Sink", console_sink.get());
        if (mqtt_sink) {
            stats_monitor->add_sink("MQTT Sink", mqtt_sink.get());
        }
        stats_monitor->add_router(ipb_router.get());
        stats_monitor->start();
        
        // 7. Start data generation
        std::cout << "7. Starting data generation..." << std::endl;
        modbus_source->start();
        opcua_source->start();
        mqtt_source->start();
        
        std::cout << "\n=== System is running ===" << std::endl;
        std::cout << "Data is being generated and routed through the system." << std::endl;
        std::cout << "Check the console output and /tmp/ipb_test_output.log for results." << std::endl;
        if (mqtt_sink) {
            std::cout << "MQTT messages are being published to: ipb/industrial/data/*" << std::endl;
        }
        std::cout << "Press Ctrl+C to stop.\n" << std::endl;
        
        // 8. Main loop - wait for shutdown signal
        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
        
        // 9. Graceful shutdown
        std::cout << "\n=== Shutting down system ===" << std::endl;
        
        std::cout << "Stopping data sources..." << std::endl;
        modbus_source->stop();
        opcua_source->stop();
        mqtt_source->stop();
        
        std::cout << "Stopping statistics monitor..." << std::endl;
        stats_monitor->stop();
        
        std::cout << "Stopping router..." << std::endl;
        ipb_router->stop();
        
        std::cout << "Stopping sinks..." << std::endl;
        console_sink->stop();
        if (mqtt_sink) {
            mqtt_sink->stop();
        }
        
        std::cout << "System shutdown complete." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception in main: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

