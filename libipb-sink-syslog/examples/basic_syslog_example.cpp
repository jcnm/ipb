#include <iostream>
#include <thread>
#include <chrono>
#include <ipb/sink/syslog/syslog_sink.hpp>
#include <ipb/common/data_point.hpp>

using namespace ipb::sink::syslog;
using namespace ipb::common;

int main() {
    std::cout << "=== IPB Syslog Sink Example ===" << std::endl;

    // Create syslog sink configuration
    SyslogSinkConfig config;
    config.facility = SyslogFacility::LOCAL0;
    config.ident = "ipb-example";
    config.format = SyslogFormat::RFC3164;

    // Create syslog sink with configuration
    SyslogSink syslog_sink(config);

    // Initialize and start the sink
    auto init_result = syslog_sink.initialize("");
    if (!init_result.is_success()) {
        std::cerr << "Failed to initialize syslog sink: " << init_result.message() << std::endl;
        return 1;
    }

    auto start_result = syslog_sink.start();
    if (!start_result.is_success()) {
        std::cerr << "Failed to start syslog sink: " << start_result.message() << std::endl;
        return 1;
    }

    std::cout << "Syslog sink started successfully!" << std::endl;

    // Send some test data points
    Value temp_value;
    temp_value.set(23.5);
    DataPoint temperature("plant_a/line_1/temperature_01", temp_value, 1);
    temperature.set_quality(Quality::GOOD);

    Value pressure_value;
    pressure_value.set(1.25);
    DataPoint pressure("plant_a/line_1/pressure_01", pressure_value, 2);
    pressure.set_quality(Quality::GOOD);

    Value alarm_value;
    alarm_value.set(true);
    DataPoint alarm("plant_a/line_1/alarm_high_temp", alarm_value, 1);
    alarm.set_quality(Quality::BAD);

    std::cout << "Sending data points to syslog..." << std::endl;

    syslog_sink.send_data_point(temperature);
    syslog_sink.send_data_point(pressure);
    syslog_sink.send_data_point(alarm);

    // Wait a bit for messages to be sent
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Get metrics
    auto metrics = syslog_sink.get_metrics();
    std::cout << "\nSyslog Sink Metrics:" << std::endl;
    std::cout << "  Messages sent: " << metrics.messages_sent << std::endl;
    std::cout << "  Messages failed: " << metrics.messages_failed << std::endl;
    std::cout << "  Total bytes: " << metrics.bytes_sent << std::endl;

    syslog_sink.stop();
    std::cout << "\n=== Example completed successfully ===" << std::endl;

    return 0;
}
