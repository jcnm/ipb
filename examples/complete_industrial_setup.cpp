/**
 * @file complete_industrial_setup.cpp
 * @brief Complete industrial setup example demonstrating IPB capabilities
 * 
 * This example shows how to set up a complete industrial data collection
 * and processing pipeline using IPB. It demonstrates:
 * 
 * - Multiple protocol adapters (Modbus, OPC UA, MQTT)
 * - Various data sinks (Kafka, ZeroMQ, Console)
 * - Advanced routing with custom logic
 * - Real-time performance monitoring
 * - Error handling and recovery
 * - Configuration management
 * 
 * Use case: Manufacturing plant with multiple production lines,
 * each equipped with different types of sensors and controllers.
 */

#include "ipb/gate/orchestrator.hpp"
#include "ipb/router/router.hpp"
#include "ipb/adapter/modbus/modbus_adapter.hpp"
#include "ipb/adapter/opcua/opcua_adapter.hpp"
#include "ipb/sink/kafka/kafka_sink.hpp"
#include "ipb/sink/zmq/zmq_sink.hpp"
#include "ipb/sink/console/console_sink.hpp"

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <signal.h>

using namespace ipb;
using namespace std::chrono_literals;

// Global orchestrator for signal handling
std::unique_ptr<gate::Orchestrator> g_orchestrator;

/**
 * @brief Signal handler for graceful shutdown
 */
void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down gracefully..." << std::endl;
    
    if (g_orchestrator) {
        g_orchestrator->handle_signal(signal);
    }
}

/**
 * @brief Custom routing logic for maintenance mode detection
 */
bool check_maintenance_mode(const common::DataPoint& data_point) {
    // Check if any maintenance flag is set
    if (data_point.get_address().find("maintenance") != std::string::npos) {
        if (data_point.get_value().has_value()) {
            auto& value = data_point.get_value().value();
            if (std::holds_alternative<bool>(value)) {
                return std::get<bool>(value);
            }
        }
    }
    return false;
}

/**
 * @brief Custom sink selection for maintenance mode
 */
std::vector<std::string> select_maintenance_sinks(const common::DataPoint& data_point) {
    if (check_maintenance_mode(data_point)) {
        return {"maintenance_console", "maintenance_kafka"};
    }
    return {"normal_kafka", "normal_zmq"};
}

/**
 * @brief Create and configure Modbus adapters for production lines
 */
std::vector<std::pair<std::string, std::shared_ptr<common::IProtocolSource>>> 
create_modbus_adapters() {
    std::vector<std::pair<std::string, std::shared_ptr<common::IProtocolSource>>> adapters;
    
    // Production Line 1 - Temperature and Pressure Monitoring
    {
        auto config = adapter::modbus::ModbusAdapterConfig::create_high_performance();
        config.connection.host = "192.168.1.100";
        config.connection.port = 502;
        config.connection.device_id = 1;
        config.connection.connection_timeout = 5s;
        config.connection.read_timeout = 1s;
        
        // Add temperature sensors
        config.registers.push_back({
            .name = "line1_temp_reactor",
            .address = 40001,
            .type = adapter::modbus::RegisterType::HOLDING_REGISTER,
            .data_type = adapter::modbus::DataType::FLOAT32,
            .polling_interval = 100ms,
            .scaling_factor = 0.1,
            .offset = -273.15  // Convert to Celsius
        });
        
        config.registers.push_back({
            .name = "line1_pressure_main",
            .address = 40003,
            .type = adapter::modbus::RegisterType::HOLDING_REGISTER,
            .data_type = adapter::modbus::DataType::INT16,
            .polling_interval = 200ms,
            .scaling_factor = 0.01  // Convert to bar
        });
        
        // Add flow rate sensor
        config.registers.push_back({
            .name = "line1_flow_rate",
            .address = 40005,
            .type = adapter::modbus::RegisterType::HOLDING_REGISTER,
            .data_type = adapter::modbus::DataType::FLOAT32,
            .polling_interval = 150ms
        });
        
        // Add status coils
        config.coils.push_back({
            .name = "line1_pump_status",
            .address = 10001,
            .polling_interval = 50ms
        });
        
        config.coils.push_back({
            .name = "line1_maintenance_mode",
            .address = 10002,
            .polling_interval = 1s
        });
        
        auto adapter = adapter::modbus::ModbusAdapterFactory::create(config);
        adapters.emplace_back("modbus_line1", std::move(adapter));
    }
    
    // Production Line 2 - Vibration and Speed Monitoring
    {
        auto config = adapter::modbus::ModbusAdapterConfig::create_low_latency();
        config.connection.host = "192.168.1.101";
        config.connection.port = 502;
        config.connection.device_id = 2;
        
        // High-frequency vibration monitoring
        config.registers.push_back({
            .name = "line2_vibration_x",
            .address = 40001,
            .type = adapter::modbus::RegisterType::HOLDING_REGISTER,
            .data_type = adapter::modbus::DataType::FLOAT32,
            .polling_interval = 10ms  // High frequency for vibration
        });
        
        config.registers.push_back({
            .name = "line2_vibration_y",
            .address = 40003,
            .type = adapter::modbus::RegisterType::HOLDING_REGISTER,
            .data_type = adapter::modbus::DataType::FLOAT32,
            .polling_interval = 10ms
        });
        
        config.registers.push_back({
            .name = "line2_motor_speed",
            .address = 40005,
            .type = adapter::modbus::RegisterType::HOLDING_REGISTER,
            .data_type = adapter::modbus::DataType::INT16,
            .polling_interval = 50ms
        });
        
        auto adapter = adapter::modbus::ModbusAdapterFactory::create(config);
        adapters.emplace_back("modbus_line2", std::move(adapter));
    }
    
    return adapters;
}

/**
 * @brief Create and configure OPC UA adapter for SCADA system
 */
std::shared_ptr<common::IProtocolSource> create_opcua_adapter() {
    auto config = adapter::opcua::OPCUAAdapterConfig::create_secure();
    config.endpoint_url = "opc.tcp://192.168.1.200:4840";
    config.connection_timeout = 10s;
    config.session_timeout = 60s;
    
    // Security configuration
    config.security_policy = adapter::opcua::SecurityPolicy::BASIC256SHA256;
    config.security_mode = adapter::opcua::MessageSecurityMode::SIGN_AND_ENCRYPT;
    config.username = "ipb_client";
    config.password = "secure_password";
    
    // Add node subscriptions for process data
    config.node_ids = {
        adapter::opcua::NodeId::parse("ns=2;s=Process.Temperature.Reactor1"),
        adapter::opcua::NodeId::parse("ns=2;s=Process.Pressure.Reactor1"),
        adapter::opcua::NodeId::parse("ns=2;s=Process.FlowRate.Line1"),
        adapter::opcua::NodeId::parse("ns=2;s=Process.FlowRate.Line2"),
        adapter::opcua::NodeId::parse("ns=2;s=Alarms.HighTemperature"),
        adapter::opcua::NodeId::parse("ns=2;s=Alarms.LowPressure"),
        adapter::opcua::NodeId::parse("ns=2;s=System.MaintenanceMode")
    };
    
    // Subscription settings for real-time data
    config.subscription.publishing_interval = 100.0;  // 100ms
    config.subscription.sampling_interval = 50.0;     // 50ms
    config.subscription.queue_size = 20;
    
    return adapter::opcua::OPCUAAdapterFactory::create(config);
}

/**
 * @brief Create and configure data sinks
 */
std::vector<std::pair<std::string, std::shared_ptr<common::IIPBSink>>> 
create_data_sinks() {
    std::vector<std::pair<std::string, std::shared_ptr<common::IIPBSink>>> sinks;
    
    // High-throughput Kafka sink for production data
    {
        auto config = sink::kafka::KafkaSinkConfig::create_high_throughput();
        config.bootstrap_servers = {"kafka-01:9092", "kafka-02:9092", "kafka-03:9092"};
        config.client_id = "ipb-production-sink";
        
        // Security configuration
        config.security_protocol = "SASL_SSL";
        config.sasl_mechanism = "SCRAM-SHA-256";
        config.sasl_username = "ipb_producer";
        config.sasl_password = "kafka_password";
        
        // Topic configuration
        config.topics = {
            {
                .topic_name = "industrial.sensors.temperature",
                .partitioning_strategy = sink::kafka::PartitioningStrategy::HASH_BY_ADDRESS,
                .num_partitions = 6,
                .replication_factor = 3
            },
            {
                .topic_name = "industrial.sensors.pressure",
                .partitioning_strategy = sink::kafka::PartitioningStrategy::HASH_BY_ADDRESS,
                .num_partitions = 6,
                .replication_factor = 3
            },
            {
                .topic_name = "industrial.sensors.flow",
                .partitioning_strategy = sink::kafka::PartitioningStrategy::HASH_BY_PROTOCOL,
                .num_partitions = 3,
                .replication_factor = 3
            }
        };
        
        config.default_topic = {
            .topic_name = "industrial.sensors.default",
            .partitioning_strategy = sink::kafka::PartitioningStrategy::ROUND_ROBIN,
            .num_partitions = 3,
            .replication_factor = 2
        };
        
        // Performance optimization
        config.delivery_guarantee = sink::kafka::DeliveryGuarantee::AT_LEAST_ONCE;
        config.compression = sink::kafka::CompressionType::SNAPPY;
        config.batch_size = 32768;
        config.linger_ms = 5ms;
        config.max_batch_size = 1000;
        config.flush_interval = 100ms;
        
        auto sink = sink::kafka::KafkaSinkFactory::create(config);
        sinks.emplace_back("kafka_production", std::move(sink));
    }
    
    // Low-latency ZeroMQ sink for real-time alerts
    {
        auto config = sink::zmq::ZMQSinkConfig::create_low_latency();
        config.socket_type = sink::zmq::SocketType::PUSH;
        
        config.endpoints = {
            {
                .transport = sink::zmq::Transport::TCP,
                .address = "192.168.1.300",
                .port = 5555,
                .bind = false
            },
            {
                .transport = sink::zmq::Transport::TCP,
                .address = "192.168.1.301",
                .port = 5555,
                .bind = false
            }
        };
        
        // Security with CURVE encryption
        config.security_mechanism = sink::zmq::SecurityMechanism::CURVE;
        config.curve_server_key = "server_public_key_here";
        config.curve_public_key = "client_public_key_here";
        config.curve_secret_key = "client_secret_key_here";
        
        // Ultra-low latency settings
        config.send_timeout = 100ms;
        config.flush_interval = 1ms;
        config.enable_zero_copy = true;
        config.serialization_format = sink::zmq::SerializationFormat::MSGPACK;
        
        auto sink = sink::zmq::ZMQSinkFactory::create(config);
        sinks.emplace_back("zmq_realtime", std::move(sink));
    }
    
    // Console sink for debugging and monitoring
    {
        auto config = sink::console::ConsoleSinkConfig::create_debug();
        config.output_format = sink::console::OutputFormat::COLORED;
        config.enable_filtering = true;
        config.address_filters = {"*.temperature.*", "*.pressure.*", "*maintenance*"};
        config.quality_filter = {common::Quality::GOOD, common::Quality::UNCERTAIN};
        
        auto sink = sink::console::ConsoleSinkFactory::create(config);
        sinks.emplace_back("console_debug", std::move(sink));
    }
    
    // Maintenance console sink
    {
        auto config = sink::console::ConsoleSinkConfig::create_production();
        config.output_format = sink::console::OutputFormat::JSON;
        config.enable_filtering = true;
        config.address_filters = {"*maintenance*", "*alarm*", "*error*"};
        
        auto sink = sink::console::ConsoleSinkFactory::create(config);
        sinks.emplace_back("maintenance_console", std::move(sink));
    }
    
    return sinks;
}

/**
 * @brief Create and configure routing rules
 */
std::vector<router::RoutingRule> create_routing_rules() {
    std::vector<router::RoutingRule> rules;
    
    // High-priority alarm routing
    {
        auto rule = router::RoutingRuleBuilder()
            .name("critical_alarms")
            .priority(router::RoutingPriority::HIGHEST)
            .match_pattern(".*alarm.*|.*emergency.*")
            .match_quality(common::Quality::GOOD)
            .route_to({"zmq_realtime", "console_debug"})
            .load_balance(router::LoadBalanceStrategy::BROADCAST)
            .build();
        
        rules.push_back(std::move(rule));
    }
    
    // Temperature monitoring with threshold-based routing
    {
        router::ValueCondition high_temp_condition;
        high_temp_condition.op = router::ValueCondition::Operator::GREATER_THAN;
        high_temp_condition.reference_value = common::Value{80.0};
        
        auto rule = router::RoutingRuleBuilder()
            .name("high_temperature_alert")
            .priority(router::RoutingPriority::HIGH)
            .match_addresses({"line1_temp_reactor", "line2_temp_reactor"})
            .match_value_condition(high_temp_condition)
            .route_to({"zmq_realtime", "kafka_production"})
            .load_balance(router::LoadBalanceStrategy::BROADCAST)
            .build();
        
        rules.push_back(std::move(rule));
    }
    
    // Normal sensor data routing
    {
        auto rule = router::RoutingRuleBuilder()
            .name("normal_sensor_data")
            .priority(router::RoutingPriority::NORMAL)
            .match_protocols({1, 2})  // Modbus and OPC UA
            .match_quality(common::Quality::GOOD)
            .route_to({"kafka_production"})
            .load_balance(router::LoadBalanceStrategy::ROUND_ROBIN)
            .enable_batching(100, 10ms)
            .build();
        
        rules.push_back(std::move(rule));
    }
    
    // Vibration data - high frequency, low latency
    {
        auto rule = router::RoutingRuleBuilder()
            .name("vibration_monitoring")
            .priority(router::RoutingPriority::HIGH)
            .match_pattern(".*vibration.*")
            .route_to({"zmq_realtime"})
            .build();  // No batching for real-time vibration data
        
        rules.push_back(std::move(rule));
    }
    
    // Maintenance mode routing
    {
        auto rule = router::RoutingRuleBuilder()
            .name("maintenance_mode")
            .priority(router::RoutingPriority::NORMAL)
            .match_custom(check_maintenance_mode)
            .custom_target_selector(select_maintenance_sinks)
            .build();
        
        rules.push_back(std::move(rule));
    }
    
    // Fallback rule for unmatched data
    {
        auto rule = router::RoutingRuleBuilder()
            .name("fallback_routing")
            .priority(router::RoutingPriority::LOWEST)
            .route_to({"console_debug"})
            .build();
        
        rules.push_back(std::move(rule));
    }
    
    return rules;
}

/**
 * @brief Setup performance monitoring
 */
void setup_monitoring(gate::Orchestrator& orchestrator) {
    // Enable Prometheus metrics
    auto prometheus_result = orchestrator.enable_prometheus_metrics(9090);
    if (!prometheus_result.is_success()) {
        std::cerr << "Warning: Failed to enable Prometheus metrics: " 
                  << prometheus_result.error_message() << std::endl;
    } else {
        std::cout << "Prometheus metrics enabled on port 9090" << std::endl;
    }
    
    // Enable performance monitoring
    auto monitoring_result = orchestrator.enable_performance_monitoring(true);
    if (!monitoring_result.is_success()) {
        std::cerr << "Warning: Failed to enable performance monitoring: " 
                  << monitoring_result.error_message() << std::endl;
    }
    
    // Set monitoring interval
    orchestrator.set_monitoring_interval(1s);
    
    std::cout << "Performance monitoring configured" << std::endl;
}

/**
 * @brief Print system status periodically
 */
void print_system_status(const gate::Orchestrator& orchestrator) {
    while (orchestrator.is_running()) {
        std::this_thread::sleep_for(10s);
        
        auto metrics = orchestrator.get_system_metrics();
        auto health = orchestrator.get_system_health();
        
        std::cout << "\n=== System Status ===" << std::endl;
        std::cout << "Health: " << static_cast<int>(health) << std::endl;
        std::cout << "Messages/sec: " << metrics.messages_per_second << std::endl;
        std::cout << "CPU Usage: " << metrics.system_cpu_usage << "%" << std::endl;
        std::cout << "Memory Usage: " << (metrics.system_memory_usage / 1024 / 1024) << " MB" << std::endl;
        std::cout << "Active Components: " << metrics.component_metrics.size() << std::endl;
        std::cout << "Pending Tasks: " << metrics.pending_tasks << std::endl;
        std::cout << "Missed Deadlines: " << metrics.tasks_missed_deadline << std::endl;
        
        // Component-specific metrics
        for (const auto& [component_id, component_info] : metrics.component_metrics) {
            std::cout << "  " << component_id << ": " 
                      << component_info.successful_operations << " ops, "
                      << "avg " << component_info.avg_processing_time.count() << "ns" << std::endl;
        }
        
        std::cout << "=====================" << std::endl;
    }
}

/**
 * @brief Main function demonstrating complete industrial setup
 */
int main(int argc, char* argv[]) {
    try {
        // Setup signal handling
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        
        std::cout << "Starting IPB Industrial Setup Example..." << std::endl;
        
        // Create orchestrator configuration
        gate::OrchestratorConfig config;
        config.instance_name = "industrial_gateway_example";
        config.enable_realtime_scheduling = true;
        config.realtime_priority = 80;
        config.enable_edf_scheduling = true;
        config.default_deadline_offset = 500us;
        config.worker_thread_count = 8;
        config.enable_monitoring = true;
        config.enable_prometheus_metrics = true;
        config.prometheus_port = 9090;
        
        // Create orchestrator
        g_orchestrator = gate::OrchestratorFactory::create_with_config(config);
        if (!g_orchestrator) {
            std::cerr << "Failed to create orchestrator" << std::endl;
            return 1;
        }
        
        std::cout << "Orchestrator created successfully" << std::endl;
        
        // Initialize orchestrator
        auto init_result = g_orchestrator->initialize("");
        if (!init_result.is_success()) {
            std::cerr << "Failed to initialize orchestrator: " 
                      << init_result.error_message() << std::endl;
            return 1;
        }
        
        // Create and register protocol adapters
        std::cout << "Creating protocol adapters..." << std::endl;
        
        auto modbus_adapters = create_modbus_adapters();
        for (auto& [adapter_id, adapter] : modbus_adapters) {
            auto register_result = g_orchestrator->register_adapter(adapter_id, adapter);
            if (!register_result.is_success()) {
                std::cerr << "Failed to register adapter " << adapter_id << ": " 
                          << register_result.error_message() << std::endl;
                return 1;
            }
            std::cout << "Registered adapter: " << adapter_id << std::endl;
        }
        
        auto opcua_adapter = create_opcua_adapter();
        auto opcua_register_result = g_orchestrator->register_adapter("opcua_scada", opcua_adapter);
        if (!opcua_register_result.is_success()) {
            std::cerr << "Failed to register OPC UA adapter: " 
                      << opcua_register_result.error_message() << std::endl;
            return 1;
        }
        std::cout << "Registered OPC UA adapter" << std::endl;
        
        // Create and register data sinks
        std::cout << "Creating data sinks..." << std::endl;
        
        auto sinks = create_data_sinks();
        for (auto& [sink_id, sink] : sinks) {
            auto register_result = g_orchestrator->register_sink(sink_id, sink);
            if (!register_result.is_success()) {
                std::cerr << "Failed to register sink " << sink_id << ": " 
                          << register_result.error_message() << std::endl;
                return 1;
            }
            std::cout << "Registered sink: " << sink_id << std::endl;
        }
        
        // Create and configure router
        std::cout << "Creating router..." << std::endl;
        
        auto router_config = router::RouterConfig::create_realtime();
        auto router = router::RouterFactory::create(router_config);
        
        // Add routing rules
        auto rules = create_routing_rules();
        for (auto& rule : rules) {
            auto add_result = router->add_routing_rule(rule);
            if (!add_result.is_success()) {
                std::cerr << "Failed to add routing rule " << rule.name << ": " 
                          << add_result.error_message() << std::endl;
                return 1;
            }
            std::cout << "Added routing rule: " << rule.name << std::endl;
        }
        
        // Register router
        auto router_register_result = g_orchestrator->register_router(router);
        if (!router_register_result.is_success()) {
            std::cerr << "Failed to register router: " 
                      << router_register_result.error_message() << std::endl;
            return 1;
        }
        std::cout << "Router registered successfully" << std::endl;
        
        // Setup monitoring
        setup_monitoring(*g_orchestrator);
        
        // Start the orchestrator
        std::cout << "Starting orchestrator..." << std::endl;
        auto start_result = g_orchestrator->start();
        if (!start_result.is_success()) {
            std::cerr << "Failed to start orchestrator: " 
                      << start_result.error_message() << std::endl;
            return 1;
        }
        
        std::cout << "IPB Industrial Gateway started successfully!" << std::endl;
        std::cout << "Prometheus metrics available at: http://localhost:9090/metrics" << std::endl;
        std::cout << "Press Ctrl+C to shutdown gracefully..." << std::endl;
        
        // Start status monitoring thread
        std::thread status_thread(print_system_status, std::cref(*g_orchestrator));
        
        // Main loop - wait for shutdown signal
        while (g_orchestrator->is_running()) {
            std::this_thread::sleep_for(100ms);
        }
        
        // Wait for status thread to finish
        if (status_thread.joinable()) {
            status_thread.join();
        }
        
        std::cout << "Shutting down..." << std::endl;
        
        // Stop orchestrator
        auto stop_result = g_orchestrator->stop();
        if (!stop_result.is_success()) {
            std::cerr << "Warning: Error during shutdown: " 
                      << stop_result.error_message() << std::endl;
        }
        
        // Final shutdown
        auto shutdown_result = g_orchestrator->shutdown();
        if (!shutdown_result.is_success()) {
            std::cerr << "Warning: Error during final shutdown: " 
                      << shutdown_result.error_message() << std::endl;
        }
        
        std::cout << "IPB Industrial Gateway stopped successfully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception occurred" << std::endl;
        return 1;
    }
    
    return 0;
}

