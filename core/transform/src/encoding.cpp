/**
 * @file encoding.cpp
 * @brief Encoding transformer implementations
 *
 * Note: Base64 and Hex encoders are header-only implementations
 * as they don't require external libraries. This file provides
 * any additional non-inline functionality and ensures proper
 * symbol visibility for shared library builds.
 */

#include <ipb/transform/encoding/encoding.hpp>

namespace ipb::transform {

// ============================================================================
// STATIC LOOKUP TABLES (for better cache locality)
// ============================================================================

namespace detail {

/**
 * @brief Base64 standard alphabet
 */
alignas(64) constexpr char BASE64_STANDARD_ALPHABET[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Base64 URL-safe alphabet
 */
alignas(64) constexpr char BASE64_URL_ALPHABET[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

/**
 * @brief Hex lowercase alphabet
 */
alignas(16) constexpr char HEX_LOWER_ALPHABET[17] = "0123456789abcdef";

/**
 * @brief Hex uppercase alphabet
 */
alignas(16) constexpr char HEX_UPPER_ALPHABET[17] = "0123456789ABCDEF";

/**
 * @brief Base64 decode table (standard)
 *
 * -1 = invalid, -2 = padding
 */
alignas(64) constexpr int8_t BASE64_DECODE_TABLE[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0-15
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 16-31
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,  // 32-47 (+, /)
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -2, -1, -1,  // 48-63 (0-9, =)
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,  // 64-79 (A-O)
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,  // 80-95 (P-Z)
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,  // 96-111 (a-o)
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,  // 112-127 (p-z)
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 128-143
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 144-159
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 160-175
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 176-191
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 192-207
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 208-223
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 224-239
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1   // 240-255
};

/**
 * @brief Base64 URL-safe decode table
 */
alignas(64) constexpr int8_t BASE64_URL_DECODE_TABLE[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1,  // - at pos 45
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -2, -1, -1,
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63,  // _ at pos 95
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

/**
 * @brief Hex decode table
 */
alignas(64) constexpr int8_t HEX_DECODE_TABLE[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,  // 0-9
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // A-F
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // a-f
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

}  // namespace detail

// ============================================================================
// OPTIMIZED ENCODING FUNCTIONS
// ============================================================================

/**
 * @brief Optimized Base64 encoding with SIMD hints
 */
std::vector<uint8_t> base64_encode_optimized(std::span<const uint8_t> input,
                                              bool url_safe,
                                              bool use_padding) {
    if (input.empty()) {
        return {};
    }

    const char* alphabet = url_safe
        ? detail::BASE64_URL_ALPHABET
        : detail::BASE64_STANDARD_ALPHABET;

    // Pre-allocate exact size
    size_t output_size = ((input.size() + 2) / 3) * 4;
    if (!use_padding) {
        size_t remainder = input.size() % 3;
        if (remainder > 0) {
            output_size -= (3 - remainder);
        }
    }

    std::vector<uint8_t> output;
    output.reserve(output_size);

    // Process 3 bytes at a time
    size_t i = 0;
    const size_t full_chunks = (input.size() / 3) * 3;

    // Main loop - process full 3-byte chunks
    for (; i < full_chunks; i += 3) {
        uint32_t triple =
            (static_cast<uint32_t>(input[i]) << 16) |
            (static_cast<uint32_t>(input[i + 1]) << 8) |
            static_cast<uint32_t>(input[i + 2]);

        output.push_back(static_cast<uint8_t>(alphabet[(triple >> 18) & 0x3F]));
        output.push_back(static_cast<uint8_t>(alphabet[(triple >> 12) & 0x3F]));
        output.push_back(static_cast<uint8_t>(alphabet[(triple >> 6) & 0x3F]));
        output.push_back(static_cast<uint8_t>(alphabet[triple & 0x3F]));
    }

    // Handle remaining bytes
    if (i < input.size()) {
        uint32_t triple = static_cast<uint32_t>(input[i]) << 16;
        if (i + 1 < input.size()) {
            triple |= static_cast<uint32_t>(input[i + 1]) << 8;
        }

        output.push_back(static_cast<uint8_t>(alphabet[(triple >> 18) & 0x3F]));
        output.push_back(static_cast<uint8_t>(alphabet[(triple >> 12) & 0x3F]));

        if (i + 1 < input.size()) {
            output.push_back(static_cast<uint8_t>(alphabet[(triple >> 6) & 0x3F]));
            if (use_padding) {
                output.push_back('=');
            }
        } else {
            if (use_padding) {
                output.push_back('=');
                output.push_back('=');
            }
        }
    }

    return output;
}

/**
 * @brief Optimized Base64 decoding with lookup table
 */
Result<std::vector<uint8_t>> base64_decode_optimized(std::span<const uint8_t> input,
                                                       bool url_safe) {
    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    const int8_t* decode_table = url_safe
        ? detail::BASE64_URL_DECODE_TABLE
        : detail::BASE64_DECODE_TABLE;

    // Count padding and validate
    size_t padding = 0;
    size_t len = input.size();
    while (len > 0 && input[len - 1] == '=') {
        padding++;
        len--;
    }

    if (padding > 2) {
        return ErrorCode::DECODING_ERROR;
    }

    // Calculate output size
    size_t output_size = (len * 3) / 4;
    std::vector<uint8_t> output;
    output.reserve(output_size);

    // Process 4 bytes at a time
    uint32_t buffer = 0;
    int bits = 0;

    for (size_t i = 0; i < len; ++i) {
        int8_t value = decode_table[input[i]];
        if (value < 0) {
            if (value == -2) continue;  // Padding
            return ErrorCode::DECODING_ERROR;
        }

        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }

    return output;
}

/**
 * @brief Optimized Hex encoding
 */
std::vector<uint8_t> hex_encode_optimized(std::span<const uint8_t> input,
                                           bool uppercase) {
    if (input.empty()) {
        return {};
    }

    const char* alphabet = uppercase
        ? detail::HEX_UPPER_ALPHABET
        : detail::HEX_LOWER_ALPHABET;

    std::vector<uint8_t> output;
    output.reserve(input.size() * 2);

    for (uint8_t byte : input) {
        output.push_back(static_cast<uint8_t>(alphabet[byte >> 4]));
        output.push_back(static_cast<uint8_t>(alphabet[byte & 0x0F]));
    }

    return output;
}

/**
 * @brief Optimized Hex decoding with lookup table
 */
Result<std::vector<uint8_t>> hex_decode_optimized(std::span<const uint8_t> input) {
    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    if (input.size() % 2 != 0) {
        return ErrorCode::DECODING_ERROR;
    }

    std::vector<uint8_t> output;
    output.reserve(input.size() / 2);

    for (size_t i = 0; i < input.size(); i += 2) {
        int8_t high = detail::HEX_DECODE_TABLE[input[i]];
        int8_t low = detail::HEX_DECODE_TABLE[input[i + 1]];

        if (high < 0 || low < 0) {
            return ErrorCode::DECODING_ERROR;
        }

        output.push_back(static_cast<uint8_t>((high << 4) | low));
    }

    return output;
}

}  // namespace ipb::transform
