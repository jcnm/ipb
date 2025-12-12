# Getting Started with IPB

This guide will help you get IPB (Industrial Protocol Bridge) up and running quickly.

## Prerequisites

### System Requirements

- **OS**: Linux (Ubuntu 20.04+), macOS (11+), or Windows 10+
- **Compiler**: GCC 11+, Clang 14+, or MSVC 2022+
- **CMake**: 3.16 or higher
- **C++ Standard**: C++20

### Dependencies

#### Linux (Ubuntu/Debian)

```bash
# Install using the provided script
./scripts/install-deps-linux.sh

# Or manually:
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    libjsoncpp-dev \
    libyaml-cpp-dev \
    libpaho-mqtt-dev \
    libpaho-mqttpp-dev \
    libcurl4-openssl-dev \
    libssl-dev
```

#### macOS

```bash
# Install using the provided script
./scripts/install-deps-macos.sh

# Or manually using Homebrew:
brew install cmake jsoncpp yaml-cpp paho-mqtt-c paho-mqtt-cpp curl openssl
```

## Building IPB

### Quick Build

```bash
# Clone the repository
git clone https://github.com/jcnm/ipb.git
cd ipb

# Build everything with default options
./scripts/build.sh
```

### Build Profiles

IPB supports different build profiles for various deployment scenarios:

```bash
# Full build - all components
./scripts/build.sh -t Release

# Debug build with verbose output
./scripts/build.sh -t Debug -v

# Embedded profile - minimal footprint
cmake -B build -DIPB_EMBEDDED=ON
cmake --build build

# Full profile - all features
cmake -B build -DIPB_FULL=ON
cmake --build build
```

### Custom Build

```bash
mkdir build && cd build

# Configure with specific options
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DIPB_SINK_MQTT=ON \
    -DIPB_SINK_KAFKA=ON \
    -DIPB_SCOOP_MODBUS=ON \
    -DIPB_BUILD_GATE=ON \
    -DBUILD_TESTING=ON

# Build
cmake --build . --parallel $(nproc)

# Run tests
ctest --output-on-failure
```

## Running IPB

### IPB Gate (Main Orchestrator)

IPB Gate is the main application for orchestrating data flow between scoops and sinks.

```bash
# Start with configuration file
./build/apps/ipb-gate/ipb-gate --config examples/gateway-config.yaml

# Start as daemon
./build/apps/ipb-gate/ipb-gate --config examples/gateway-config.yaml --daemon

# Start with verbose logging
./build/apps/ipb-gate/ipb-gate --config examples/gateway-config.yaml --log-level debug
```

### IPB Bridge (Lightweight)

For simpler deployments, use the lightweight bridge application:

```bash
./build/apps/ipb-bridge/ipb-bridge --config examples/bridge-config.yaml
```

## Configuration

### Basic Configuration

Create a YAML configuration file:

```yaml
# gateway-config.yaml
gateway:
  name: "my-gateway"
  log_level: "info"
  worker_threads: 4

sinks:
  - id: "console_output"
    type: "console"
    enabled: true
    format: "json"
    colored: true

  - id: "mqtt_telemetry"
    type: "mqtt"
    enabled: true
    broker_url: "tcp://localhost:1883"
    client_id: "ipb-gateway-001"
    base_topic: "factory/telemetry"
    qos: 1

routing:
  rules:
    - name: "all_data"
      enabled: true
      source_filter:
        address_pattern: ".*"
      destinations:
        - sink_id: "console_output"
        - sink_id: "mqtt_telemetry"
```

### MQTT Control Interface

Control IPB remotely via MQTT commands:

```bash
# Get status
mosquitto_pub -t "ipb/gateway/commands" \
    -m '{"type":"GET_STATUS","request_id":"status_001"}'

# Reload configuration
mosquitto_pub -t "ipb/gateway/commands" \
    -m '{"type":"RELOAD_CONFIG","request_id":"reload_001"}'

# Subscribe to responses
mosquitto_sub -t "ipb/gateway/responses/#"
```

## First Example

Here's a simple example using the IPB API:

```cpp
#include <ipb/common/data_point.hpp>
#include <ipb/core/message_bus/message_bus.hpp>
#include <iostream>

int main() {
    using namespace ipb::common;
    using namespace ipb::core;

    // Create a data point
    DataPoint sensor_reading("sensors/temperature/zone1");
    sensor_reading.set_value(25.5);
    sensor_reading.set_quality(Quality::GOOD);

    std::cout << "Address: " << sensor_reading.address() << std::endl;
    std::cout << "Value: " << sensor_reading.value().get<double>() << std::endl;
    std::cout << "Timestamp: " << sensor_reading.timestamp().nanoseconds() << " ns" << std::endl;

    // Create a message bus
    MessageBus bus;
    bus.start();

    // Subscribe to sensor data
    auto subscription = bus.subscribe("sensors/*", [](const Message& msg) {
        std::cout << "Received: " << msg.payload.address() << std::endl;
    });

    // Publish the data point
    bus.publish("sensors/temperature", sensor_reading);

    bus.stop();
    return 0;
}
```

## Project Structure

```
ipb/
├── apps/                    # Applications
│   ├── ipb-gate/            # Main orchestrator
│   └── ipb-bridge/          # Lightweight bridge
├── core/                    # Core libraries
│   ├── common/              # Common types (DataPoint, Value, etc.)
│   ├── components/          # Modular components
│   ├── router/              # Message routing
│   └── security/            # Security components
├── sinks/                   # Output adapters
│   ├── console/             # Console output
│   ├── syslog/              # Syslog output
│   ├── mqtt/                # MQTT output
│   └── ...
├── scoops/                  # Data collectors
│   ├── mqtt/                # MQTT input
│   ├── modbus/              # Modbus input
│   └── ...
├── transport/               # Transport layers
│   ├── mqtt/                # MQTT transport
│   └── http/                # HTTP transport
└── examples/                # Example applications
```

## Next Steps

- Read the [API Reference](API_REFERENCE.md) for detailed API documentation
- See [Configuration Guide](CONFIGURATION.md) for all configuration options
- Check the [Architecture Document](../ARCHITECTURE.md) for design details
- Explore the [examples/](../examples/) directory for more examples

## Troubleshooting

### Common Issues

**Build fails with missing dependencies:**
```bash
# Make sure all dependencies are installed
./scripts/install-deps-linux.sh  # or install-deps-macos.sh
```

**MQTT connection refused:**
```bash
# Make sure MQTT broker is running
sudo systemctl start mosquitto
# or
mosquitto -v
```

**Permission denied errors:**
```bash
# Check file permissions and run with appropriate privileges
sudo ./build/apps/ipb-gate/ipb-gate --config examples/gateway-config.yaml
```

## Getting Help

- [GitHub Issues](https://github.com/jcnm/ipb/issues) - Report bugs and request features
- [Architecture Guide](../ARCHITECTURE.md) - Understand the system design
- [API Reference](API_REFERENCE.md) - Detailed API documentation
