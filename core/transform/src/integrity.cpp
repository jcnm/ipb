/**
 * @file integrity.cpp
 * @brief Integrity transformer implementations using external libraries
 *
 * Uses:
 * - zlib for CRC32 (IEEE polynomial)
 * - xxHash library for XXHash64
 */

#include <ipb/transform/integrity/integrity.hpp>

#ifdef IPB_HAS_ZLIB
#include <zlib.h>
#endif

#ifdef IPB_HAS_XXHASH
#include <xxhash.h>
#endif

namespace ipb::transform {

// ============================================================================
// CRC32 IMPLEMENTATION (using zlib)
// ============================================================================

uint32_t compute_crc32(std::span<const uint8_t> data) noexcept {
#ifdef IPB_HAS_ZLIB
    // zlib's crc32 is highly optimized (hardware CRC32 on supported CPUs)
    return static_cast<uint32_t>(
        ::crc32(0L, data.data(), static_cast<uInt>(data.size()))
    );
#else
    // Fallback: simple byte-by-byte CRC32 (IEEE polynomial 0xEDB88320)
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
#endif
}

// ============================================================================
// XXHASH64 IMPLEMENTATION (using xxHash library)
// ============================================================================

uint64_t compute_xxhash64(std::span<const uint8_t> data, uint64_t seed) noexcept {
#ifdef IPB_HAS_XXHASH
    // xxHash library is highly optimized (SIMD on supported CPUs)
    return XXH64(data.data(), data.size(), seed);
#else
    // Fallback: minimal XXHash64 implementation
    // This is a simplified version - prefer using the library
    constexpr uint64_t PRIME1 = 0x9E3779B185EBCA87ULL;
    constexpr uint64_t PRIME2 = 0xC2B2AE3D27D4EB4FULL;
    constexpr uint64_t PRIME3 = 0x165667B19E3779F9ULL;
    constexpr uint64_t PRIME5 = 0x27D4EB2F165667C5ULL;

    auto rotl64 = [](uint64_t x, int r) -> uint64_t {
        return (x << r) | (x >> (64 - r));
    };

    uint64_t h64 = seed + PRIME5 + data.size();

    const uint8_t* p = data.data();
    const uint8_t* end = p + data.size();

    // Process remaining bytes
    while (p < end) {
        h64 ^= static_cast<uint64_t>(*p) * PRIME5;
        h64 = rotl64(h64, 11) * PRIME1;
        p++;
    }

    // Final mix
    h64 ^= h64 >> 33;
    h64 *= PRIME2;
    h64 ^= h64 >> 29;
    h64 *= PRIME3;
    h64 ^= h64 >> 32;

    return h64;
#endif
}

}  // namespace ipb::transform
