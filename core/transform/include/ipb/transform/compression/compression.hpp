#pragma once

/**
 * @file compression.hpp
 * @brief Compression transformer implementations
 *
 * Provides lossless compression transformers with a consistent interface.
 * All compressors are bijective: decompress(compress(data)) == data
 *
 * Supported algorithms:
 * - ZSTD: Best ratio/speed balance, recommended for most use cases
 * - LZ4: Ultra-fast, lower ratio, ideal for real-time
 * - Snappy: Fast, designed for high throughput
 *
 * @code
 * // Using ZSTD with default level
 * ZstdTransformer compressor;
 * auto compressed = compressor.transform(data);
 * auto original = compressor.inverse(compressed.value());
 *
 * // Using LZ4 for real-time
 * Lz4Transformer fast_compressor;
 * @endcode
 */

#include "../transformer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace ipb::transform {

// ============================================================================
// COMPRESSION HEADER
// ============================================================================

/**
 * @brief Header prepended to compressed data for self-describing format
 *
 * Format (8 bytes):
 * - Magic: 2 bytes (0x49, 0x50 = "IP")
 * - Version: 1 byte
 * - Algorithm: 1 byte (TransformerId low byte)
 * - Original size: 4 bytes (little-endian)
 */
struct CompressionHeader {
    static constexpr uint8_t MAGIC_0  = 0x49;  // 'I'
    static constexpr uint8_t MAGIC_1  = 0x50;  // 'P'
    static constexpr uint8_t VERSION  = 0x01;
    static constexpr size_t  SIZE     = 8;

    uint8_t  algorithm{0};
    uint32_t original_size{0};

    /**
     * @brief Serialize header to buffer
     */
    void write_to(std::span<uint8_t> buffer) const noexcept {
        if (buffer.size() < SIZE) return;

        buffer[0] = MAGIC_0;
        buffer[1] = MAGIC_1;
        buffer[2] = VERSION;
        buffer[3] = algorithm;

        // Little-endian original size
        buffer[4] = static_cast<uint8_t>(original_size & 0xFF);
        buffer[5] = static_cast<uint8_t>((original_size >> 8) & 0xFF);
        buffer[6] = static_cast<uint8_t>((original_size >> 16) & 0xFF);
        buffer[7] = static_cast<uint8_t>((original_size >> 24) & 0xFF);
    }

    /**
     * @brief Parse header from buffer
     */
    static Result<CompressionHeader> read_from(std::span<const uint8_t> buffer) noexcept {
        if (buffer.size() < SIZE) {
            return ErrorCode::TRUNCATED_DATA;
        }

        if (buffer[0] != MAGIC_0 || buffer[1] != MAGIC_1) {
            return ErrorCode::INVALID_HEADER;
        }

        if (buffer[2] != VERSION) {
            return ErrorCode::UNSUPPORTED_VERSION;
        }

        CompressionHeader header;
        header.algorithm = buffer[3];
        header.original_size = static_cast<uint32_t>(buffer[4]) |
                               (static_cast<uint32_t>(buffer[5]) << 8) |
                               (static_cast<uint32_t>(buffer[6]) << 16) |
                               (static_cast<uint32_t>(buffer[7]) << 24);

        return header;
    }

    /**
     * @brief Check if buffer starts with valid header
     */
    static bool is_valid_header(std::span<const uint8_t> buffer) noexcept {
        return buffer.size() >= SIZE &&
               buffer[0] == MAGIC_0 &&
               buffer[1] == MAGIC_1 &&
               buffer[2] == VERSION;
    }
};

// ============================================================================
// BASE COMPRESSOR
// ============================================================================

/**
 * @brief Abstract base class for compression algorithms
 *
 * Provides common functionality for all compressors including
 * header handling and level configuration.
 */
class ICompressor : public ITransformer {
public:
    explicit ICompressor(CompressionLevel level = CompressionLevel::DEFAULT,
                         bool include_header = true)
        : level_(level), include_header_(include_header) {}

    bool has_header() const noexcept override { return include_header_; }

    CompressionLevel level() const noexcept { return level_; }

    /**
     * @brief Get algorithm-specific compression level
     *
     * Maps CompressionLevel enum to algorithm-specific integer.
     */
    virtual int native_level() const noexcept = 0;

protected:
    CompressionLevel level_;
    bool include_header_;

    /**
     * @brief Wrap compressed data with header
     */
    std::vector<uint8_t> wrap_with_header(std::span<const uint8_t> compressed,
                                           size_t original_size) const {
        std::vector<uint8_t> output;
        output.resize(CompressionHeader::SIZE + compressed.size());

        CompressionHeader header;
        header.algorithm = static_cast<uint8_t>(id()) & 0xFF;
        header.original_size = static_cast<uint32_t>(original_size);
        header.write_to(output);

        std::memcpy(output.data() + CompressionHeader::SIZE,
                    compressed.data(), compressed.size());

        return output;
    }

    /**
     * @brief Unwrap header and return compressed data
     */
    Result<std::pair<CompressionHeader, std::span<const uint8_t>>>
    unwrap_header(std::span<const uint8_t> input) const {
        auto header_result = CompressionHeader::read_from(input);
        if (header_result.is_error()) {
            return header_result.error();
        }

        auto header = header_result.value();

        // Verify algorithm matches
        uint8_t expected_algo = static_cast<uint8_t>(id()) & 0xFF;
        if (header.algorithm != expected_algo) {
            return ErrorCode::FORMAT_UNSUPPORTED;
        }

        return std::make_pair(header, input.subspan(CompressionHeader::SIZE));
    }
};

// ============================================================================
// ZSTD COMPRESSOR
// ============================================================================

/**
 * @brief Zstandard compression transformer
 *
 * ZSTD provides excellent compression ratio with good speed.
 * Recommended for most use cases.
 *
 * Features:
 * - Compression levels 1-22 (higher = better ratio, slower)
 * - Dictionary support (not yet implemented)
 * - Streaming support (not yet implemented)
 */
class ZstdTransformer final : public ICompressor {
public:
    explicit ZstdTransformer(CompressionLevel level = CompressionLevel::DEFAULT,
                              bool include_header = true)
        : ICompressor(level, include_header) {}

    Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) override;
    Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) override;

    TransformerId id() const noexcept override { return TransformerId::ZSTD; }

    int native_level() const noexcept override {
        switch (level_) {
            case CompressionLevel::STORE:   return 0;
            case CompressionLevel::FASTEST: return 1;
            case CompressionLevel::FAST:    return 3;
            case CompressionLevel::DEFAULT: return 6;
            case CompressionLevel::BETTER:  return 12;
            case CompressionLevel::BEST:    return 19;
            case CompressionLevel::ULTRA:   return 22;
            default:                        return 6;
        }
    }

    double max_expansion_ratio() const noexcept override {
        // ZSTD worst case is ~1.03x + header
        return 1.05;
    }

    std::unique_ptr<ITransformer> clone() const override {
        return std::make_unique<ZstdTransformer>(level_, include_header_);
    }
};

// ============================================================================
// LZ4 COMPRESSOR
// ============================================================================

/**
 * @brief LZ4 compression transformer
 *
 * LZ4 is extremely fast with moderate compression ratio.
 * Ideal for real-time applications where speed is critical.
 *
 * Features:
 * - Standard mode (LZ4) or High Compression mode (LZ4-HC)
 * - Very low latency
 */
class Lz4Transformer final : public ICompressor {
public:
    explicit Lz4Transformer(CompressionLevel level = CompressionLevel::DEFAULT,
                             bool include_header = true,
                             bool high_compression = false)
        : ICompressor(level, include_header), high_compression_(high_compression) {}

    Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) override;
    Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) override;

    TransformerId id() const noexcept override {
        return high_compression_ ? TransformerId::LZ4_HC : TransformerId::LZ4;
    }

    int native_level() const noexcept override {
        if (!high_compression_) {
            return 1;  // LZ4 standard has no levels
        }
        // LZ4-HC levels: 1-12
        switch (level_) {
            case CompressionLevel::STORE:   return 0;
            case CompressionLevel::FASTEST: return 1;
            case CompressionLevel::FAST:    return 3;
            case CompressionLevel::DEFAULT: return 6;
            case CompressionLevel::BETTER:  return 9;
            case CompressionLevel::BEST:    return 12;
            case CompressionLevel::ULTRA:   return 12;
            default:                        return 6;
        }
    }

    double max_expansion_ratio() const noexcept override {
        // LZ4 worst case is input_size + (input_size/255) + 16
        return 1.01;
    }

    size_t estimate_output_size(size_t input_size) const noexcept override {
        // LZ4 bound formula
        return input_size + (input_size / 255) + 16 + CompressionHeader::SIZE;
    }

    bool is_high_compression() const noexcept { return high_compression_; }

    std::unique_ptr<ITransformer> clone() const override {
        return std::make_unique<Lz4Transformer>(level_, include_header_, high_compression_);
    }

private:
    bool high_compression_;
};

// ============================================================================
// SNAPPY COMPRESSOR
// ============================================================================

/**
 * @brief Snappy compression transformer
 *
 * Snappy is designed for very high speed with reasonable compression.
 * Originally developed by Google for high-throughput scenarios.
 */
class SnappyTransformer final : public ICompressor {
public:
    explicit SnappyTransformer(bool include_header = true)
        : ICompressor(CompressionLevel::DEFAULT, include_header) {}

    Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) override;
    Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) override;

    TransformerId id() const noexcept override { return TransformerId::SNAPPY; }

    // Snappy has no compression levels
    int native_level() const noexcept override { return 0; }

    double max_expansion_ratio() const noexcept override {
        // Snappy: max_compressed = 32 + source_len + source_len/6
        return 1.2;
    }

    std::unique_ptr<ITransformer> clone() const override {
        return std::make_unique<SnappyTransformer>(include_header_);
    }
};

// ============================================================================
// GZIP COMPRESSOR (for compatibility)
// ============================================================================

/**
 * @brief GZIP compression transformer
 *
 * Standard GZIP format for compatibility with external tools.
 * Uses zlib internally.
 */
class GzipTransformer final : public ICompressor {
public:
    explicit GzipTransformer(CompressionLevel level = CompressionLevel::DEFAULT,
                              bool include_header = true)
        : ICompressor(level, include_header) {}

    Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) override;
    Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) override;

    TransformerId id() const noexcept override { return TransformerId::GZIP; }

    int native_level() const noexcept override {
        // zlib levels: 0-9
        switch (level_) {
            case CompressionLevel::STORE:   return 0;
            case CompressionLevel::FASTEST: return 1;
            case CompressionLevel::FAST:    return 3;
            case CompressionLevel::DEFAULT: return 6;
            case CompressionLevel::BETTER:  return 8;
            case CompressionLevel::BEST:    return 9;
            case CompressionLevel::ULTRA:   return 9;
            default:                        return 6;
        }
    }

    double max_expansion_ratio() const noexcept override {
        // GZIP can expand incompressible data
        return 1.1;
    }

    std::unique_ptr<ITransformer> clone() const override {
        return std::make_unique<GzipTransformer>(level_, include_header_);
    }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

/**
 * @brief Create a compressor by algorithm ID
 */
inline std::unique_ptr<ICompressor> make_compressor(
    TransformerId algo,
    CompressionLevel level = CompressionLevel::DEFAULT,
    bool include_header = true) {

    switch (algo) {
        case TransformerId::ZSTD:
            return std::make_unique<ZstdTransformer>(level, include_header);
        case TransformerId::LZ4:
            return std::make_unique<Lz4Transformer>(level, include_header, false);
        case TransformerId::LZ4_HC:
            return std::make_unique<Lz4Transformer>(level, include_header, true);
        case TransformerId::SNAPPY:
            return std::make_unique<SnappyTransformer>(include_header);
        case TransformerId::GZIP:
            return std::make_unique<GzipTransformer>(level, include_header);
        case TransformerId::NONE:
            return nullptr;
        default:
            return nullptr;
    }
}

/**
 * @brief Create a compressor from config
 */
inline std::unique_ptr<ICompressor> make_compressor(const TransformConfig& config) {
    return make_compressor(config.type, config.level, config.include_header);
}

}  // namespace ipb::transform
