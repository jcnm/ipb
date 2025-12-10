#include <iostream>
#include <ipb/common/data_point.hpp>

using namespace ipb::common;

int main() {
    std::cout << "=== IPB Common Library - DataPoint Example ===" << std::endl;
    
    // Create a temperature sensor data point
    DataPoint temperature("plant_a/line_1/temperature_01", "modbus", 23.5);
    temperature.set_quality(DataQuality::GOOD);
    
    std::cout << "Temperature sensor:" << std::endl;
    std::cout << "  Address: " << temperature.get_address() << std::endl;
    std::cout << "  Protocol: " << temperature.get_protocol_id() << std::endl;
    std::cout << "  Value: " << temperature.get_value<double>() << "°C" << std::endl;
    std::cout << "  Quality: " << static_cast<int>(temperature.get_quality()) << std::endl;
    std::cout << "  Timestamp: " << temperature.get_timestamp() << std::endl;
    
    // Create a pressure sensor data point
    DataPoint pressure("plant_a/line_1/pressure_01", "opcua", 1.25);
    pressure.set_quality(DataQuality::GOOD);
    
    std::cout << "\nPressure sensor:" << std::endl;
    std::cout << "  Address: " << pressure.get_address() << std::endl;
    std::cout << "  Protocol: " << pressure.get_protocol_id() << std::endl;
    std::cout << "  Value: " << pressure.get_value<double>() << " bar" << std::endl;
    std::cout << "  Quality: " << static_cast<int>(pressure.get_quality()) << std::endl;
    
    // Create a boolean alarm data point
    DataPoint alarm("plant_a/line_1/alarm_high_temp", "modbus", true);
    alarm.set_quality(DataQuality::BAD);
    
    std::cout << "\nAlarm status:" << std::endl;
    std::cout << "  Address: " << alarm.get_address() << std::endl;
    std::cout << "  Protocol: " << alarm.get_protocol_id() << std::endl;
    std::cout << "  Value: " << (alarm.get_value<bool>() ? "ACTIVE" : "INACTIVE") << std::endl;
    std::cout << "  Quality: " << static_cast<int>(alarm.get_quality()) << std::endl;
    
    std::cout << "\n=== Example completed successfully ===" << std::endl;
    return 0;
}

