#include <iostream>
#include <ipb/sink/syslog/syslog_sink.hpp>
#include <ipb/common/data_point.hpp>

using namespace ipb::sink::syslog;
using namespace ipb::common;

int main() {
    std::cout << "=== IPB Syslog Sink Example ===" << std::endl;
    
    // Create syslog sink with default configuration
    SyslogSink syslog_sink;
    SyslogSinkConfig config;
    
    // Configure for local syslog
    config.connection.facility = SyslogFacility::LOCAL0;
    config.connection.identity = "ipb-example";
    config.format.message_format = SyslogFormat::RFC3164;
    config.format.include_timestamp = true;
    config.format.include_hostname = true;
    
    if (!syslog_sink.configure(config)) {
        std::cerr << "Failed to configure syslog sink" << std::endl;
        return 1;
    }
    
    if (!syslog_sink.start()) {
        std::cerr << "Failed to start syslog sink" << std::endl;
        return 1;
    }
    
    std::cout << "Syslog sink started successfully!" << std::endl;
    
    // Send some test data points
    DataPoint temperature("plant_a/line_1/temperature_01", "modbus", 23.5);
    temperature.set_quality(DataQuality::GOOD);
    
    DataPoint pressure("plant_a/line_1/pressure_01", "opcua", 1.25);
    pressure.set_quality(DataQuality::GOOD);
    
    DataPoint alarm("plant_a/line_1/alarm_high_temp", "modbus", true);
    alarm.set_quality(DataQuality::BAD);
    
    std::cout << "Sending data points to syslog..." << std::endl;
    
    syslog_sink.send(temperature);
    syslog_sink.send(pressure);
    syslog_sink.send(alarm);
    
    // Wait a bit for messages to be sent
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Get statistics
    auto stats = syslog_sink.get_statistics();
    std::cout << "\nSyslog Sink Statistics:" << std::endl;
    std::cout << "  Messages sent: " << stats.messages_sent << std::endl;
    std::cout << "  Messages failed: " << stats.messages_failed << std::endl;
    std::cout << "  Total bytes: " << stats.total_bytes_sent << std::endl;
    
    syslog_sink.stop();
    std::cout << "\n=== Example completed successfully ===" << std::endl;
    
    return 0;
}

