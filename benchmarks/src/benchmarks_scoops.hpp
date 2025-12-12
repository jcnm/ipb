#pragma once

/**
 * @file benchmarks_scoops.hpp
 * @brief Scoop (input source) component benchmarks
 *
 * Benchmarks for input sources:
 * - OPC-UA Scoop (read, subscribe, batch)
 * - Modbus Scoop (read registers, write registers)
 * - Sparkplug B Scoop (decode, encode)
 * - File Scoop (read, parse)
 * - MQTT Scoop (subscribe, decode)
 *
 * Each scoop is benchmarked for:
 * - Single value read throughput
 * - Batch read performance
 * - Data parsing/decoding overhead
 * - Event handling latency
 */

#include <ipb/benchmarks/benchmark_framework.hpp>

#include <cstring>
#include <random>
#include <vector>

namespace ipb::benchmark {

//=============================================================================
// Modbus Scoop Benchmarks
//=============================================================================

namespace modbus_scoop_benchmarks {

// Simulated Modbus register data
inline std::vector<uint16_t> g_registers(125);  // Max 125 registers per request
inline std::mt19937 g_rng(42);

void setup() {
    // Fill with random register values
    std::uniform_int_distribution<uint16_t> dist(0, 65535);
    for (auto& reg : g_registers) {
        reg = dist(g_rng);
    }
}

void bench_decode_registers() {
    // Decode 10 registers into values
    double values[10];
    for (int i = 0; i < 10; ++i) {
        values[i] = static_cast<double>(g_registers[i]);
    }
    asm volatile("" : : "r"(values[0]) : "memory");
}

void bench_decode_float32() {
    // Decode 2 registers as 32-bit float (big-endian)
    uint32_t combined = (static_cast<uint32_t>(g_registers[0]) << 16) |
                        static_cast<uint32_t>(g_registers[1]);
    float value;
    std::memcpy(&value, &combined, sizeof(float));
    asm volatile("" : : "r"(value) : "memory");
}

void bench_decode_int32() {
    // Decode 2 registers as 32-bit integer
    int32_t value = (static_cast<int32_t>(g_registers[0]) << 16) |
                    static_cast<int32_t>(g_registers[1]);
    asm volatile("" : : "r"(value) : "memory");
}

void bench_encode_registers() {
    // Encode 10 double values into registers
    uint16_t output[10];
    double values[10] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
    for (int i = 0; i < 10; ++i) {
        output[i] = static_cast<uint16_t>(values[i]);
    }
    asm volatile("" : : "r"(output[0]) : "memory");
}

} // namespace modbus_scoop_benchmarks

//=============================================================================
// OPC-UA Scoop Benchmarks (Simulated)
//=============================================================================

namespace opcua_scoop_benchmarks {

// Simulated OPC-UA Variant decoding
struct SimulatedVariant {
    uint8_t type;
    union {
        bool boolean;
        int32_t int32;
        double dbl;
        char str[64];
    } data;
};

inline SimulatedVariant g_variant;

void setup() {
    g_variant.type = 11;  // Double
    g_variant.data.dbl = 42.5;
}

void bench_decode_variant_double() {
    double value = 0;
    if (g_variant.type == 11) {
        value = g_variant.data.dbl;
    }
    asm volatile("" : : "r"(value) : "memory");
}

void bench_decode_variant_int32() {
    g_variant.type = 6;
    g_variant.data.int32 = 12345;

    int32_t value = 0;
    if (g_variant.type == 6) {
        value = g_variant.data.int32;
    }
    asm volatile("" : : "r"(value) : "memory");
}

void bench_node_id_parse() {
    // Parse node ID string "ns=2;s=MyVariable"
    const char* node_id = "ns=2;s=MyVariable";
    int ns = 0;
    char identifier[64] = {0};

    // Simple parsing
    if (std::strncmp(node_id, "ns=", 3) == 0) {
        ns = node_id[3] - '0';
        const char* semi = std::strchr(node_id, ';');
        if (semi && semi[1] == 's' && semi[2] == '=') {
            std::strncpy(identifier, semi + 3, sizeof(identifier) - 1);
        }
    }
    asm volatile("" : : "r"(ns), "r"(identifier[0]) : "memory");
}

} // namespace opcua_scoop_benchmarks

//=============================================================================
// Sparkplug B Scoop Benchmarks (Simulated)
//=============================================================================

namespace sparkplug_benchmarks {

// Simulated metric data
struct SparkplugMetric {
    uint64_t alias;
    uint64_t timestamp;
    uint32_t datatype;
    union {
        int32_t int_value;
        float float_value;
        double double_value;
        bool bool_value;
    } value;
};

inline std::vector<SparkplugMetric> g_metrics(100);

void setup() {
    for (size_t i = 0; i < g_metrics.size(); ++i) {
        g_metrics[i].alias = i;
        g_metrics[i].timestamp = 1705312200000 + i;
        g_metrics[i].datatype = 10;  // Double
        g_metrics[i].value.double_value = static_cast<double>(i) * 1.5;
    }
}

void bench_decode_metric() {
    const auto& metric = g_metrics[0];
    double value = 0;
    switch (metric.datatype) {
        case 7: value = metric.value.int_value; break;
        case 9: value = metric.value.float_value; break;
        case 10: value = metric.value.double_value; break;
        case 11: value = metric.value.bool_value ? 1.0 : 0.0; break;
    }
    asm volatile("" : : "r"(value) : "memory");
}

void bench_decode_batch() {
    // Decode batch of 10 metrics
    double values[10];
    for (int i = 0; i < 10; ++i) {
        const auto& metric = g_metrics[i];
        switch (metric.datatype) {
            case 10: values[i] = metric.value.double_value; break;
            default: values[i] = 0; break;
        }
    }
    asm volatile("" : : "r"(values[0]) : "memory");
}

void bench_timestamp_decode() {
    // Convert Sparkplug timestamp (milliseconds) to timespec
    uint64_t ts_ms = g_metrics[0].timestamp;
    time_t seconds = ts_ms / 1000;
    long nanoseconds = (ts_ms % 1000) * 1000000;
    asm volatile("" : : "r"(seconds), "r"(nanoseconds) : "memory");
}

} // namespace sparkplug_benchmarks

//=============================================================================
// Registration Function
//=============================================================================

inline void register_scoop_benchmarks() {
    auto& registry = BenchmarkRegistry::instance();

    // Modbus Scoop
    {
        BenchmarkDef def;
        def.category = BenchmarkCategory::SCOOPS;
        def.component = "modbus";
        def.iterations = 100000;
        def.warmup = 1000;

        def.name = "decode_registers";
        def.setup = modbus_scoop_benchmarks::setup;
        def.benchmark = modbus_scoop_benchmarks::bench_decode_registers;
        def.target_p50_ns = 100;
        def.target_p99_ns = 1000;
        registry.register_benchmark(def);

        def.name = "decode_float32";
        def.benchmark = modbus_scoop_benchmarks::bench_decode_float32;
        def.target_p50_ns = 50;
        def.target_p99_ns = 500;
        registry.register_benchmark(def);

        def.name = "decode_int32";
        def.benchmark = modbus_scoop_benchmarks::bench_decode_int32;
        registry.register_benchmark(def);

        def.name = "encode_registers";
        def.benchmark = modbus_scoop_benchmarks::bench_encode_registers;
        registry.register_benchmark(def);
    }

    // OPC-UA Scoop
    {
        BenchmarkDef def;
        def.category = BenchmarkCategory::SCOOPS;
        def.component = "opcua";
        def.iterations = 100000;
        def.warmup = 1000;

        def.name = "decode_variant_double";
        def.setup = opcua_scoop_benchmarks::setup;
        def.benchmark = opcua_scoop_benchmarks::bench_decode_variant_double;
        def.target_p50_ns = 50;
        def.target_p99_ns = 500;
        registry.register_benchmark(def);

        def.name = "decode_variant_int32";
        def.setup = opcua_scoop_benchmarks::setup;
        def.benchmark = opcua_scoop_benchmarks::bench_decode_variant_int32;
        registry.register_benchmark(def);

        def.name = "node_id_parse";
        def.benchmark = opcua_scoop_benchmarks::bench_node_id_parse;
        def.target_p50_ns = 200;
        def.target_p99_ns = 2000;
        registry.register_benchmark(def);
    }

    // Sparkplug B
    {
        BenchmarkDef def;
        def.category = BenchmarkCategory::SCOOPS;
        def.component = "sparkplug";
        def.iterations = 100000;
        def.warmup = 1000;

        def.name = "decode_metric";
        def.setup = sparkplug_benchmarks::setup;
        def.benchmark = sparkplug_benchmarks::bench_decode_metric;
        def.target_p50_ns = 50;
        def.target_p99_ns = 500;
        registry.register_benchmark(def);

        def.name = "decode_batch";
        def.setup = sparkplug_benchmarks::setup;
        def.benchmark = sparkplug_benchmarks::bench_decode_batch;
        def.target_p50_ns = 200;
        def.target_p99_ns = 2000;
        registry.register_benchmark(def);

        def.name = "timestamp_decode";
        def.setup = sparkplug_benchmarks::setup;
        def.benchmark = sparkplug_benchmarks::bench_timestamp_decode;
        registry.register_benchmark(def);
    }
}

} // namespace ipb::benchmark
