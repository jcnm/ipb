/**
 * @file encoding.cpp
 * @brief Encoding transformer implementations using external libraries
 *
 * Uses:
 * - OpenSSL for Base64 encoding/decoding (when available)
 * - Simple lookup tables for Hex (trivial, no library needed)
 */

#include <ipb/transform/encoding/encoding.hpp>

#ifdef IPB_HAS_OPENSSL
#include <openssl/evp.h>
#include <openssl/buffer.h>
#endif

namespace ipb::transform {

namespace detail {

// Hex is trivial - no library needed, just lookup tables
alignas(16) constexpr char HEX_LOWER_ALPHABET[17] = "0123456789abcdef";
alignas(16) constexpr char HEX_UPPER_ALPHABET[17] = "0123456789ABCDEF";

alignas(64) constexpr int8_t HEX_DECODE_TABLE[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
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

// URL-safe Base64 character replacement
constexpr char BASE64_URL_ALPHABET[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

constexpr int8_t BASE64_URL_DECODE_TABLE[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -2, -1, -1,
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63,
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

}  // namespace detail

// ============================================================================
// BASE64 IMPLEMENTATION (using OpenSSL when available)
// ============================================================================

std::vector<uint8_t> base64_encode_optimized(std::span<const uint8_t> input,
                                              bool url_safe,
                                              bool use_padding) {
    if (input.empty()) {
        return {};
    }

#ifdef IPB_HAS_OPENSSL
    // Use OpenSSL's EVP_EncodeBlock for standard Base64
    if (!url_safe && use_padding) {
        // Calculate output size (EVP_EncodeBlock always pads)
        size_t output_len = ((input.size() + 2) / 3) * 4;
        std::vector<uint8_t> output(output_len + 1);  // +1 for null terminator

        int encoded_len = EVP_EncodeBlock(
            output.data(),
            input.data(),
            static_cast<int>(input.size())
        );

        output.resize(static_cast<size_t>(encoded_len));
        return output;
    }
#endif

    // Fallback / URL-safe / no-padding: use simple implementation
    const char* alphabet = url_safe
        ? detail::BASE64_URL_ALPHABET
        : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    size_t output_size = ((input.size() + 2) / 3) * 4;
    if (!use_padding) {
        size_t remainder = input.size() % 3;
        if (remainder > 0) {
            output_size -= (3 - remainder);
        }
    }

    std::vector<uint8_t> output;
    output.reserve(output_size);

    size_t i = 0;
    const size_t full_chunks = (input.size() / 3) * 3;

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

    if (i < input.size()) {
        uint32_t triple = static_cast<uint32_t>(input[i]) << 16;
        if (i + 1 < input.size()) {
            triple |= static_cast<uint32_t>(input[i + 1]) << 8;
        }

        output.push_back(static_cast<uint8_t>(alphabet[(triple >> 18) & 0x3F]));
        output.push_back(static_cast<uint8_t>(alphabet[(triple >> 12) & 0x3F]));

        if (i + 1 < input.size()) {
            output.push_back(static_cast<uint8_t>(alphabet[(triple >> 6) & 0x3F]));
            if (use_padding) output.push_back('=');
        } else {
            if (use_padding) {
                output.push_back('=');
                output.push_back('=');
            }
        }
    }

    return output;
}

Result<std::vector<uint8_t>> base64_decode_optimized(std::span<const uint8_t> input,
                                                       bool url_safe) {
    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

#ifdef IPB_HAS_OPENSSL
    // Use OpenSSL for standard Base64
    if (!url_safe) {
        // Calculate max output size
        size_t max_output = (input.size() * 3) / 4;
        std::vector<uint8_t> output(max_output);

        int decoded_len = EVP_DecodeBlock(
            output.data(),
            input.data(),
            static_cast<int>(input.size())
        );

        if (decoded_len < 0) {
            return ErrorCode::DECODING_ERROR;
        }

        // Adjust for padding (EVP_DecodeBlock doesn't account for it)
        size_t padding = 0;
        if (input.size() >= 1 && input[input.size() - 1] == '=') padding++;
        if (input.size() >= 2 && input[input.size() - 2] == '=') padding++;

        output.resize(static_cast<size_t>(decoded_len) - padding);
        return output;
    }
#endif

    // Fallback / URL-safe: use lookup table
    const int8_t* decode_table = url_safe
        ? detail::BASE64_URL_DECODE_TABLE
        : reinterpret_cast<const int8_t*>("\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
          "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
          "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x3e\xff\xff\xff\x3f"
          "\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\xff\xff\xff\xfe\xff\xff"
          "\xff\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e"
          "\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\xff\xff\xff\xff\xff"
          "\xff\x1a\x1b\x1c\x1d\x1e\x1f\x20\x21\x22\x23\x24\x25\x26\x27\x28"
          "\x29\x2a\x2b\x2c\x2d\x2e\x2f\x30\x31\x32\x33\xff\xff\xff\xff\xff");

    size_t padding = 0;
    size_t len = input.size();
    while (len > 0 && input[len - 1] == '=') {
        padding++;
        len--;
    }

    if (padding > 2) {
        return ErrorCode::DECODING_ERROR;
    }

    size_t output_size = (len * 3) / 4;
    std::vector<uint8_t> output;
    output.reserve(output_size);

    uint32_t buffer = 0;
    int bits = 0;

    for (size_t i = 0; i < len; ++i) {
        uint8_t c = input[i];
        int8_t value = (c < 128) ? decode_table[c] : -1;
        if (value < 0) {
            if (value == -2) continue;
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

// ============================================================================
// HEX IMPLEMENTATION (simple lookup tables - no library needed)
// ============================================================================

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
