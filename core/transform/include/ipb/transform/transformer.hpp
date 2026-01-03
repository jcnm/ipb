#pragma once

/**
 * @file transformer.hpp
 * @brief Bijective Transformation System for IPB
 *
 * This module provides a composable, symmetric transformation pipeline
 * supporting compression, encryption, and custom transformations.
 *
 * Design Principles:
 * - **Bijective**: Every transform() has a corresponding inverse()
 * - **Symmetric**: Same interface for encode/decode operations
 * - **Composable**: Transformers can be chained in pipelines
 * - **Additive**: New transformers can be added without friction
 * - **Swappable**: Implementations can be replaced (e.g., zstd -> lz4)
 *
 * @code
 * // Example usage
 * auto pipeline = TransformPipeline::builder()
 *     .add<ZstdTransformer>(CompressionLevel::DEFAULT)
 *     .add<AesGcmTransformer>(key, nonce)
 *     .build();
 *
 * auto encrypted = pipeline.transform(data);
 * auto decrypted = pipeline.inverse(encrypted.value());
 * @endcode
 */

#include <ipb/common/error.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ipb::transform {

using ipb::common::ErrorCode;
using ipb::common::Result;

// ============================================================================
// TRANSFORM TYPES AND CATEGORIES
// ============================================================================

/**
 * @brief Categories of transformations
 */
enum class TransformCategory : uint8_t {
    COMPRESSION = 0x01,  // Data compression (lossless)
    ENCRYPTION  = 0x02,  // Data encryption
    ENCODING    = 0x03,  // Format encoding (base64, hex, etc.)
    CHECKSUM    = 0x04,  // Integrity verification
    CUSTOM      = 0xFF   // User-defined transformations
};

/**
 * @brief Specific transformer type identifiers
 */
enum class TransformerId : uint16_t {
    // Compression (0x01xx)
    NONE          = 0x0000,
    ZSTD          = 0x0101,
    LZ4           = 0x0102,
    SNAPPY        = 0x0103,
    GZIP          = 0x0104,
    BROTLI        = 0x0105,
    LZ4_HC        = 0x0106,  // LZ4 High Compression

    // Encryption (0x02xx)
    AES_128_GCM   = 0x0201,
    AES_256_GCM   = 0x0202,
    CHACHA20_POLY = 0x0203,
    AES_128_CBC   = 0x0204,
    AES_256_CBC   = 0x0205,

    // Encoding (0x03xx)
    BASE64        = 0x0301,
    BASE64_URL    = 0x0302,
    HEX           = 0x0303,

    // Checksum (0x04xx)
    CRC32         = 0x0401,
    XXH64         = 0x0402,
    SHA256        = 0x0403,

    // Custom (0xFFxx)
    CUSTOM_START  = 0xFF00
};

/**
 * @brief Get category from transformer ID
 */
constexpr TransformCategory get_category(TransformerId id) noexcept {
    return static_cast<TransformCategory>((static_cast<uint16_t>(id) >> 8) & 0xFF);
}

/**
 * @brief Get human-readable name for transformer ID
 */
constexpr std::string_view transformer_name(TransformerId id) noexcept {
    switch (id) {
        case TransformerId::NONE:          return "none";
        case TransformerId::ZSTD:          return "zstd";
        case TransformerId::LZ4:           return "lz4";
        case TransformerId::SNAPPY:        return "snappy";
        case TransformerId::GZIP:          return "gzip";
        case TransformerId::BROTLI:        return "brotli";
        case TransformerId::LZ4_HC:        return "lz4-hc";
        case TransformerId::AES_128_GCM:   return "aes-128-gcm";
        case TransformerId::AES_256_GCM:   return "aes-256-gcm";
        case TransformerId::CHACHA20_POLY: return "chacha20-poly1305";
        case TransformerId::AES_128_CBC:   return "aes-128-cbc";
        case TransformerId::AES_256_CBC:   return "aes-256-cbc";
        case TransformerId::BASE64:        return "base64";
        case TransformerId::BASE64_URL:    return "base64url";
        case TransformerId::HEX:           return "hex";
        case TransformerId::CRC32:         return "crc32";
        case TransformerId::XXH64:         return "xxhash64";
        case TransformerId::SHA256:        return "sha256";
        default:                           return "custom";
    }
}

// ============================================================================
// COMPRESSION LEVELS
// ============================================================================

/**
 * @brief Compression level presets
 *
 * These map to algorithm-specific levels internally.
 */
enum class CompressionLevel : int8_t {
    FASTEST     = 1,   // Minimum compression, maximum speed
    FAST        = 3,   // Good balance for real-time
    DEFAULT     = 6,   // Standard balance
    BETTER      = 9,   // Higher compression
    BEST        = 12,  // Maximum compression

    // Special values
    STORE       = 0,   // No compression (passthrough)
    ULTRA       = 22   // Extreme compression (slow)
};

// ============================================================================
// TRANSFORMER INTERFACE
// ============================================================================

/**
 * @brief Abstract base class for all bijective transformers
 *
 * A transformer implements a bijective (invertible) operation on byte data.
 * Every transform() must have a corresponding inverse() that recovers
 * the original data.
 *
 * Thread Safety:
 * - transform() and inverse() must be thread-safe
 * - Configuration should be immutable after construction
 *
 * @invariant For any valid input `data`:
 *     inverse(transform(data)) == data
 */
class ITransformer {
public:
    virtual ~ITransformer() = default;

    // ========== Core Bijective Operations ==========

    /**
     * @brief Apply forward transformation (compress, encrypt, encode)
     *
     * @param input Raw input data
     * @return Transformed data or error
     */
    virtual Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) = 0;

    /**
     * @brief Apply inverse transformation (decompress, decrypt, decode)
     *
     * @param input Previously transformed data
     * @return Original data or error
     */
    virtual Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) = 0;

    // ========== Metadata ==========

    /**
     * @brief Get transformer identifier
     */
    virtual TransformerId id() const noexcept = 0;

    /**
     * @brief Get transformer category
     */
    TransformCategory category() const noexcept { return get_category(id()); }

    /**
     * @brief Get human-readable name
     */
    virtual std::string_view name() const noexcept { return transformer_name(id()); }

    /**
     * @brief Get description of this transformer instance
     */
    virtual std::string description() const { return std::string(name()); }

    // ========== Capabilities ==========

    /**
     * @brief Check if transformer requires a key/secret
     */
    virtual bool requires_key() const noexcept { return false; }

    /**
     * @brief Check if transformer output includes metadata header
     */
    virtual bool has_header() const noexcept { return false; }

    /**
     * @brief Get maximum expected expansion ratio
     *
     * For compression, this is worst-case (incompressible data).
     * For encryption, includes overhead (IV, tag, padding).
     *
     * @return Ratio >= 1.0 (e.g., 1.1 means up to 10% expansion)
     */
    virtual double max_expansion_ratio() const noexcept { return 1.1; }

    /**
     * @brief Estimate output size for given input
     *
     * @param input_size Input data size in bytes
     * @return Estimated output size (may be larger for safety)
     */
    virtual size_t estimate_output_size(size_t input_size) const noexcept {
        return static_cast<size_t>(input_size * max_expansion_ratio()) + 64;
    }

    // ========== Cloning ==========

    /**
     * @brief Create a copy of this transformer with same configuration
     *
     * Useful for thread-local instances or pipeline cloning.
     */
    virtual std::unique_ptr<ITransformer> clone() const = 0;
};

// ============================================================================
// NULL/PASSTHROUGH TRANSFORMER
// ============================================================================

/**
 * @brief Identity transformer that passes data through unchanged
 *
 * Useful as a placeholder or for testing.
 */
class NullTransformer final : public ITransformer {
public:
    Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) override {
        return std::vector<uint8_t>(input.begin(), input.end());
    }

    Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) override {
        return std::vector<uint8_t>(input.begin(), input.end());
    }

    TransformerId id() const noexcept override { return TransformerId::NONE; }

    std::string_view name() const noexcept override { return "passthrough"; }

    double max_expansion_ratio() const noexcept override { return 1.0; }

    size_t estimate_output_size(size_t input_size) const noexcept override {
        return input_size;
    }

    std::unique_ptr<ITransformer> clone() const override {
        return std::make_unique<NullTransformer>();
    }
};

// ============================================================================
// TRANSFORMER CONFIGURATION
// ============================================================================

/**
 * @brief Configuration for creating transformers
 */
struct TransformConfig {
    TransformerId type{TransformerId::NONE};
    CompressionLevel level{CompressionLevel::DEFAULT};

    // For encryption
    std::vector<uint8_t> key;
    std::vector<uint8_t> nonce;  // IV for some algorithms

    // Additional options (algorithm-specific)
    bool include_header{true};   // Include metadata for self-describing output
    bool verify_integrity{true}; // Verify on inverse

    // Factory helper
    static TransformConfig compression(TransformerId type,
                                         CompressionLevel level = CompressionLevel::DEFAULT) {
        TransformConfig cfg;
        cfg.type  = type;
        cfg.level = level;
        return cfg;
    }

    static TransformConfig encryption(TransformerId type,
                                        std::span<const uint8_t> key_data,
                                        std::span<const uint8_t> nonce_data = {}) {
        TransformConfig cfg;
        cfg.type  = type;
        cfg.key   = std::vector<uint8_t>(key_data.begin(), key_data.end());
        cfg.nonce = std::vector<uint8_t>(nonce_data.begin(), nonce_data.end());
        return cfg;
    }
};

// ============================================================================
// TRANSFORM RESULT METADATA
// ============================================================================

/**
 * @brief Metadata about a transformation result
 */
struct TransformStats {
    size_t input_size{0};
    size_t output_size{0};
    double ratio{1.0};  // output_size / input_size
    std::chrono::nanoseconds duration{0};

    // Compression-specific
    double compression_ratio() const noexcept {
        return input_size > 0 ? static_cast<double>(output_size) / input_size : 1.0;
    }

    double space_savings() const noexcept {
        return input_size > 0 ? 1.0 - (static_cast<double>(output_size) / input_size) : 0.0;
    }
};

/**
 * @brief Result with optional statistics
 */
template <typename T>
struct TransformResult {
    T data;
    TransformStats stats;

    TransformResult(T d, TransformStats s = {}) : data(std::move(d)), stats(s) {}
};

}  // namespace ipb::transform
