#pragma once

/**
 * @file transform.hpp
 * @brief IPB Bijective Transformation System - Main Header
 *
 * This is the main include file for the IPB transform module.
 * It provides a complete, composable, and symmetric transformation system.
 *
 * ## Design Principles
 *
 * - **Bijective**: Every transform() has a corresponding inverse()
 * - **Symmetric**: Same interface for encode/decode operations
 * - **Composable**: Transformers chain together in pipelines
 * - **Additive**: New transformers integrate without friction
 * - **Swappable**: Implementations replaceable (e.g., zstd ↔ lz4)
 *
 * ## Available Transformers
 *
 * | Category    | Transformers                          |
 * |-------------|---------------------------------------|
 * | Compression | ZSTD, LZ4, LZ4-HC, Snappy, GZIP      |
 * | Encryption  | AES-128-GCM, AES-256-GCM, ChaCha20   |
 * | Encoding    | Base64, Base64-URL, Hex              |
 * | Integrity   | CRC32, XXHash64                       |
 * | Custom      | User-defined transformers             |
 *
 * ## Quick Start
 *
 * @code
 * #include <ipb/transform/transform.hpp>
 *
 * using namespace ipb::transform;
 *
 * // Simple compression
 * ZstdTransformer compressor(CompressionLevel::FAST);
 * auto compressed = compressor.transform(data);
 * auto original = compressor.inverse(compressed.value());
 *
 * // Pipeline: compress -> encrypt -> encode
 * auto key = AesGcmTransformer::generate_key();
 * auto pipeline = TransformPipeline::builder()
 *     .add<ZstdTransformer>(CompressionLevel::DEFAULT)
 *     .add<AesGcmTransformer>(key)
 *     .add<Base64Transformer>()
 *     .build();
 *
 * auto result = pipeline.transform(plaintext);
 * auto decoded = pipeline.inverse(result.value());
 *
 * // Using factory
 * auto compressor = TransformRegistry::create(TransformerId::LZ4);
 * @endcode
 *
 * ## Pipeline Composition
 *
 * Pipelines apply transforms in order for transform(), and in reverse for inverse():
 *
 * ```
 * transform():  data → [A] → [B] → [C] → output
 * inverse():    data → [C⁻¹] → [B⁻¹] → [A⁻¹] → output
 * ```
 *
 * @see transformer.hpp for the base interface
 * @see transform_pipeline.hpp for pipeline composition
 */

// Core interfaces
#include "transformer.hpp"
#include "transform_pipeline.hpp"

// Transformer implementations
#include "compression/compression.hpp"
#include "encryption/encryption.hpp"
#include "encoding/encoding.hpp"
#include "integrity/integrity.hpp"

#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace ipb::transform {

// ============================================================================
// TRANSFORM REGISTRY
// ============================================================================

/**
 * @brief Registry for creating transformers by ID or name
 *
 * Provides factory methods for creating transformers dynamically.
 * Custom transformers can be registered at runtime.
 *
 * Thread-safe for concurrent access.
 */
class TransformRegistry {
public:
    /**
     * @brief Factory function type
     */
    using Factory = std::function<std::unique_ptr<ITransformer>(const TransformConfig&)>;

    /**
     * @brief Get singleton instance
     */
    static TransformRegistry& instance() {
        static TransformRegistry registry;
        return registry;
    }

    /**
     * @brief Create transformer by ID with default config
     */
    static std::unique_ptr<ITransformer> create(TransformerId id) {
        return instance().create_impl(id, TransformConfig{});
    }

    /**
     * @brief Create transformer by ID with config
     */
    static std::unique_ptr<ITransformer> create(TransformerId id, const TransformConfig& config) {
        return instance().create_impl(id, config);
    }

    /**
     * @brief Create transformer by name
     */
    static std::unique_ptr<ITransformer> create(std::string_view name) {
        auto id = instance().name_to_id(name);
        if (id == TransformerId::NONE) {
            return nullptr;
        }
        return create(id);
    }

    /**
     * @brief Register a custom transformer factory
     */
    void register_factory(TransformerId id, std::string name, Factory factory) {
        std::unique_lock lock(mutex_);
        factories_[id] = std::move(factory);
        name_map_[std::move(name)] = id;
    }

    /**
     * @brief Check if a transformer is available
     */
    bool is_available(TransformerId id) const {
        std::shared_lock lock(mutex_);
        return factories_.contains(id);
    }

    /**
     * @brief Get list of all registered transformer IDs
     */
    std::vector<TransformerId> available_transformers() const {
        std::shared_lock lock(mutex_);
        std::vector<TransformerId> ids;
        ids.reserve(factories_.size());
        for (const auto& [id, _] : factories_) {
            ids.push_back(id);
        }
        return ids;
    }

private:
    TransformRegistry() {
        register_defaults();
    }

    TransformRegistry(const TransformRegistry&) = delete;
    TransformRegistry& operator=(const TransformRegistry&) = delete;

    void register_defaults() {
        // Compression
        register_factory(TransformerId::ZSTD, "zstd",
            [](const TransformConfig& cfg) {
                return std::make_unique<ZstdTransformer>(cfg.level, cfg.include_header);
            });

        register_factory(TransformerId::LZ4, "lz4",
            [](const TransformConfig& cfg) {
                return std::make_unique<Lz4Transformer>(cfg.level, cfg.include_header, false);
            });

        register_factory(TransformerId::LZ4_HC, "lz4-hc",
            [](const TransformConfig& cfg) {
                return std::make_unique<Lz4Transformer>(cfg.level, cfg.include_header, true);
            });

        register_factory(TransformerId::SNAPPY, "snappy",
            [](const TransformConfig& cfg) {
                return std::make_unique<SnappyTransformer>(cfg.include_header);
            });

        register_factory(TransformerId::GZIP, "gzip",
            [](const TransformConfig& cfg) {
                return std::make_unique<GzipTransformer>(cfg.level, cfg.include_header);
            });

        // Encryption
        register_factory(TransformerId::AES_128_GCM, "aes-128-gcm",
            [](const TransformConfig& cfg) {
                return std::make_unique<AesGcmTransformer>(cfg.key, cfg.include_header);
            });

        register_factory(TransformerId::AES_256_GCM, "aes-256-gcm",
            [](const TransformConfig& cfg) {
                return std::make_unique<AesGcmTransformer>(cfg.key, cfg.include_header);
            });

        register_factory(TransformerId::CHACHA20_POLY, "chacha20-poly1305",
            [](const TransformConfig& cfg) {
                return std::make_unique<ChaCha20Poly1305Transformer>(cfg.key, cfg.include_header);
            });

        // Encoding
        register_factory(TransformerId::BASE64, "base64",
            [](const TransformConfig&) {
                return std::make_unique<Base64Transformer>();
            });

        register_factory(TransformerId::BASE64_URL, "base64url",
            [](const TransformConfig&) {
                return std::make_unique<Base64Transformer>(Base64Transformer::Variant::URL_SAFE);
            });

        register_factory(TransformerId::HEX, "hex",
            [](const TransformConfig&) {
                return std::make_unique<HexTransformer>();
            });

        // Integrity
        register_factory(TransformerId::CRC32, "crc32",
            [](const TransformConfig&) {
                return std::make_unique<Crc32Transformer>();
            });

        register_factory(TransformerId::XXH64, "xxhash64",
            [](const TransformConfig&) {
                return std::make_unique<XxHash64Transformer>();
            });

        // Passthrough
        register_factory(TransformerId::NONE, "none",
            [](const TransformConfig&) {
                return std::make_unique<NullTransformer>();
            });
    }

    std::unique_ptr<ITransformer> create_impl(TransformerId id, const TransformConfig& config) {
        std::shared_lock lock(mutex_);
        auto it = factories_.find(id);
        if (it == factories_.end()) {
            return nullptr;
        }
        return it->second(config);
    }

    TransformerId name_to_id(std::string_view name) const {
        std::shared_lock lock(mutex_);
        auto it = name_map_.find(std::string(name));
        if (it == name_map_.end()) {
            return TransformerId::NONE;
        }
        return it->second;
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<TransformerId, Factory> factories_;
    std::unordered_map<std::string, TransformerId> name_map_;
};

// ============================================================================
// PIPELINE BUILDER IMPLEMENTATION
// ============================================================================

inline PipelineBuilder& PipelineBuilder::compress(TransformerId algo, CompressionLevel level) {
    auto compressor = make_compressor(algo, level);
    if (compressor) {
        stages_.push_back(std::move(compressor));
    }
    return *this;
}

inline PipelineBuilder& PipelineBuilder::encrypt(TransformerId algo,
                                                   std::span<const uint8_t> key,
                                                   std::span<const uint8_t> nonce) {
    (void)nonce;  // Nonce is generated automatically
    auto encryptor = make_encryptor(algo, key);
    if (encryptor) {
        stages_.push_back(std::move(encryptor));
    }
    return *this;
}

// ============================================================================
// CONVENIENCE FUNCTIONS
// ============================================================================

/**
 * @brief Create a compression-only pipeline
 */
inline TransformPipeline make_compression_pipeline(
    TransformerId algo = TransformerId::ZSTD,
    CompressionLevel level = CompressionLevel::DEFAULT) {

    return TransformPipeline::builder()
        .compress(algo, level)
        .build();
}

/**
 * @brief Create an encryption-only pipeline
 */
inline TransformPipeline make_encryption_pipeline(
    TransformerId algo,
    std::span<const uint8_t> key) {

    return TransformPipeline::builder()
        .encrypt(algo, key)
        .build();
}

/**
 * @brief Create a compress-then-encrypt pipeline
 *
 * This is the recommended order for secure transmission:
 * 1. Compress first (works on plaintext patterns)
 * 2. Encrypt second (secures compressed data)
 */
inline TransformPipeline make_secure_pipeline(
    std::span<const uint8_t> key,
    TransformerId compression = TransformerId::ZSTD,
    TransformerId encryption = TransformerId::AES_256_GCM,
    CompressionLevel level = CompressionLevel::DEFAULT) {

    return TransformPipeline::builder()
        .compress(compression, level)
        .encrypt(encryption, key)
        .build();
}

/**
 * @brief Create a full pipeline with integrity check
 *
 * Order: compress -> encrypt -> checksum -> encode
 */
inline TransformPipeline make_full_pipeline(
    std::span<const uint8_t> key,
    TransformerId compression = TransformerId::ZSTD,
    TransformerId encryption = TransformerId::AES_256_GCM,
    TransformerId integrity = TransformerId::XXH64,
    TransformerId encoding = TransformerId::BASE64) {

    auto builder = TransformPipeline::builder()
        .compress(compression);

    if (!key.empty()) {
        builder.encrypt(encryption, key);
    }

    if (integrity != TransformerId::NONE) {
        builder.add(make_integrity_checker(integrity));
    }

    if (encoding != TransformerId::NONE) {
        builder.add(make_encoder(encoding));
    }

    return builder.build();
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * @brief Compress data using the specified algorithm
 */
inline Result<std::vector<uint8_t>> compress(
    std::span<const uint8_t> data,
    TransformerId algo = TransformerId::ZSTD,
    CompressionLevel level = CompressionLevel::DEFAULT) {

    auto compressor = make_compressor(algo, level);
    if (!compressor) {
        return ErrorCode::FEATURE_UNAVAILABLE;
    }
    return compressor->transform(data);
}

/**
 * @brief Decompress data
 */
inline Result<std::vector<uint8_t>> decompress(
    std::span<const uint8_t> data,
    TransformerId algo = TransformerId::ZSTD) {

    auto compressor = make_compressor(algo);
    if (!compressor) {
        return ErrorCode::FEATURE_UNAVAILABLE;
    }
    return compressor->inverse(data);
}

/**
 * @brief Encrypt data
 */
inline Result<std::vector<uint8_t>> encrypt(
    std::span<const uint8_t> data,
    std::span<const uint8_t> key,
    TransformerId algo = TransformerId::AES_256_GCM) {

    auto encryptor = make_encryptor(algo, key);
    if (!encryptor) {
        return ErrorCode::FEATURE_UNAVAILABLE;
    }
    return encryptor->transform(data);
}

/**
 * @brief Decrypt data
 */
inline Result<std::vector<uint8_t>> decrypt(
    std::span<const uint8_t> data,
    std::span<const uint8_t> key,
    TransformerId algo = TransformerId::AES_256_GCM) {

    auto encryptor = make_encryptor(algo, key);
    if (!encryptor) {
        return ErrorCode::FEATURE_UNAVAILABLE;
    }
    return encryptor->inverse(data);
}

/**
 * @brief Encode data to base64
 */
inline std::vector<uint8_t> encode_base64(std::span<const uint8_t> data) {
    Base64Transformer encoder;
    return encoder.transform(data).value_or(std::vector<uint8_t>{});
}

/**
 * @brief Decode data from base64
 */
inline Result<std::vector<uint8_t>> decode_base64(std::span<const uint8_t> data) {
    Base64Transformer decoder;
    return decoder.inverse(data);
}

/**
 * @brief Encode data to hex
 */
inline std::vector<uint8_t> encode_hex(std::span<const uint8_t> data) {
    HexTransformer encoder;
    return encoder.transform(data).value_or(std::vector<uint8_t>{});
}

/**
 * @brief Decode data from hex
 */
inline Result<std::vector<uint8_t>> decode_hex(std::span<const uint8_t> data) {
    HexTransformer decoder;
    return decoder.inverse(data);
}

/**
 * @brief Calculate CRC32 checksum
 */
inline uint32_t crc32(std::span<const uint8_t> data) {
    return detail::crc32(data);
}

/**
 * @brief Calculate XXHash64
 */
inline uint64_t xxhash64(std::span<const uint8_t> data, uint64_t seed = 0) {
    return detail::xxhash64(data, seed);
}

}  // namespace ipb::transform
