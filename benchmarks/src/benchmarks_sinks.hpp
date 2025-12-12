#pragma once

/**
 * @file benchmarks_sinks.hpp
 * @brief Sink component benchmarks
 *
 * Benchmarks for output sinks:
 * - MQTT Sink (publish, batch)
 * - HTTP Sink (POST, batch)
 * - Console Sink (format, write)
 * - Syslog Sink (format, send)
 * - File Sink (write, rotate)
 * - WebSocket Sink (send, batch)
 *
 * Each sink is benchmarked for:
 * - Single message throughput
 * - Batch processing performance
 * - Serialization overhead
 * - Connection handling
 */

#include <ipb/benchmarks/benchmark_framework.hpp>

namespace ipb::benchmark {

//=============================================================================
// MQTT Sink Benchmarks (Placeholder - requires MQTT client)
//=============================================================================

namespace mqtt_sink_benchmarks {

void bench_format_message() {
    // Benchmark message formatting for MQTT
    // TODO: Implement when MQTT sink is integrated
}

void bench_serialize_payload() {
    // Benchmark payload serialization
}

} // namespace mqtt_sink_benchmarks

//=============================================================================
// HTTP Sink Benchmarks (Placeholder)
//=============================================================================

namespace http_sink_benchmarks {

void bench_format_json() {
    // Benchmark JSON formatting for HTTP
}

void bench_serialize_batch() {
    // Benchmark batch serialization
}

} // namespace http_sink_benchmarks

//=============================================================================
// Console Sink Benchmarks
//=============================================================================

namespace console_sink_benchmarks {

void bench_format_output() {
    // Format string generation
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "[%s] %s: %.6f",
             "2024-01-15T10:30:00Z",
             "sensor.temperature",
             42.5);
}

} // namespace console_sink_benchmarks

//=============================================================================
// Syslog Sink Benchmarks
//=============================================================================

namespace syslog_sink_benchmarks {

void bench_format_syslog() {
    // RFC 5424 format generation
    char buffer[512];
    snprintf(buffer, sizeof(buffer),
             "<%d>1 %s %s %s - - %s",
             14,  // facility * 8 + severity
             "2024-01-15T10:30:00Z",
             "hostname",
             "ipb",
             "sensor.temperature=42.5");
}

} // namespace syslog_sink_benchmarks

//=============================================================================
// Registration Function
//=============================================================================

inline void register_sink_benchmarks() {
    auto& registry = BenchmarkRegistry::instance();

    // Console Sink
    {
        BenchmarkDef def;
        def.category = BenchmarkCategory::SINKS;
        def.component = "console";
        def.iterations = 100000;
        def.warmup = 1000;

        def.name = "format_output";
        def.benchmark = console_sink_benchmarks::bench_format_output;
        def.target_p50_ns = 500;
        def.target_p99_ns = 5000;
        registry.register_benchmark(def);
    }

    // Syslog Sink
    {
        BenchmarkDef def;
        def.category = BenchmarkCategory::SINKS;
        def.component = "syslog";
        def.iterations = 100000;
        def.warmup = 1000;

        def.name = "format_message";
        def.benchmark = syslog_sink_benchmarks::bench_format_syslog;
        def.target_p50_ns = 500;
        def.target_p99_ns = 5000;
        registry.register_benchmark(def);
    }

    // Note: MQTT, HTTP, WebSocket benchmarks require actual sink implementations
    // and will be added when those components are available for benchmarking
}

} // namespace ipb::benchmark
