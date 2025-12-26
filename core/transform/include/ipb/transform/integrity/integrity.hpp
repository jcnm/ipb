#pragma once

/**
 * @file integrity.hpp
 * @brief Integrity verification transformers
 *
 * Provides checksum/hash transformers for data integrity:
 * - CRC32: Fast cyclic redundancy check
 * - XXHash64: High-performance non-cryptographic hash
 *
 * These transformers append a checksum on transform() and verify/strip it on inverse().
 * They are bijective in the sense that inverse(transform(data)) == data.
 */

#include "../transformer.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace ipb::transform {

// ============================================================================
// CRC32 TABLES
// ============================================================================

namespace detail {

/**
 * @brief CRC32 lookup table (IEEE polynomial)
 */
constexpr std::array<uint32_t, 256> make_crc32_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        table[i] = crc;
    }
    return table;
}

inline constexpr auto crc32_table = make_crc32_table();

/**
 * @brief Calculate CRC32
 */
inline uint32_t crc32(std::span<const uint8_t> data, uint32_t initial = 0xFFFFFFFF) {
    uint32_t crc = initial;
    for (uint8_t byte : data) {
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

/**
 * @brief XXHash64 constants
 */
constexpr uint64_t XXH64_PRIME1 = 0x9E3779B185EBCA87ULL;
constexpr uint64_t XXH64_PRIME2 = 0xC2B2AE3D27D4EB4FULL;
constexpr uint64_t XXH64_PRIME3 = 0x165667B19E3779F9ULL;
constexpr uint64_t XXH64_PRIME4 = 0x85EBCA77C2B2AE63ULL;
constexpr uint64_t XXH64_PRIME5 = 0x27D4EB2F165667C5ULL;

/**
 * @brief Rotate left
 */
constexpr uint64_t rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

/**
 * @brief Read 64-bit little-endian
 */
inline uint64_t read64le(const uint8_t* p) {
    return static_cast<uint64_t>(p[0]) |
           (static_cast<uint64_t>(p[1]) << 8) |
           (static_cast<uint64_t>(p[2]) << 16) |
           (static_cast<uint64_t>(p[3]) << 24) |
           (static_cast<uint64_t>(p[4]) << 32) |
           (static_cast<uint64_t>(p[5]) << 40) |
           (static_cast<uint64_t>(p[6]) << 48) |
           (static_cast<uint64_t>(p[7]) << 56);
}

/**
 * @brief Read 32-bit little-endian
 */
inline uint32_t read32le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

/**
 * @brief Calculate XXHash64
 */
inline uint64_t xxhash64(std::span<const uint8_t> data, uint64_t seed = 0) {
    const uint8_t* p = data.data();
    const uint8_t* end = p + data.size();
    uint64_t h64;

    if (data.size() >= 32) {
        const uint8_t* limit = end - 32;
        uint64_t v1 = seed + XXH64_PRIME1 + XXH64_PRIME2;
        uint64_t v2 = seed + XXH64_PRIME2;
        uint64_t v3 = seed;
        uint64_t v4 = seed - XXH64_PRIME1;

        do {
            v1 += read64le(p) * XXH64_PRIME2;
            v1 = rotl64(v1, 31);
            v1 *= XXH64_PRIME1;
            p += 8;

            v2 += read64le(p) * XXH64_PRIME2;
            v2 = rotl64(v2, 31);
            v2 *= XXH64_PRIME1;
            p += 8;

            v3 += read64le(p) * XXH64_PRIME2;
            v3 = rotl64(v3, 31);
            v3 *= XXH64_PRIME1;
            p += 8;

            v4 += read64le(p) * XXH64_PRIME2;
            v4 = rotl64(v4, 31);
            v4 *= XXH64_PRIME1;
            p += 8;
        } while (p <= limit);

        h64 = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);

        auto merge = [](uint64_t h, uint64_t v) {
            v *= XXH64_PRIME2;
            v = rotl64(v, 31);
            v *= XXH64_PRIME1;
            h ^= v;
            h = h * XXH64_PRIME1 + XXH64_PRIME4;
            return h;
        };

        h64 = merge(h64, v1);
        h64 = merge(h64, v2);
        h64 = merge(h64, v3);
        h64 = merge(h64, v4);
    } else {
        h64 = seed + XXH64_PRIME5;
    }

    h64 += data.size();

    while (p + 8 <= end) {
        uint64_t k1 = read64le(p) * XXH64_PRIME2;
        k1 = rotl64(k1, 31);
        k1 *= XXH64_PRIME1;
        h64 ^= k1;
        h64 = rotl64(h64, 27) * XXH64_PRIME1 + XXH64_PRIME4;
        p += 8;
    }

    while (p + 4 <= end) {
        h64 ^= read32le(p) * XXH64_PRIME1;
        h64 = rotl64(h64, 23) * XXH64_PRIME2 + XXH64_PRIME3;
        p += 4;
    }

    while (p < end) {
        h64 ^= (*p) * XXH64_PRIME5;
        h64 = rotl64(h64, 11) * XXH64_PRIME1;
        p++;
    }

    h64 ^= h64 >> 33;
    h64 *= XXH64_PRIME2;
    h64 ^= h64 >> 29;
    h64 *= XXH64_PRIME3;
    h64 ^= h64 >> 32;

    return h64;
}

}  // namespace detail

// ============================================================================
// CRC32 TRANSFORMER
// ============================================================================

/**
 * @brief CRC32 integrity transformer
 *
 * Appends a 4-byte CRC32 checksum to the data.
 * On inverse, verifies and strips the checksum.
 */
class Crc32Transformer final : public ITransformer {
public:
    static constexpr size_t CHECKSUM_SIZE = 4;

    Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) override {
        std::vector<uint8_t> output;
        output.reserve(input.size() + CHECKSUM_SIZE);
        output.insert(output.end(), input.begin(), input.end());

        // Calculate and append CRC32
        uint32_t crc = detail::crc32(input);
        output.push_back(static_cast<uint8_t>(crc & 0xFF));
        output.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
        output.push_back(static_cast<uint8_t>((crc >> 16) & 0xFF));
        output.push_back(static_cast<uint8_t>((crc >> 24) & 0xFF));

        return output;
    }

    Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) override {
        if (input.size() < CHECKSUM_SIZE) {
            return ErrorCode::TRUNCATED_DATA;
        }

        // Extract stored checksum
        size_t data_size = input.size() - CHECKSUM_SIZE;
        uint32_t stored_crc =
            static_cast<uint32_t>(input[data_size]) |
            (static_cast<uint32_t>(input[data_size + 1]) << 8) |
            (static_cast<uint32_t>(input[data_size + 2]) << 16) |
            (static_cast<uint32_t>(input[data_size + 3]) << 24);

        // Calculate and verify
        auto data = input.subspan(0, data_size);
        uint32_t calculated_crc = detail::crc32(data);

        if (stored_crc != calculated_crc) {
            return ErrorCode::INVALID_CHECKSUM;
        }

        return std::vector<uint8_t>(data.begin(), data.end());
    }

    TransformerId id() const noexcept override { return TransformerId::CRC32; }

    double max_expansion_ratio() const noexcept override { return 1.0; }

    size_t estimate_output_size(size_t input_size) const noexcept override {
        return input_size + CHECKSUM_SIZE;
    }

    std::unique_ptr<ITransformer> clone() const override {
        return std::make_unique<Crc32Transformer>();
    }
};

// ============================================================================
// XXHASH64 TRANSFORMER
// ============================================================================

/**
 * @brief XXHash64 integrity transformer
 *
 * Appends an 8-byte XXHash64 checksum to the data.
 * XXHash is faster than CRC32 on modern CPUs.
 */
class XxHash64Transformer final : public ITransformer {
public:
    static constexpr size_t CHECKSUM_SIZE = 8;

    explicit XxHash64Transformer(uint64_t seed = 0) : seed_(seed) {}

    Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) override {
        std::vector<uint8_t> output;
        output.reserve(input.size() + CHECKSUM_SIZE);
        output.insert(output.end(), input.begin(), input.end());

        // Calculate and append XXHash64
        uint64_t hash = detail::xxhash64(input, seed_);
        for (int i = 0; i < 8; ++i) {
            output.push_back(static_cast<uint8_t>((hash >> (i * 8)) & 0xFF));
        }

        return output;
    }

    Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) override {
        if (input.size() < CHECKSUM_SIZE) {
            return ErrorCode::TRUNCATED_DATA;
        }

        // Extract stored hash
        size_t data_size = input.size() - CHECKSUM_SIZE;
        uint64_t stored_hash = 0;
        for (int i = 0; i < 8; ++i) {
            stored_hash |= static_cast<uint64_t>(input[data_size + i]) << (i * 8);
        }

        // Calculate and verify
        auto data = input.subspan(0, data_size);
        uint64_t calculated_hash = detail::xxhash64(data, seed_);

        if (stored_hash != calculated_hash) {
            return ErrorCode::INVALID_CHECKSUM;
        }

        return std::vector<uint8_t>(data.begin(), data.end());
    }

    TransformerId id() const noexcept override { return TransformerId::XXH64; }

    double max_expansion_ratio() const noexcept override { return 1.0; }

    size_t estimate_output_size(size_t input_size) const noexcept override {
        return input_size + CHECKSUM_SIZE;
    }

    std::unique_ptr<ITransformer> clone() const override {
        return std::make_unique<XxHash64Transformer>(seed_);
    }

private:
    uint64_t seed_;
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

/**
 * @brief Create an integrity checker by algorithm ID
 */
inline std::unique_ptr<ITransformer> make_integrity_checker(TransformerId algo,
                                                             uint64_t seed = 0) {
    switch (algo) {
        case TransformerId::CRC32:
            return std::make_unique<Crc32Transformer>();
        case TransformerId::XXH64:
            return std::make_unique<XxHash64Transformer>(seed);
        default:
            return nullptr;
    }
}

}  // namespace ipb::transform
