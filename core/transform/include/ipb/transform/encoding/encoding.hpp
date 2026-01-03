#pragma once

/**
 * @file encoding.hpp
 * @brief Encoding transformer implementations
 *
 * Provides encoding/decoding transformers:
 * - Base64: Standard and URL-safe variants
 * - Hex: Hexadecimal encoding
 *
 * All encoders are bijective: decode(encode(data)) == data
 */

#include "../transformer.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ipb::transform {

// ============================================================================
// BASE64 TRANSFORMER
// ============================================================================

/**
 * @brief Base64 encoding transformer
 *
 * Encodes binary data to ASCII text using Base64.
 * Useful for transmitting binary data over text-only channels.
 */
class Base64Transformer final : public ITransformer {
public:
    /**
     * @brief Encoding variant
     */
    enum class Variant {
        STANDARD,   // Standard Base64 with + and /
        URL_SAFE    // URL-safe Base64 with - and _
    };

    explicit Base64Transformer(Variant variant = Variant::STANDARD,
                                bool use_padding = true)
        : variant_(variant), use_padding_(use_padding) {}

    Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) override {
        if (input.empty()) {
            return std::vector<uint8_t>{};
        }

        const char* alphabet = (variant_ == Variant::URL_SAFE)
            ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
            : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::vector<uint8_t> output;
        output.reserve(((input.size() + 2) / 3) * 4);

        for (size_t i = 0; i < input.size(); i += 3) {
            uint32_t triple = static_cast<uint32_t>(input[i]) << 16;
            if (i + 1 < input.size()) {
                triple |= static_cast<uint32_t>(input[i + 1]) << 8;
            }
            if (i + 2 < input.size()) {
                triple |= static_cast<uint32_t>(input[i + 2]);
            }

            output.push_back(static_cast<uint8_t>(alphabet[(triple >> 18) & 0x3F]));
            output.push_back(static_cast<uint8_t>(alphabet[(triple >> 12) & 0x3F]));

            if (i + 1 < input.size()) {
                output.push_back(static_cast<uint8_t>(alphabet[(triple >> 6) & 0x3F]));
            } else if (use_padding_) {
                output.push_back('=');
            }

            if (i + 2 < input.size()) {
                output.push_back(static_cast<uint8_t>(alphabet[triple & 0x3F]));
            } else if (use_padding_) {
                output.push_back('=');
            }
        }

        return output;
    }

    Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) override {
        if (input.empty()) {
            return std::vector<uint8_t>{};
        }

        // Build decode table
        std::array<int8_t, 256> decode_table;
        decode_table.fill(-1);

        const char* alphabet = (variant_ == Variant::URL_SAFE)
            ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
            : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        for (int i = 0; i < 64; ++i) {
            decode_table[static_cast<uint8_t>(alphabet[i])] = static_cast<int8_t>(i);
        }
        decode_table['='] = 0;  // Padding

        // Count padding
        size_t padding = 0;
        size_t len = input.size();
        while (len > 0 && input[len - 1] == '=') {
            padding++;
            len--;
        }

        std::vector<uint8_t> output;
        output.reserve((len * 3) / 4);

        for (size_t i = 0; i < input.size(); i += 4) {
            uint32_t sextet[4] = {0, 0, 0, 0};
            size_t valid = 0;

            for (size_t j = 0; j < 4 && i + j < input.size(); ++j) {
                uint8_t c = input[i + j];
                if (c == '=') break;
                if (decode_table[c] < 0) {
                    return ErrorCode::DECODING_ERROR;
                }
                sextet[j] = static_cast<uint32_t>(decode_table[c]);
                valid++;
            }

            if (valid >= 2) {
                output.push_back(static_cast<uint8_t>((sextet[0] << 2) | (sextet[1] >> 4)));
            }
            if (valid >= 3) {
                output.push_back(static_cast<uint8_t>((sextet[1] << 4) | (sextet[2] >> 2)));
            }
            if (valid >= 4) {
                output.push_back(static_cast<uint8_t>((sextet[2] << 6) | sextet[3]));
            }
        }

        return output;
    }

    TransformerId id() const noexcept override {
        return (variant_ == Variant::URL_SAFE)
            ? TransformerId::BASE64_URL
            : TransformerId::BASE64;
    }

    double max_expansion_ratio() const noexcept override {
        return 4.0 / 3.0 + 0.01;  // ~1.34
    }

    size_t estimate_output_size(size_t input_size) const noexcept override {
        return ((input_size + 2) / 3) * 4;
    }

    std::unique_ptr<ITransformer> clone() const override {
        return std::make_unique<Base64Transformer>(variant_, use_padding_);
    }

private:
    Variant variant_;
    bool use_padding_;
};

// ============================================================================
// HEX TRANSFORMER
// ============================================================================

/**
 * @brief Hexadecimal encoding transformer
 *
 * Encodes binary data to hexadecimal ASCII text.
 * Each byte becomes two hex characters.
 */
class HexTransformer final : public ITransformer {
public:
    explicit HexTransformer(bool uppercase = false)
        : uppercase_(uppercase) {}

    Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) override {
        if (input.empty()) {
            return std::vector<uint8_t>{};
        }

        const char* hex_chars = uppercase_
            ? "0123456789ABCDEF"
            : "0123456789abcdef";

        std::vector<uint8_t> output;
        output.reserve(input.size() * 2);

        for (uint8_t byte : input) {
            output.push_back(static_cast<uint8_t>(hex_chars[byte >> 4]));
            output.push_back(static_cast<uint8_t>(hex_chars[byte & 0x0F]));
        }

        return output;
    }

    Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) override {
        if (input.empty()) {
            return std::vector<uint8_t>{};
        }

        if (input.size() % 2 != 0) {
            return ErrorCode::DECODING_ERROR;
        }

        std::vector<uint8_t> output;
        output.reserve(input.size() / 2);

        for (size_t i = 0; i < input.size(); i += 2) {
            int high = hex_value(input[i]);
            int low = hex_value(input[i + 1]);

            if (high < 0 || low < 0) {
                return ErrorCode::DECODING_ERROR;
            }

            output.push_back(static_cast<uint8_t>((high << 4) | low));
        }

        return output;
    }

    TransformerId id() const noexcept override { return TransformerId::HEX; }

    double max_expansion_ratio() const noexcept override { return 2.0; }

    size_t estimate_output_size(size_t input_size) const noexcept override {
        return input_size * 2;
    }

    std::unique_ptr<ITransformer> clone() const override {
        return std::make_unique<HexTransformer>(uppercase_);
    }

private:
    bool uppercase_;

    static int hex_value(uint8_t c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

/**
 * @brief Create an encoder by algorithm ID
 */
inline std::unique_ptr<ITransformer> make_encoder(TransformerId algo) {
    switch (algo) {
        case TransformerId::BASE64:
            return std::make_unique<Base64Transformer>(Base64Transformer::Variant::STANDARD);
        case TransformerId::BASE64_URL:
            return std::make_unique<Base64Transformer>(Base64Transformer::Variant::URL_SAFE);
        case TransformerId::HEX:
            return std::make_unique<HexTransformer>();
        default:
            return nullptr;
    }
}

}  // namespace ipb::transform
