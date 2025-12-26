/**
 * @file integrity.cpp
 * @brief Integrity transformer implementations
 *
 * Provides optimized CRC32 and XXHash64 implementations with:
 * - Cache-aligned lookup tables
 * - SIMD hints for vectorization
 * - Streaming support for large data
 */

#include <ipb/transform/integrity/integrity.hpp>

#include <cstring>

namespace ipb::transform {

namespace detail {

// ============================================================================
// CRC32 OPTIMIZED IMPLEMENTATION
// ============================================================================

/**
 * @brief CRC32 lookup table (IEEE polynomial 0xEDB88320)
 * Pre-computed at compile time for zero runtime overhead.
 */
alignas(64) static constexpr std::array<uint32_t, 256> CRC32_TABLE = [] {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
        }
        table[i] = crc;
    }
    return table;
}();

/**
 * @brief CRC32 with 4-way parallel lookup (Slicing-by-4)
 * Processes 4 bytes per iteration for better throughput.
 */
alignas(64) static constexpr auto CRC32_TABLE_4 = [] {
    std::array<std::array<uint32_t, 256>, 4> tables{};

    // First table is standard CRC32
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
        }
        tables[0][i] = crc;
    }

    // Build remaining tables
    for (uint32_t i = 0; i < 256; ++i) {
        tables[1][i] = (tables[0][i] >> 8) ^ tables[0][tables[0][i] & 0xFF];
        tables[2][i] = (tables[1][i] >> 8) ^ tables[0][tables[1][i] & 0xFF];
        tables[3][i] = (tables[2][i] >> 8) ^ tables[0][tables[2][i] & 0xFF];
    }

    return tables;
}();

/**
 * @brief Optimized CRC32 using slicing-by-4 technique
 */
uint32_t crc32_optimized(std::span<const uint8_t> data, uint32_t initial) noexcept {
    uint32_t crc = initial;
    const uint8_t* p = data.data();
    size_t len = data.size();

    // Process unaligned prefix byte-by-byte
    while (len > 0 && (reinterpret_cast<uintptr_t>(p) & 3) != 0) {
        crc = CRC32_TABLE[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
        --len;
    }

    // Process aligned data 4 bytes at a time
    while (len >= 4) {
        uint32_t val;
        std::memcpy(&val, p, 4);
        crc ^= val;

        crc = CRC32_TABLE_4[3][crc & 0xFF] ^
              CRC32_TABLE_4[2][(crc >> 8) & 0xFF] ^
              CRC32_TABLE_4[1][(crc >> 16) & 0xFF] ^
              CRC32_TABLE_4[0][(crc >> 24) & 0xFF];

        p += 4;
        len -= 4;
    }

    // Process remaining bytes
    while (len > 0) {
        crc = CRC32_TABLE[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
        --len;
    }

    return ~crc;
}

// ============================================================================
// XXHASH64 OPTIMIZED IMPLEMENTATION
// ============================================================================

// XXHash64 prime constants are defined in the header (integrity.hpp)
// Using detail::XXH64_PRIME1, etc.

/**
 * @brief Rotate left (compile-time optimized)
 */
static inline constexpr uint64_t xxh_rotl64(uint64_t x, int r) noexcept {
    return (x << r) | (x >> (64 - r));
}

/**
 * @brief Read 64-bit little-endian (optimized for aligned access)
 */
static inline uint64_t xxh_read64le(const uint8_t* p) noexcept {
    uint64_t val;
    std::memcpy(&val, p, 8);
    // Assuming little-endian (most modern systems)
    return val;
}

/**
 * @brief Read 32-bit little-endian
 */
static inline uint32_t xxh_read32le(const uint8_t* p) noexcept {
    uint32_t val;
    std::memcpy(&val, p, 4);
    return val;
}

/**
 * @brief XXH64 round function
 */
static inline uint64_t xxh64_round(uint64_t acc, uint64_t input) noexcept {
    acc += input * XXH64_PRIME2;
    acc = xxh_rotl64(acc, 31);
    acc *= XXH64_PRIME1;
    return acc;
}

/**
 * @brief XXH64 merge round
 */
static inline uint64_t xxh64_merge_round(uint64_t acc, uint64_t val) noexcept {
    val = xxh64_round(0, val);
    acc ^= val;
    acc = acc * XXH64_PRIME1 + XXH64_PRIME4;
    return acc;
}

/**
 * @brief Optimized XXHash64 implementation
 */
uint64_t xxhash64_optimized(std::span<const uint8_t> data, uint64_t seed) noexcept {
    const uint8_t* p = data.data();
    const uint8_t* const end = p + data.size();
    uint64_t h64;

    if (data.size() >= 32) {
        const uint8_t* const limit = end - 32;
        uint64_t v1 = seed + XXH64_PRIME1 + XXH64_PRIME2;
        uint64_t v2 = seed + XXH64_PRIME2;
        uint64_t v3 = seed;
        uint64_t v4 = seed - XXH64_PRIME1;

        // Main loop - process 32 bytes at a time
        do {
            v1 = xxh64_round(v1, xxh_read64le(p));
            p += 8;
            v2 = xxh64_round(v2, xxh_read64le(p));
            p += 8;
            v3 = xxh64_round(v3, xxh_read64le(p));
            p += 8;
            v4 = xxh64_round(v4, xxh_read64le(p));
            p += 8;
        } while (p <= limit);

        h64 = xxh_rotl64(v1, 1) + xxh_rotl64(v2, 7) +
              xxh_rotl64(v3, 12) + xxh_rotl64(v4, 18);

        h64 = xxh64_merge_round(h64, v1);
        h64 = xxh64_merge_round(h64, v2);
        h64 = xxh64_merge_round(h64, v3);
        h64 = xxh64_merge_round(h64, v4);
    } else {
        h64 = seed + XXH64_PRIME5;
    }

    h64 += data.size();

    // Process remaining 8-byte chunks
    while (p + 8 <= end) {
        uint64_t k1 = xxh64_round(0, xxh_read64le(p));
        h64 ^= k1;
        h64 = xxh_rotl64(h64, 27) * XXH64_PRIME1 + XXH64_PRIME4;
        p += 8;
    }

    // Process remaining 4-byte chunk
    if (p + 4 <= end) {
        h64 ^= static_cast<uint64_t>(xxh_read32le(p)) * XXH64_PRIME1;
        h64 = xxh_rotl64(h64, 23) * XXH64_PRIME2 + XXH64_PRIME3;
        p += 4;
    }

    // Process remaining bytes
    while (p < end) {
        h64 ^= static_cast<uint64_t>(*p) * XXH64_PRIME5;
        h64 = xxh_rotl64(h64, 11) * XXH64_PRIME1;
        p++;
    }

    // Final mix
    h64 ^= h64 >> 33;
    h64 *= XXH64_PRIME2;
    h64 ^= h64 >> 29;
    h64 *= XXH64_PRIME3;
    h64 ^= h64 >> 32;

    return h64;
}

// ============================================================================
// STREAMING IMPLEMENTATIONS
// ============================================================================

/**
 * @brief CRC32 streaming state
 */
struct Crc32State {
    uint32_t crc = 0xFFFFFFFFu;

    void update(std::span<const uint8_t> data) noexcept {
        // Use internal state as initial
        uint32_t local_crc = crc;
        for (uint8_t byte : data) {
            local_crc = CRC32_TABLE[(local_crc ^ byte) & 0xFF] ^ (local_crc >> 8);
        }
        crc = local_crc;
    }

    uint32_t finalize() const noexcept {
        return ~crc;
    }

    void reset() noexcept {
        crc = 0xFFFFFFFFu;
    }
};

/**
 * @brief XXHash64 streaming state
 */
struct XxHash64State {
    uint64_t total_len = 0;
    uint64_t v1 = 0;
    uint64_t v2 = 0;
    uint64_t v3 = 0;
    uint64_t v4 = 0;
    std::array<uint8_t, 32> buffer{};
    size_t buffer_size = 0;
    uint64_t seed = 0;
    bool large_mode = false;

    explicit XxHash64State(uint64_t s = 0) noexcept : seed(s) {
        reset();
    }

    void reset() noexcept {
        total_len = 0;
        v1 = seed + XXH64_PRIME1 + XXH64_PRIME2;
        v2 = seed + XXH64_PRIME2;
        v3 = seed;
        v4 = seed - XXH64_PRIME1;
        buffer_size = 0;
        large_mode = false;
    }

    void update(std::span<const uint8_t> data) noexcept {
        const uint8_t* p = data.data();
        size_t len = data.size();

        total_len += len;

        // If buffer has data, try to fill it
        if (buffer_size > 0) {
            size_t to_fill = 32 - buffer_size;
            if (len < to_fill) {
                std::memcpy(buffer.data() + buffer_size, p, len);
                buffer_size += len;
                return;
            }

            std::memcpy(buffer.data() + buffer_size, p, to_fill);
            p += to_fill;
            len -= to_fill;

            // Process full buffer
            const uint8_t* bp = buffer.data();
            v1 = xxh64_round(v1, xxh_read64le(bp));
            v2 = xxh64_round(v2, xxh_read64le(bp + 8));
            v3 = xxh64_round(v3, xxh_read64le(bp + 16));
            v4 = xxh64_round(v4, xxh_read64le(bp + 24));
            buffer_size = 0;
            large_mode = true;
        }

        // Process full 32-byte blocks
        if (len >= 32) {
            large_mode = true;
            const uint8_t* const limit = p + len - 32;

            do {
                v1 = xxh64_round(v1, xxh_read64le(p));
                v2 = xxh64_round(v2, xxh_read64le(p + 8));
                v3 = xxh64_round(v3, xxh_read64le(p + 16));
                v4 = xxh64_round(v4, xxh_read64le(p + 24));
                p += 32;
            } while (p <= limit);
        }

        // Store remaining bytes in buffer
        if (p < data.data() + data.size()) {
            size_t remaining = data.data() + data.size() - p;
            std::memcpy(buffer.data(), p, remaining);
            buffer_size = remaining;
        }
    }

    uint64_t finalize() const noexcept {
        uint64_t h64;

        if (large_mode) {
            h64 = xxh_rotl64(v1, 1) + xxh_rotl64(v2, 7) +
                  xxh_rotl64(v3, 12) + xxh_rotl64(v4, 18);

            h64 = xxh64_merge_round(h64, v1);
            h64 = xxh64_merge_round(h64, v2);
            h64 = xxh64_merge_round(h64, v3);
            h64 = xxh64_merge_round(h64, v4);
        } else {
            h64 = seed + XXH64_PRIME5;
        }

        h64 += total_len;

        // Process remaining buffer
        const uint8_t* p = buffer.data();
        const uint8_t* const end = p + buffer_size;

        while (p + 8 <= end) {
            uint64_t k1 = xxh64_round(0, xxh_read64le(p));
            h64 ^= k1;
            h64 = xxh_rotl64(h64, 27) * XXH64_PRIME1 + XXH64_PRIME4;
            p += 8;
        }

        if (p + 4 <= end) {
            h64 ^= static_cast<uint64_t>(xxh_read32le(p)) * XXH64_PRIME1;
            h64 = xxh_rotl64(h64, 23) * XXH64_PRIME2 + XXH64_PRIME3;
            p += 4;
        }

        while (p < end) {
            h64 ^= static_cast<uint64_t>(*p) * XXH64_PRIME5;
            h64 = xxh_rotl64(h64, 11) * XXH64_PRIME1;
            p++;
        }

        // Final mix
        h64 ^= h64 >> 33;
        h64 *= XXH64_PRIME2;
        h64 ^= h64 >> 29;
        h64 *= XXH64_PRIME3;
        h64 ^= h64 >> 32;

        return h64;
    }
};

}  // namespace detail

// ============================================================================
// PUBLIC API IMPLEMENTATIONS
// ============================================================================

uint32_t compute_crc32(std::span<const uint8_t> data) noexcept {
    return detail::crc32_optimized(data, 0xFFFFFFFFu);
}

uint64_t compute_xxhash64(std::span<const uint8_t> data, uint64_t seed) noexcept {
    return detail::xxhash64_optimized(data, seed);
}

}  // namespace ipb::transform
