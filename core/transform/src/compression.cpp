/**
 * @file compression.cpp
 * @brief Compression transformer implementations
 */

#include <ipb/transform/compression/compression.hpp>

// Conditional compilation based on available libraries
#if __has_include(<zstd.h>)
#define IPB_HAS_ZSTD 1
#include <zstd.h>
#else
#define IPB_HAS_ZSTD 0
#endif

#if __has_include(<lz4.h>)
#define IPB_HAS_LZ4 1
#include <lz4.h>
#if __has_include(<lz4hc.h>)
#include <lz4hc.h>
#endif
#else
#define IPB_HAS_LZ4 0
#endif

#if __has_include(<snappy.h>)
#define IPB_HAS_SNAPPY 1
#include <snappy.h>
#else
#define IPB_HAS_SNAPPY 0
#endif

#if __has_include(<zlib.h>)
#define IPB_HAS_ZLIB 1
#include <zlib.h>
#else
#define IPB_HAS_ZLIB 0
#endif

namespace ipb::transform {

// ============================================================================
// ZSTD IMPLEMENTATION
// ============================================================================

Result<std::vector<uint8_t>> ZstdTransformer::transform(std::span<const uint8_t> input) {
#if IPB_HAS_ZSTD
    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    // Get maximum compressed size
    size_t max_compressed_size = ZSTD_compressBound(input.size());
    std::vector<uint8_t> compressed(max_compressed_size);

    // Compress
    size_t compressed_size = ZSTD_compress(
        compressed.data(), max_compressed_size,
        input.data(), input.size(),
        native_level()
    );

    if (ZSTD_isError(compressed_size)) {
        return ErrorCode::ENCODING_ERROR;
    }

    compressed.resize(compressed_size);

    if (include_header_) {
        return wrap_with_header(compressed, input.size());
    }

    return compressed;
#else
    (void)input;
    return ErrorCode::FEATURE_UNAVAILABLE;
#endif
}

Result<std::vector<uint8_t>> ZstdTransformer::inverse(std::span<const uint8_t> input) {
#if IPB_HAS_ZSTD
    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    std::span<const uint8_t> compressed_data = input;
    size_t original_size = 0;

    if (include_header_) {
        auto unwrap_result = unwrap_header(input);
        if (unwrap_result.is_error()) {
            return unwrap_result.error();
        }
        auto [header, data] = unwrap_result.value();
        compressed_data = data;
        original_size = header.original_size;
    } else {
        // Without header, get size from ZSTD frame
        unsigned long long content_size = ZSTD_getFrameContentSize(
            compressed_data.data(), compressed_data.size());

        if (content_size == ZSTD_CONTENTSIZE_ERROR) {
            return ErrorCode::CORRUPT_DATA;
        }
        if (content_size == ZSTD_CONTENTSIZE_UNKNOWN) {
            // Use streaming decompression or estimate
            original_size = compressed_data.size() * 4;  // Rough estimate
        } else {
            original_size = static_cast<size_t>(content_size);
        }
    }

    std::vector<uint8_t> decompressed(original_size);

    size_t decompressed_size = ZSTD_decompress(
        decompressed.data(), decompressed.size(),
        compressed_data.data(), compressed_data.size()
    );

    if (ZSTD_isError(decompressed_size)) {
        return ErrorCode::DECODING_ERROR;
    }

    decompressed.resize(decompressed_size);
    return decompressed;
#else
    (void)input;
    return ErrorCode::FEATURE_UNAVAILABLE;
#endif
}

// ============================================================================
// LZ4 IMPLEMENTATION
// ============================================================================

Result<std::vector<uint8_t>> Lz4Transformer::transform(std::span<const uint8_t> input) {
#if IPB_HAS_LZ4
    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    // Check size limits
    if (input.size() > static_cast<size_t>(LZ4_MAX_INPUT_SIZE)) {
        return ErrorCode::MESSAGE_TOO_LARGE;
    }

    int input_size = static_cast<int>(input.size());
    int max_compressed_size = LZ4_compressBound(input_size);
    std::vector<uint8_t> compressed(static_cast<size_t>(max_compressed_size));

    int compressed_size;
    if (high_compression_) {
        compressed_size = LZ4_compress_HC(
            reinterpret_cast<const char*>(input.data()),
            reinterpret_cast<char*>(compressed.data()),
            input_size,
            max_compressed_size,
            native_level()
        );
    } else {
        compressed_size = LZ4_compress_default(
            reinterpret_cast<const char*>(input.data()),
            reinterpret_cast<char*>(compressed.data()),
            input_size,
            max_compressed_size
        );
    }

    if (compressed_size <= 0) {
        return ErrorCode::ENCODING_ERROR;
    }

    compressed.resize(static_cast<size_t>(compressed_size));

    if (include_header_) {
        return wrap_with_header(compressed, input.size());
    }

    return compressed;
#else
    (void)input;
    return ErrorCode::FEATURE_UNAVAILABLE;
#endif
}

Result<std::vector<uint8_t>> Lz4Transformer::inverse(std::span<const uint8_t> input) {
#if IPB_HAS_LZ4
    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    std::span<const uint8_t> compressed_data = input;
    size_t original_size = 0;

    if (include_header_) {
        auto unwrap_result = unwrap_header(input);
        if (unwrap_result.is_error()) {
            return unwrap_result.error();
        }
        auto [header, data] = unwrap_result.value();
        compressed_data = data;
        original_size = header.original_size;
    } else {
        // Without header, we cannot know the original size for LZ4
        // This is a limitation - use header mode for LZ4
        return ErrorCode::INVALID_HEADER;
    }

    std::vector<uint8_t> decompressed(original_size);

    int decompressed_size = LZ4_decompress_safe(
        reinterpret_cast<const char*>(compressed_data.data()),
        reinterpret_cast<char*>(decompressed.data()),
        static_cast<int>(compressed_data.size()),
        static_cast<int>(original_size)
    );

    if (decompressed_size < 0) {
        return ErrorCode::DECODING_ERROR;
    }

    decompressed.resize(static_cast<size_t>(decompressed_size));
    return decompressed;
#else
    (void)input;
    return ErrorCode::FEATURE_UNAVAILABLE;
#endif
}

// ============================================================================
// SNAPPY IMPLEMENTATION
// ============================================================================

Result<std::vector<uint8_t>> SnappyTransformer::transform(std::span<const uint8_t> input) {
#if IPB_HAS_SNAPPY
    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    size_t max_compressed_size = snappy::MaxCompressedLength(input.size());
    std::vector<uint8_t> compressed(max_compressed_size);

    size_t compressed_size = 0;
    snappy::RawCompress(
        reinterpret_cast<const char*>(input.data()),
        input.size(),
        reinterpret_cast<char*>(compressed.data()),
        &compressed_size
    );

    compressed.resize(compressed_size);

    if (include_header_) {
        return wrap_with_header(compressed, input.size());
    }

    return compressed;
#else
    (void)input;
    return ErrorCode::FEATURE_UNAVAILABLE;
#endif
}

Result<std::vector<uint8_t>> SnappyTransformer::inverse(std::span<const uint8_t> input) {
#if IPB_HAS_SNAPPY
    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    std::span<const uint8_t> compressed_data = input;
    size_t original_size = 0;

    if (include_header_) {
        auto unwrap_result = unwrap_header(input);
        if (unwrap_result.is_error()) {
            return unwrap_result.error();
        }
        auto [header, data] = unwrap_result.value();
        compressed_data = data;
        original_size = header.original_size;
    } else {
        // Snappy can determine uncompressed length from the stream
        if (!snappy::GetUncompressedLength(
                reinterpret_cast<const char*>(compressed_data.data()),
                compressed_data.size(),
                &original_size)) {
            return ErrorCode::CORRUPT_DATA;
        }
    }

    std::vector<uint8_t> decompressed(original_size);

    if (!snappy::RawUncompress(
            reinterpret_cast<const char*>(compressed_data.data()),
            compressed_data.size(),
            reinterpret_cast<char*>(decompressed.data()))) {
        return ErrorCode::DECODING_ERROR;
    }

    return decompressed;
#else
    (void)input;
    return ErrorCode::FEATURE_UNAVAILABLE;
#endif
}

// ============================================================================
// GZIP IMPLEMENTATION
// ============================================================================

Result<std::vector<uint8_t>> GzipTransformer::transform(std::span<const uint8_t> input) {
#if IPB_HAS_ZLIB
    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    // Estimate output size
    uLong max_compressed_size = compressBound(static_cast<uLong>(input.size()));
    std::vector<uint8_t> compressed(max_compressed_size);

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = compressed.data();
    stream.avail_out = static_cast<uInt>(compressed.size());

    // Initialize for gzip format (windowBits + 16)
    int ret = deflateInit2(&stream, native_level(), Z_DEFLATED,
                           15 + 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        return ErrorCode::ENCODING_ERROR;
    }

    ret = deflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&stream);
        return ErrorCode::ENCODING_ERROR;
    }

    size_t compressed_size = stream.total_out;
    deflateEnd(&stream);

    compressed.resize(compressed_size);

    if (include_header_) {
        return wrap_with_header(compressed, input.size());
    }

    return compressed;
#else
    (void)input;
    return ErrorCode::FEATURE_UNAVAILABLE;
#endif
}

Result<std::vector<uint8_t>> GzipTransformer::inverse(std::span<const uint8_t> input) {
#if IPB_HAS_ZLIB
    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    std::span<const uint8_t> compressed_data = input;
    size_t original_size = 0;

    if (include_header_) {
        auto unwrap_result = unwrap_header(input);
        if (unwrap_result.is_error()) {
            return unwrap_result.error();
        }
        auto [header, data] = unwrap_result.value();
        compressed_data = data;
        original_size = header.original_size;
    } else {
        // Estimate - gzip files store original size in last 4 bytes
        if (compressed_data.size() >= 4) {
            size_t offset = compressed_data.size() - 4;
            original_size = static_cast<uint32_t>(compressed_data[offset]) |
                           (static_cast<uint32_t>(compressed_data[offset + 1]) << 8) |
                           (static_cast<uint32_t>(compressed_data[offset + 2]) << 16) |
                           (static_cast<uint32_t>(compressed_data[offset + 3]) << 24);
        } else {
            original_size = compressed_data.size() * 4;
        }
    }

    std::vector<uint8_t> decompressed(original_size);

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(compressed_data.data());
    stream.avail_in = static_cast<uInt>(compressed_data.size());
    stream.next_out = decompressed.data();
    stream.avail_out = static_cast<uInt>(decompressed.size());

    // Initialize for gzip format (windowBits + 16)
    int ret = inflateInit2(&stream, 15 + 16);
    if (ret != Z_OK) {
        return ErrorCode::DECODING_ERROR;
    }

    ret = inflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END && ret != Z_OK) {
        inflateEnd(&stream);
        return ErrorCode::DECODING_ERROR;
    }

    size_t decompressed_size = stream.total_out;
    inflateEnd(&stream);

    decompressed.resize(decompressed_size);
    return decompressed;
#else
    (void)input;
    return ErrorCode::FEATURE_UNAVAILABLE;
#endif
}

}  // namespace ipb::transform
