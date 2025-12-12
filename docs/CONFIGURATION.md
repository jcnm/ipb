# IPB Configuration Reference

This document provides a comprehensive reference for all IPB configuration options.

## Table of Contents

- [Gateway Configuration](#gateway-configuration)
- [Sinks Configuration](#sinks-configuration)
- [Scoops Configuration](#scoops-configuration)
- [Routing Configuration](#routing-configuration)
- [Security Configuration](#security-configuration)
- [Performance Tuning](#performance-tuning)
- [CMake Build Options](#cmake-build-options)

---

## Gateway Configuration

The top-level gateway configuration controls the IPB instance.

```yaml
gateway:
  # Unique identifier for this gateway instance
  name: "industrial-gateway-01"

  # Logging level: trace, debug, info, warn, error, critical
  log_level: "info"

  # Log output: console, syslog, file, or path to log file
  log_output: "console"

  # Number of worker threads (0 = auto-detect based on CPU cores)
  worker_threads: 4

  # Enable real-time thread priorities
  realtime_enabled: false

  # Real-time priority level (1-99, requires privileges)
  realtime_priority: 50

  # CPU affinity for worker threads (comma-separated CPU IDs, -1 = no affinity)
  cpu_affinity: "-1"

  # Graceful shutdown timeout in seconds
  shutdown_timeout: 30

  # Enable statistics collection
  stats_enabled: true

  # Statistics reporting interval in seconds
  stats_interval: 60

  # Health check configuration
  health_check:
    enabled: true
    port: 8080
    path: "/health"
```

---

## Sinks Configuration

Sinks are output destinations for processed data.

### Console Sink

```yaml
sinks:
  - id: "console_debug"
    type: "console"
    enabled: true

    # Output format: text, json, json_compact, csv, table, custom
    format: "json"

    # Enable colored output (ANSI colors)
    colored: true

    # Include timestamp in output
    include_timestamp: true

    # Include quality in output
    include_quality: true

    # Filter by minimum quality (0=GOOD, 1=UNCERTAIN, 2=BAD, etc.)
    min_quality: 0

    # Output to stderr instead of stdout
    stderr: false

    # Buffer size for batching
    buffer_size: 100

    # Flush interval in milliseconds
    flush_interval: 1000
```

### Syslog Sink

```yaml
sinks:
  - id: "syslog_audit"
    type: "syslog"
    enabled: true

    # Syslog facility: kern, user, mail, daemon, auth, syslog, lpr, news,
    #                  uucp, cron, authpriv, ftp, local0-local7
    facility: "local0"

    # Application identifier
    identity: "ipb-gateway"

    # Include PID in messages
    include_pid: true

    # Log to console as well
    log_cons: false

    # Default syslog priority: emerg, alert, crit, err, warning, notice, info, debug
    default_priority: "info"

    # Remote syslog configuration (optional)
    remote:
      enabled: false
      host: "syslog.example.com"
      port: 514
      protocol: "udp"  # udp or tcp
      tls_enabled: false
```

### MQTT Sink

```yaml
sinks:
  - id: "mqtt_telemetry"
    type: "mqtt"
    enabled: true

    # Broker connection
    broker_url: "tcp://localhost:1883"
    client_id: "ipb-gateway-001"

    # Authentication (optional)
    username: ""
    password: ""

    # TLS configuration
    tls:
      enabled: false
      ca_cert: "/etc/certs/ca.crt"
      client_cert: "/etc/certs/client.crt"
      client_key: "/etc/certs/client.key"
      verify_server: true

    # Topic configuration
    base_topic: "factory/telemetry"

    # Topic strategy: single_topic, protocol_based, address_based, hierarchical, custom
    topic_strategy: "address_based"

    # Custom topic template (when strategy is "custom")
    # Variables: {address}, {protocol}, {quality}, {timestamp}
    topic_template: "{base_topic}/{protocol}/{address}"

    # Message format: json, json_compact, csv, influx_line, binary, custom
    message_format: "json"

    # QoS level (0, 1, or 2)
    qos: 1

    # Retain messages
    retain: false

    # Connection settings
    connection:
      keep_alive: 60
      connect_timeout: 30
      disconnect_timeout: 10
      auto_reconnect: true
      reconnect_delay_min: 1
      reconnect_delay_max: 60

    # Batching configuration
    batching:
      enabled: true
      max_messages: 100
      max_delay_ms: 100

    # Performance tuning
    performance:
      # Message queue size
      queue_size: 10000
      # Number of publisher threads
      publisher_threads: 2
      # Enable message persistence
      persistence: false
      persistence_path: "/var/lib/ipb/mqtt"
```

### Kafka Sink

```yaml
sinks:
  - id: "kafka_analytics"
    type: "kafka"
    enabled: true

    # Broker list
    brokers: "kafka1:9092,kafka2:9092,kafka3:9092"

    # Topic name
    topic: "ipb-telemetry"

    # Client ID
    client_id: "ipb-gateway-001"

    # Partitioning strategy: round_robin, hash, key_based
    partition_strategy: "hash"

    # Key template for partitioning
    key_template: "{protocol}:{address}"

    # Message format: json, avro, protobuf
    message_format: "json"

    # Compression: none, gzip, snappy, lz4, zstd
    compression: "lz4"

    # Producer configuration
    producer:
      acks: "all"  # 0, 1, or all
      retries: 3
      batch_size: 16384
      linger_ms: 5
      buffer_memory: 33554432

    # Security
    security:
      protocol: "PLAINTEXT"  # PLAINTEXT, SSL, SASL_PLAINTEXT, SASL_SSL
      ssl:
        ca_location: ""
        certificate_location: ""
        key_location: ""
      sasl:
        mechanism: "PLAIN"  # PLAIN, SCRAM-SHA-256, SCRAM-SHA-512
        username: ""
        password: ""
```

### ZeroMQ Sink

```yaml
sinks:
  - id: "zmq_internal"
    type: "zmq"
    enabled: true

    # Socket type: pub, push, dealer
    socket_type: "pub"

    # Bind or connect
    mode: "bind"  # bind or connect

    # Endpoint URL
    endpoint: "tcp://*:5555"

    # High water mark (queue size)
    hwm: 10000

    # Message format: json, msgpack, protobuf
    message_format: "json"

    # Enable multipart messages
    multipart: false

    # Topic prefix (for PUB sockets)
    topic_prefix: "ipb"
```

---

## Scoops Configuration

Scoops are data collectors/input sources.

### MQTT Scoop

```yaml
scoops:
  - id: "mqtt_sensors"
    type: "mqtt"
    enabled: true

    # Broker connection
    broker_url: "tcp://localhost:1883"
    client_id: "ipb-scoop-001"

    # Subscriptions
    subscriptions:
      - topic: "sensors/+/temperature"
        qos: 1
      - topic: "sensors/+/pressure"
        qos: 1
      - topic: "alarms/#"
        qos: 2

    # Message parsing
    parsing:
      # Input format: json, csv, raw, custom
      format: "json"
      # Address extraction path (for JSON)
      address_path: "$.sensor_id"
      # Value extraction path (for JSON)
      value_path: "$.value"
      # Timestamp extraction path (for JSON, optional)
      timestamp_path: "$.timestamp"

    # Protocol ID for routing
    protocol_id: 10
```

### Modbus Scoop

```yaml
scoops:
  - id: "modbus_plc"
    type: "modbus"
    enabled: true

    # Connection mode: tcp, rtu, rtu_over_tcp
    mode: "tcp"

    # TCP settings
    tcp:
      host: "192.168.1.100"
      port: 502
      timeout_ms: 1000

    # RTU settings (for serial)
    rtu:
      device: "/dev/ttyUSB0"
      baud_rate: 9600
      parity: "none"  # none, even, odd
      data_bits: 8
      stop_bits: 1

    # Slave/Unit ID
    unit_id: 1

    # Polling configuration
    polling:
      interval_ms: 1000
      timeout_ms: 500
      retries: 3

    # Register definitions
    registers:
      - name: "temperature_zone1"
        address: 0
        type: "holding"  # holding, input, coil, discrete
        data_type: "float32"  # int16, uint16, int32, uint32, float32, float64
        byte_order: "big_endian"
        scale: 0.1
        offset: 0

      - name: "pressure_tank1"
        address: 2
        type: "holding"
        data_type: "uint16"
        scale: 0.01

    # Protocol ID for routing
    protocol_id: 1
```

### OPC UA Scoop

```yaml
scoops:
  - id: "opcua_server"
    type: "opcua"
    enabled: true

    # Server endpoint
    endpoint: "opc.tcp://192.168.1.50:4840"

    # Security mode: none, sign, sign_and_encrypt
    security_mode: "sign_and_encrypt"

    # Security policy: none, basic128rsa15, basic256, basic256sha256
    security_policy: "basic256sha256"

    # Authentication
    authentication:
      type: "username"  # anonymous, username, certificate
      username: "operator"
      password: "secret"
      certificate: ""
      private_key: ""

    # Subscription settings
    subscription:
      publishing_interval_ms: 1000
      lifetime_count: 100
      max_keep_alive_count: 10
      max_notifications_per_publish: 1000

    # Node subscriptions
    nodes:
      - node_id: "ns=2;s=Temperature.Zone1"
        name: "temperature_zone1"
        sampling_interval_ms: 500

      - node_id: "ns=2;s=Pressure.Tank1"
        name: "pressure_tank1"
        sampling_interval_ms: 1000

    # Protocol ID for routing
    protocol_id: 2
```

---

## Routing Configuration

Routing rules determine how data flows from scoops to sinks.

```yaml
routing:
  # Default behavior when no rules match
  default_action: "drop"  # drop, forward_all

  # Enable rule caching for performance
  rule_caching: true

  # Dead letter queue for undeliverable messages
  dead_letter:
    enabled: true
    max_size: 10000
    retention_seconds: 3600

  rules:
    - name: "critical_alarms"
      enabled: true
      priority: 100  # Higher priority = evaluated first

      # Source filtering
      source_filter:
        # Address pattern (regex)
        address_pattern: ".*alarm.*"
        # Protocol IDs to match
        protocol_ids: [1, 2]  # Modbus and OPC UA
        # Quality filter
        min_quality: 0  # GOOD only

      # Value conditions
      value_conditions:
        - type: "threshold"
          operator: "gt"  # gt, lt, eq, ne, ge, le
          value: 80.0
        # OR condition
        - type: "change"
          min_change: 5.0

      # Destinations
      destinations:
        - sink_id: "mqtt_telemetry"
          priority: "high"
        - sink_id: "syslog_audit"
          priority: "high"

      # Rate limiting
      rate_limit:
        enabled: true
        max_messages_per_second: 100
        burst_size: 50

    - name: "regular_telemetry"
      enabled: true
      priority: 50

      source_filter:
        address_pattern: "sensors/.*"

      destinations:
        - sink_id: "mqtt_telemetry"
          priority: "normal"
        - sink_id: "console_debug"
          priority: "low"

      # Sampling (reduce data rate)
      sampling:
        enabled: true
        rate: 0.1  # Keep 10% of messages
        method: "random"  # random, nth, time_based
```

---

## Security Configuration

```yaml
security:
  # TLS configuration
  tls:
    enabled: true
    min_version: "1.2"  # 1.0, 1.1, 1.2, 1.3
    cipher_suites:
      - "TLS_AES_256_GCM_SHA384"
      - "TLS_CHACHA20_POLY1305_SHA256"
    verify_peer: true
    verify_hostname: true

  # Certificate paths
  certificates:
    ca_cert: "/etc/ipb/certs/ca.crt"
    server_cert: "/etc/ipb/certs/server.crt"
    server_key: "/etc/ipb/certs/server.key"

  # Access control
  access_control:
    enabled: true
    default_policy: "deny"

    users:
      - username: "admin"
        password_hash: "$2b$12$..."
        roles: ["admin"]

      - username: "operator"
        password_hash: "$2b$12$..."
        roles: ["read", "write"]

    roles:
      admin:
        - "*"
      read:
        - "GET:*"
      write:
        - "GET:*"
        - "POST:sinks/*"
        - "POST:scoops/*"
```

---

## Performance Tuning

```yaml
performance:
  # Memory management
  memory:
    # Pre-allocate memory pools
    pool_size: 10000
    # Maximum memory usage (MB, 0 = unlimited)
    max_memory_mb: 1024
    # Enable huge pages (requires system configuration)
    huge_pages: false

  # Threading
  threading:
    # Use real-time scheduler (requires privileges)
    realtime: false
    # Thread pool size (0 = auto)
    pool_size: 0
    # Stack size per thread (KB)
    stack_size: 256

  # Buffering
  buffering:
    # Input buffer size per scoop
    input_buffer_size: 10000
    # Output buffer size per sink
    output_buffer_size: 10000
    # Batch size for processing
    batch_size: 100

  # Network
  network:
    # TCP keep-alive
    tcp_keepalive: true
    tcp_keepalive_idle: 60
    tcp_keepalive_interval: 10
    tcp_keepalive_count: 3
    # Socket buffer sizes
    recv_buffer_size: 65536
    send_buffer_size: 65536

  # Profiling
  profiling:
    enabled: false
    output: "/var/log/ipb/profile.log"
    sample_rate: 1000
```

---

## CMake Build Options

### Build Profiles

| Option | Default | Description |
|--------|---------|-------------|
| `IPB_EMBEDDED` | OFF | Minimal footprint build |
| `IPB_FULL` | OFF | All components enabled |

### Sink Options

| Option | Default | Description |
|--------|---------|-------------|
| `IPB_SINK_CONSOLE` | ON | Console output sink |
| `IPB_SINK_SYSLOG` | ON | Syslog output sink |
| `IPB_SINK_MQTT` | ON | MQTT output sink |
| `IPB_SINK_KAFKA` | OFF | Kafka output sink |
| `IPB_SINK_SPARKPLUG` | OFF | Sparkplug B sink |
| `IPB_SINK_ZMQ` | OFF | ZeroMQ sink |

### Scoop Options

| Option | Default | Description |
|--------|---------|-------------|
| `IPB_SCOOP_CONSOLE` | OFF | Console input scoop |
| `IPB_SCOOP_MODBUS` | OFF | Modbus protocol scoop |
| `IPB_SCOOP_OPCUA` | OFF | OPC UA scoop |
| `IPB_SCOOP_MQTT` | ON | MQTT subscriber scoop |
| `IPB_SCOOP_SPARKPLUG` | OFF | Sparkplug B scoop |

### Transport Options

| Option | Default | Description |
|--------|---------|-------------|
| `IPB_TRANSPORT_MQTT` | ON | MQTT transport layer |
| `IPB_TRANSPORT_HTTP` | ON | HTTP transport layer |

### Application Options

| Option | Default | Description |
|--------|---------|-------------|
| `IPB_BUILD_GATE` | ON | Build ipb-gate application |
| `IPB_BUILD_BRIDGE` | ON | Build ipb-bridge application |

### Development Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTING` | OFF | Build unit tests |
| `BUILD_EXAMPLES` | ON | Build example applications |
| `ENABLE_OPTIMIZATIONS` | ON | Enable compiler optimizations |
| `ENABLE_LTO` | OFF | Enable Link Time Optimization |
| `ENABLE_SANITIZERS` | OFF | Enable address/UB sanitizers |
| `ENABLE_COVERAGE` | OFF | Enable code coverage |

### Example Build Commands

```bash
# Minimal embedded build
cmake -B build -DIPB_EMBEDDED=ON

# Full build with all features
cmake -B build -DIPB_FULL=ON -DBUILD_TESTING=ON

# Custom build for MQTT + Modbus
cmake -B build \
    -DIPB_SINK_MQTT=ON \
    -DIPB_SCOOP_MODBUS=ON \
    -DIPB_SCOOP_MQTT=ON \
    -DBUILD_TESTING=ON

# Release build with LTO
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_LTO=ON \
    -DENABLE_OPTIMIZATIONS=ON

# Debug build with sanitizers
cmake -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_SANITIZERS=ON \
    -DBUILD_TESTING=ON
```

---

## Environment Variables

IPB respects the following environment variables:

| Variable | Description |
|----------|-------------|
| `IPB_CONFIG_PATH` | Path to configuration file |
| `IPB_LOG_LEVEL` | Override log level |
| `IPB_LOG_OUTPUT` | Override log output destination |
| `IPB_STATS_ENABLED` | Enable/disable statistics (true/false) |
| `IPB_WORKER_THREADS` | Override worker thread count |

---

## See Also

- [Getting Started Guide](GETTING_STARTED.md)
- [API Reference](API_REFERENCE.md)
- [Architecture Document](../ARCHITECTURE.md)
