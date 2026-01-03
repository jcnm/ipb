#include <ipb/common/data_point.hpp>
#include <ipb/sink/mqtt/mqtt_sink.hpp>

#include <iostream>

using namespace ipb::sink::mqtt;
using namespace ipb::common;

int main() {
    std::cout << "=== IPB MQTT Sink Example ===" << std::endl;

    // Create MQTT sink with default configuration
    MQTTSink mqtt_sink;
    MQTTSinkConfig config;

    // Configure for local MQTT broker
    config.connection.broker_url         = "tcp://localhost:1883";
    config.connection.client_id          = "ipb-example-client";
    config.connection.username           = "";
    config.connection.password           = "";
    config.connection.keep_alive_seconds = 60;
    config.connection.clean_session      = true;

    // Topic configuration (in messages struct)
    config.messages.base_topic     = "ipb/industrial/data";
    config.messages.topic_strategy = MQTTTopicStrategy::HIERARCHICAL;

    // Message configuration
    config.messages.qos                = QoS::AT_LEAST_ONCE;
    config.messages.retain             = false;
    config.messages.format             = MQTTMessageFormat::JSON;
    config.messages.enable_compression = false;

    // Performance configuration
    config.performance.enable_batching  = false;
    config.performance.batch_size       = 1;
    config.performance.batch_timeout    = std::chrono::milliseconds{1000};
    config.performance.thread_pool_size = 2;
    config.performance.queue_size       = 1000;

    if (!mqtt_sink.configure(config)) {
        std::cerr << "Failed to configure MQTT sink" << std::endl;
        return 1;
    }

    if (!mqtt_sink.start()) {
        std::cerr << "Failed to start MQTT sink" << std::endl;
        std::cerr << "Make sure MQTT broker is running on localhost:1883" << std::endl;
        return 1;
    }

    std::cout << "MQTT sink started successfully!" << std::endl;
    std::cout << "Publishing to broker: " << config.connection.broker_url << std::endl;
    std::cout << "Base topic: " << config.messages.base_topic << std::endl;

    // Send some test data points
    DataPoint temperature;
    temperature.set_address("plant_a/line_1/temperature_01");
    temperature.set_protocol_id(1);  // modbus
    temperature.set_value(23.5);
    temperature.set_quality(Quality::GOOD);

    DataPoint pressure;
    pressure.set_address("plant_a/line_1/pressure_01");
    pressure.set_protocol_id(2);  // opcua
    pressure.set_value(1.25);
    pressure.set_quality(Quality::GOOD);

    DataPoint alarm;
    alarm.set_address("plant_a/line_1/alarm_high_temp");
    alarm.set_protocol_id(1);  // modbus
    alarm.set_value(true);
    alarm.set_quality(Quality::BAD);

    std::cout << "\nSending data points to MQTT broker..." << std::endl;

    mqtt_sink.send_data_point(temperature);
    std::cout << "  Sent temperature reading: 23.5°C" << std::endl;

    mqtt_sink.send_data_point(pressure);
    std::cout << "  Sent pressure reading: 1.25 bar" << std::endl;

    mqtt_sink.send_data_point(alarm);
    std::cout << "  Sent alarm status: ACTIVE" << std::endl;

    // Wait a bit for messages to be sent
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Get statistics
    const auto& stats = mqtt_sink.get_statistics();
    std::cout << "\nMQTT Sink Statistics:" << std::endl;
    std::cout << "  Messages sent: " << stats.messages_sent.load() << std::endl;
    std::cout << "  Messages failed: " << stats.messages_failed.load() << std::endl;
    std::cout << "  Total bytes: " << stats.bytes_sent.load() << std::endl;
    std::cout << "  Connection status: "
              << (stats.is_connected.load() ? "Connected" : "Disconnected") << std::endl;

    mqtt_sink.stop();
    std::cout << "\n=== Example completed successfully ===" << std::endl;
    std::cout << "\nTo monitor messages, use:" << std::endl;
    std::cout << "  mosquitto_sub -h localhost -t \"ipb/industrial/data/#\"" << std::endl;

    return 0;
}
