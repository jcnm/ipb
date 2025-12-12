#include <iostream>
#include <ipb/common/data_point.hpp>

using namespace ipb::common;

// Protocol IDs (example values)
constexpr uint16_t PROTOCOL_MODBUS = 1;
constexpr uint16_t PROTOCOL_OPCUA = 2;

// Helper to create Value with a specific type
template<typename T>
Value make_value(T val) {
    Value v;
    v.set<T>(val);
    return v;
}

int main() {
    std::cout << "=== IPB Common Library - DataPoint Example ===" << std::endl;

    // Create a temperature sensor data point using set_value
    DataPoint temperature("plant_a/line_1/temperature_01");
    temperature.set_value(23.5);
    temperature.set_protocol_id(PROTOCOL_MODBUS);
    temperature.set_quality(Quality::GOOD);

    std::cout << "Temperature sensor:" << std::endl;
    std::cout << "  Address: " << temperature.get_address() << std::endl;
    std::cout << "  Protocol: " << temperature.get_protocol_id() << std::endl;
    std::cout << "  Value: " << temperature.value().get<double>() << " C" << std::endl;
    std::cout << "  Quality: " << static_cast<int>(temperature.get_quality()) << std::endl;
    std::cout << "  Timestamp: " << temperature.get_timestamp() << std::endl;

    // Create a pressure sensor data point
    DataPoint pressure("plant_a/line_1/pressure_01");
    pressure.set_value(1.25);
    pressure.set_protocol_id(PROTOCOL_OPCUA);
    pressure.set_quality(Quality::GOOD);

    std::cout << "\nPressure sensor:" << std::endl;
    std::cout << "  Address: " << pressure.get_address() << std::endl;
    std::cout << "  Protocol: " << pressure.get_protocol_id() << std::endl;
    std::cout << "  Value: " << pressure.value().get<double>() << " bar" << std::endl;
    std::cout << "  Quality: " << static_cast<int>(pressure.get_quality()) << std::endl;

    // Create a boolean alarm data point
    DataPoint alarm("plant_a/line_1/alarm_high_temp");
    alarm.set_value(true);
    alarm.set_protocol_id(PROTOCOL_MODBUS);
    alarm.set_quality(Quality::BAD);

    std::cout << "\nAlarm status:" << std::endl;
    std::cout << "  Address: " << alarm.get_address() << std::endl;
    std::cout << "  Protocol: " << alarm.get_protocol_id() << std::endl;
    std::cout << "  Value: " << (alarm.value().get<bool>() ? "ACTIVE" : "INACTIVE") << std::endl;
    std::cout << "  Quality: " << static_cast<int>(alarm.get_quality()) << std::endl;

    std::cout << "\n=== Example completed successfully ===" << std::endl;
    return 0;
}
