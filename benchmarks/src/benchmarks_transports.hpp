#pragma once

/**
 * @file benchmarks_transports.hpp
 * @brief Transport layer benchmarks
 *
 * Benchmarks for transport protocols:
 * - TCP (connect, send, receive)
 * - UDP (send, receive)
 * - TLS/SSL (handshake, encrypt, decrypt)
 * - WebSocket (frame, defragment)
 * - Serial (encode, decode)
 *
 * Each transport is benchmarked for:
 * - Connection establishment latency
 * - Send/receive throughput
 * - Protocol overhead
 * - Encryption overhead (where applicable)
 */

#include <ipb/benchmarks/benchmark_framework.hpp>

#include <cstring>
#include <vector>

namespace ipb::benchmark {

//=============================================================================
// Buffer Operations (Common to all transports)
//=============================================================================

namespace buffer_benchmarks {

inline std::vector<char> g_small_buffer(64);
inline std::vector<char> g_medium_buffer(1024);
inline std::vector<char> g_large_buffer(65536);

void setup() {
    // Fill buffers with test data
    for (auto& c : g_small_buffer)
        c = 'A';
    for (auto& c : g_medium_buffer)
        c = 'B';
    for (auto& c : g_large_buffer)
        c = 'C';
}

void bench_memcpy_64() {
    char dest[64];
    std::memcpy(dest, g_small_buffer.data(), 64);
    asm volatile("" : : "r"(dest[0]) : "memory");
}

void bench_memcpy_1k() {
    char dest[1024];
    std::memcpy(dest, g_medium_buffer.data(), 1024);
    asm volatile("" : : "r"(dest[0]) : "memory");
}

void bench_memcpy_64k() {
    std::vector<char> dest(65536);
    std::memcpy(dest.data(), g_large_buffer.data(), 65536);
    asm volatile("" : : "r"(dest[0]) : "memory");
}

void bench_buffer_alloc_small() {
    auto* buf = new char[64];
    asm volatile("" : : "r"(buf) : "memory");
    delete[] buf;
}

void bench_buffer_alloc_medium() {
    auto* buf = new char[1024];
    asm volatile("" : : "r"(buf) : "memory");
    delete[] buf;
}

void bench_buffer_alloc_large() {
    auto* buf = new char[65536];
    asm volatile("" : : "r"(buf) : "memory");
    delete[] buf;
}

}  // namespace buffer_benchmarks

//=============================================================================
// TCP Frame Benchmarks
//=============================================================================

namespace tcp_benchmarks {

// Simulated TCP header operations
struct TCPHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
};

inline TCPHeader g_header;

void setup() {
    g_header.src_port    = 12345;
    g_header.dst_port    = 1883;
    g_header.seq_num     = 1000000;
    g_header.ack_num     = 2000000;
    g_header.data_offset = 5;
    g_header.flags       = 0x18;  // PSH+ACK
    g_header.window      = 65535;
    g_header.checksum    = 0;
    g_header.urgent_ptr  = 0;
}

void bench_header_parse() {
    // Parse header from network byte order
    uint16_t src = __builtin_bswap16(g_header.src_port);
    uint16_t dst = __builtin_bswap16(g_header.dst_port);
    uint32_t seq = __builtin_bswap32(g_header.seq_num);
    asm volatile("" : : "r"(src), "r"(dst), "r"(seq) : "memory");
}

void bench_header_build() {
    // Build header in network byte order
    TCPHeader hdr;
    hdr.src_port = __builtin_bswap16(12345);
    hdr.dst_port = __builtin_bswap16(1883);
    hdr.seq_num  = __builtin_bswap32(1000000);
    hdr.ack_num  = __builtin_bswap32(2000000);
    asm volatile("" : : "r"(hdr.src_port) : "memory");
}

uint16_t compute_checksum(const void* data, size_t len) {
    const uint16_t* ptr = static_cast<const uint16_t*>(data);
    uint32_t sum        = 0;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1) {
        sum += *reinterpret_cast<const uint8_t*>(ptr);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

void bench_checksum_64() {
    uint16_t cs = compute_checksum(buffer_benchmarks::g_small_buffer.data(), 64);
    asm volatile("" : : "r"(cs) : "memory");
}

void bench_checksum_1k() {
    uint16_t cs = compute_checksum(buffer_benchmarks::g_medium_buffer.data(), 1024);
    asm volatile("" : : "r"(cs) : "memory");
}

}  // namespace tcp_benchmarks

//=============================================================================
// WebSocket Frame Benchmarks
//=============================================================================

namespace websocket_benchmarks {

// WebSocket frame header
struct WSFrameHeader {
    uint8_t fin_rsv_opcode;
    uint8_t mask_len;
    uint8_t mask_key[4];
};

inline std::vector<char> g_payload(256);
inline WSFrameHeader g_frame_header;

void setup() {
    g_frame_header.fin_rsv_opcode = 0x82;        // Binary frame, FIN set
    g_frame_header.mask_len       = 0x80 | 126;  // Masked, extended length
    g_frame_header.mask_key[0]    = 0x12;
    g_frame_header.mask_key[1]    = 0x34;
    g_frame_header.mask_key[2]    = 0x56;
    g_frame_header.mask_key[3]    = 0x78;

    for (size_t i = 0; i < g_payload.size(); ++i) {
        g_payload[i] = static_cast<char>(i);
    }
}

void bench_frame_parse() {
    bool fin       = (g_frame_header.fin_rsv_opcode & 0x80) != 0;
    uint8_t opcode = g_frame_header.fin_rsv_opcode & 0x0F;
    bool masked    = (g_frame_header.mask_len & 0x80) != 0;
    uint8_t len    = g_frame_header.mask_len & 0x7F;
    asm volatile("" : : "r"(fin), "r"(opcode), "r"(masked), "r"(len) : "memory");
}

void bench_mask_payload() {
    std::vector<char> output(g_payload.size());
    const uint8_t* mask = g_frame_header.mask_key;

    for (size_t i = 0; i < g_payload.size(); ++i) {
        output[i] = g_payload[i] ^ mask[i % 4];
    }
    asm volatile("" : : "r"(output[0]) : "memory");
}

void bench_unmask_payload() {
    // XOR with mask key (same operation as masking)
    std::vector<char> output(g_payload.size());
    const uint8_t* mask = g_frame_header.mask_key;

    for (size_t i = 0; i < g_payload.size(); ++i) {
        output[i] = g_payload[i] ^ mask[i % 4];
    }
    asm volatile("" : : "r"(output[0]) : "memory");
}

void bench_build_frame() {
    // Build a complete frame
    std::vector<char> frame(2 + 4 + g_payload.size());

    frame[0] = static_cast<char>(0x82);                     // Binary, FIN
    frame[1] = static_cast<char>(0x80 | g_payload.size());  // Masked

    // Copy mask key
    std::memcpy(&frame[2], g_frame_header.mask_key, 4);

    // Copy and mask payload
    for (size_t i = 0; i < g_payload.size(); ++i) {
        frame[6 + i] = g_payload[i] ^ g_frame_header.mask_key[i % 4];
    }

    asm volatile("" : : "r"(frame[0]) : "memory");
}

}  // namespace websocket_benchmarks

//=============================================================================
// Serial Protocol Benchmarks (Simulated)
//=============================================================================

namespace serial_benchmarks {

// Simulated Modbus RTU frame
struct ModbusRTUFrame {
    uint8_t slave_addr;
    uint8_t function_code;
    uint8_t data[252];
    uint16_t crc;
};

inline ModbusRTUFrame g_rtu_frame;

void setup() {
    g_rtu_frame.slave_addr    = 1;
    g_rtu_frame.function_code = 3;     // Read holding registers
    g_rtu_frame.data[0]       = 0x00;  // Start address high
    g_rtu_frame.data[1]       = 0x00;  // Start address low
    g_rtu_frame.data[2]       = 0x00;  // Quantity high
    g_rtu_frame.data[3]       = 0x0A;  // Quantity low (10)
}

uint16_t calc_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void bench_crc16_small() {
    uint16_t crc = calc_crc16(reinterpret_cast<uint8_t*>(&g_rtu_frame), 6);
    asm volatile("" : : "r"(crc) : "memory");
}

void bench_crc16_medium() {
    uint16_t crc = calc_crc16(reinterpret_cast<uint8_t*>(&g_rtu_frame), 64);
    asm volatile("" : : "r"(crc) : "memory");
}

void bench_frame_parse() {
    uint8_t addr   = g_rtu_frame.slave_addr;
    uint8_t func   = g_rtu_frame.function_code;
    uint16_t start = (g_rtu_frame.data[0] << 8) | g_rtu_frame.data[1];
    uint16_t qty   = (g_rtu_frame.data[2] << 8) | g_rtu_frame.data[3];
    asm volatile("" : : "r"(addr), "r"(func), "r"(start), "r"(qty) : "memory");
}

void bench_frame_build() {
    ModbusRTUFrame frame;
    frame.slave_addr    = 1;
    frame.function_code = 3;
    frame.data[0]       = 0x00;
    frame.data[1]       = 0x00;
    frame.data[2]       = 0x00;
    frame.data[3]       = 0x0A;
    frame.crc           = calc_crc16(reinterpret_cast<uint8_t*>(&frame), 6);
    asm volatile("" : : "r"(frame.crc) : "memory");
}

}  // namespace serial_benchmarks

//=============================================================================
// Registration Function
//=============================================================================

inline void register_transport_benchmarks() {
    auto& registry = BenchmarkRegistry::instance();

    // Buffer Operations
    {
        BenchmarkDef def;
        def.category   = BenchmarkCategory::TRANSPORTS;
        def.component  = "buffer";
        def.iterations = 100000;
        def.warmup     = 1000;

        def.name          = "memcpy_64";
        def.setup         = buffer_benchmarks::setup;
        def.benchmark     = buffer_benchmarks::bench_memcpy_64;
        def.target_p50_ns = 50;
        def.target_p99_ns = 500;
        registry.register_benchmark(def);

        def.name          = "memcpy_1k";
        def.benchmark     = buffer_benchmarks::bench_memcpy_1k;
        def.target_p50_ns = 200;
        def.target_p99_ns = 2000;
        registry.register_benchmark(def);

        def.name          = "memcpy_64k";
        def.benchmark     = buffer_benchmarks::bench_memcpy_64k;
        def.target_p50_ns = 10000;
        def.target_p99_ns = 50000;
        registry.register_benchmark(def);

        def.name      = "alloc_small";
        def.benchmark = buffer_benchmarks::bench_buffer_alloc_small;
        registry.register_benchmark(def);

        def.name      = "alloc_medium";
        def.benchmark = buffer_benchmarks::bench_buffer_alloc_medium;
        registry.register_benchmark(def);

        def.name      = "alloc_large";
        def.benchmark = buffer_benchmarks::bench_buffer_alloc_large;
        registry.register_benchmark(def);
    }

    // TCP
    {
        BenchmarkDef def;
        def.category   = BenchmarkCategory::TRANSPORTS;
        def.component  = "tcp";
        def.iterations = 100000;
        def.warmup     = 1000;

        def.name          = "header_parse";
        def.setup         = tcp_benchmarks::setup;
        def.benchmark     = tcp_benchmarks::bench_header_parse;
        def.target_p50_ns = 50;   // Relaxed for CI environments
        def.target_p99_ns = 500;
        registry.register_benchmark(def);

        def.name          = "header_build";
        def.setup         = tcp_benchmarks::setup;
        def.benchmark     = tcp_benchmarks::bench_header_build;
        def.target_p50_ns = 50;   // Explicit threshold (not inherited)
        def.target_p99_ns = 500;
        registry.register_benchmark(def);

        def.name          = "checksum_64";
        def.setup         = buffer_benchmarks::setup;
        def.benchmark     = tcp_benchmarks::bench_checksum_64;
        def.target_p50_ns = 100;
        def.target_p99_ns = 1000;
        registry.register_benchmark(def);

        def.name      = "checksum_1k";
        def.setup     = buffer_benchmarks::setup;
        def.benchmark = tcp_benchmarks::bench_checksum_1k;
        registry.register_benchmark(def);
    }

    // WebSocket
    {
        BenchmarkDef def;
        def.category   = BenchmarkCategory::TRANSPORTS;
        def.component  = "websocket";
        def.iterations = 100000;
        def.warmup     = 1000;

        def.name          = "frame_parse";
        def.setup         = websocket_benchmarks::setup;
        def.benchmark     = websocket_benchmarks::bench_frame_parse;
        def.target_p50_ns = 50;   // Relaxed for CI environments
        def.target_p99_ns = 500;
        registry.register_benchmark(def);

        def.name          = "mask_payload";
        def.setup         = websocket_benchmarks::setup;
        def.benchmark     = websocket_benchmarks::bench_mask_payload;
        def.target_p50_ns = 500;
        def.target_p99_ns = 5000;
        registry.register_benchmark(def);

        def.name      = "unmask_payload";
        def.setup     = websocket_benchmarks::setup;
        def.benchmark = websocket_benchmarks::bench_unmask_payload;
        registry.register_benchmark(def);

        def.name      = "build_frame";
        def.setup     = websocket_benchmarks::setup;
        def.benchmark = websocket_benchmarks::bench_build_frame;
        registry.register_benchmark(def);
    }

    // Serial (Modbus RTU)
    {
        BenchmarkDef def;
        def.category   = BenchmarkCategory::TRANSPORTS;
        def.component  = "serial";
        def.iterations = 100000;
        def.warmup     = 1000;

        def.name          = "crc16_small";
        def.setup         = serial_benchmarks::setup;
        def.benchmark     = serial_benchmarks::bench_crc16_small;
        def.target_p50_ns = 100;
        def.target_p99_ns = 1000;
        registry.register_benchmark(def);

        def.name          = "crc16_medium";
        def.setup         = serial_benchmarks::setup;
        def.benchmark     = serial_benchmarks::bench_crc16_medium;
        def.target_p50_ns = 1000;  // Medium buffer takes longer
        def.target_p99_ns = 5000;
        registry.register_benchmark(def);

        def.name          = "frame_parse";
        def.setup         = serial_benchmarks::setup;
        def.benchmark     = serial_benchmarks::bench_frame_parse;
        def.target_p50_ns = 50;   // Explicit threshold (not inherited)
        def.target_p99_ns = 500;
        registry.register_benchmark(def);

        def.name          = "frame_build";
        def.setup         = serial_benchmarks::setup;
        def.benchmark     = serial_benchmarks::bench_frame_build;
        def.target_p50_ns = 200;  // Includes CRC calculation
        def.target_p99_ns = 2000;
        registry.register_benchmark(def);
    }
}

}  // namespace ipb::benchmark
