#pragma once

/**
 * @file encryption.hpp
 * @brief Encryption transformer implementations
 *
 * Provides authenticated encryption transformers with a consistent interface.
 * All encryptors are bijective: decrypt(encrypt(data)) == data
 *
 * Supported algorithms:
 * - AES-256-GCM: Industry standard, hardware-accelerated
 * - AES-128-GCM: Faster, slightly smaller key
 * - ChaCha20-Poly1305: Alternative to AES, good on ARM
 *
 * Security Features:
 * - Authenticated encryption (AEAD)
 * - Random nonce/IV generation
 * - Timing-safe operations
 *
 * @code
 * // Generate a key
 * auto key = AesGcmTransformer::generate_key();
 *
 * // Encrypt data
 * AesGcmTransformer encryptor(key);
 * auto encrypted = encryptor.transform(plaintext);
 *
 * // Decrypt data
 * auto decrypted = encryptor.inverse(encrypted.value());
 * @endcode
 */

#include "../transformer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <span>
#include <vector>

namespace ipb::transform {

// ============================================================================
// ENCRYPTION HEADER
// ============================================================================

/**
 * @brief Header prepended to encrypted data
 *
 * Format:
 * - Magic: 2 bytes (0x45, 0x50 = "EP")
 * - Version: 1 byte
 * - Algorithm: 1 byte
 * - Nonce length: 1 byte
 * - Tag length: 1 byte
 * - Reserved: 2 bytes
 * - Nonce: variable (12 bytes for GCM)
 * - [Encrypted data]
 * - Auth tag: variable (16 bytes for GCM)
 */
struct EncryptionHeader {
    static constexpr uint8_t MAGIC_0     = 0x45;  // 'E'
    static constexpr uint8_t MAGIC_1     = 0x50;  // 'P'
    static constexpr uint8_t VERSION     = 0x01;
    static constexpr size_t  FIXED_SIZE  = 8;     // Fixed header portion

    uint8_t algorithm{0};
    uint8_t nonce_length{12};
    uint8_t tag_length{16};
    std::vector<uint8_t> nonce;

    /**
     * @brief Total header size including nonce
     */
    size_t total_size() const noexcept {
        return FIXED_SIZE + nonce_length;
    }

    /**
     * @brief Serialize header to buffer
     */
    void write_to(std::span<uint8_t> buffer) const noexcept {
        if (buffer.size() < total_size()) return;

        buffer[0] = MAGIC_0;
        buffer[1] = MAGIC_1;
        buffer[2] = VERSION;
        buffer[3] = algorithm;
        buffer[4] = nonce_length;
        buffer[5] = tag_length;
        buffer[6] = 0;  // Reserved
        buffer[7] = 0;  // Reserved

        std::memcpy(buffer.data() + FIXED_SIZE, nonce.data(),
                    std::min(nonce.size(), static_cast<size_t>(nonce_length)));
    }

    /**
     * @brief Parse header from buffer
     */
    static Result<EncryptionHeader> read_from(std::span<const uint8_t> buffer) noexcept {
        if (buffer.size() < FIXED_SIZE) {
            return ErrorCode::TRUNCATED_DATA;
        }

        if (buffer[0] != MAGIC_0 || buffer[1] != MAGIC_1) {
            return ErrorCode::INVALID_HEADER;
        }

        if (buffer[2] != VERSION) {
            return ErrorCode::UNSUPPORTED_VERSION;
        }

        EncryptionHeader header;
        header.algorithm    = buffer[3];
        header.nonce_length = buffer[4];
        header.tag_length   = buffer[5];

        if (buffer.size() < header.total_size()) {
            return ErrorCode::TRUNCATED_DATA;
        }

        header.nonce.resize(header.nonce_length);
        std::memcpy(header.nonce.data(), buffer.data() + FIXED_SIZE, header.nonce_length);

        return header;
    }

    /**
     * @brief Check if buffer starts with valid header
     */
    static bool is_valid_header(std::span<const uint8_t> buffer) noexcept {
        return buffer.size() >= FIXED_SIZE &&
               buffer[0] == MAGIC_0 &&
               buffer[1] == MAGIC_1 &&
               buffer[2] == VERSION;
    }
};

// ============================================================================
// KEY MANAGEMENT HELPERS
// ============================================================================

/**
 * @brief Key size constants
 */
struct KeySize {
    static constexpr size_t AES_128 = 16;
    static constexpr size_t AES_256 = 32;
    static constexpr size_t CHACHA  = 32;
};

/**
 * @brief Nonce/IV size constants
 */
struct NonceSize {
    static constexpr size_t GCM      = 12;
    static constexpr size_t CBC_IV   = 16;
    static constexpr size_t CHACHA   = 12;
};

/**
 * @brief Auth tag size constants
 */
struct TagSize {
    static constexpr size_t GCM      = 16;
    static constexpr size_t POLY1305 = 16;
};

/**
 * @brief Generate cryptographically secure random bytes
 */
inline std::vector<uint8_t> generate_random_bytes(size_t count) {
    std::vector<uint8_t> bytes(count);
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& byte : bytes) {
        byte = static_cast<uint8_t>(dist(rd));
    }
    return bytes;
}

// ============================================================================
// BASE ENCRYPTOR
// ============================================================================

/**
 * @brief Abstract base class for encryption algorithms
 */
class IEncryptor : public ITransformer {
public:
    explicit IEncryptor(std::span<const uint8_t> key, bool include_header = true)
        : key_(key.begin(), key.end()), include_header_(include_header) {}

    bool requires_key() const noexcept override { return true; }
    bool has_header() const noexcept override { return include_header_; }

    /**
     * @brief Get expected key size for this algorithm
     */
    virtual size_t key_size() const noexcept = 0;

    /**
     * @brief Get nonce/IV size for this algorithm
     */
    virtual size_t nonce_size() const noexcept = 0;

    /**
     * @brief Get auth tag size for this algorithm
     */
    virtual size_t tag_size() const noexcept = 0;

    /**
     * @brief Verify key is valid for this algorithm
     */
    bool verify_key() const noexcept {
        return key_.size() == key_size();
    }

protected:
    std::vector<uint8_t> key_;
    bool include_header_;

    /**
     * @brief Generate a random nonce
     */
    std::vector<uint8_t> generate_nonce() const {
        return generate_random_bytes(nonce_size());
    }

    /**
     * @brief Wrap encrypted data with header
     */
    std::vector<uint8_t> wrap_with_header(
        std::span<const uint8_t> ciphertext,
        std::span<const uint8_t> nonce,
        std::span<const uint8_t> tag) const {

        EncryptionHeader header;
        header.algorithm    = static_cast<uint8_t>(id()) & 0xFF;
        header.nonce_length = static_cast<uint8_t>(nonce.size());
        header.tag_length   = static_cast<uint8_t>(tag.size());
        header.nonce        = std::vector<uint8_t>(nonce.begin(), nonce.end());

        std::vector<uint8_t> output;
        output.resize(header.total_size() + ciphertext.size() + tag.size());

        header.write_to(output);
        std::memcpy(output.data() + header.total_size(),
                    ciphertext.data(), ciphertext.size());
        std::memcpy(output.data() + header.total_size() + ciphertext.size(),
                    tag.data(), tag.size());

        return output;
    }

    /**
     * @brief Parse header and extract components
     */
    struct ParsedEncryption {
        EncryptionHeader header;
        std::span<const uint8_t> ciphertext;
        std::span<const uint8_t> tag;
    };

    Result<ParsedEncryption> parse_encrypted(std::span<const uint8_t> input) const {
        auto header_result = EncryptionHeader::read_from(input);
        if (header_result.is_error()) {
            return header_result.error();
        }

        auto header = header_result.value();

        // Verify algorithm
        uint8_t expected_algo = static_cast<uint8_t>(id()) & 0xFF;
        if (header.algorithm != expected_algo) {
            return ErrorCode::FORMAT_UNSUPPORTED;
        }

        size_t header_size = header.total_size();
        size_t tag_size_val = header.tag_length;

        if (input.size() < header_size + tag_size_val) {
            return ErrorCode::TRUNCATED_DATA;
        }

        size_t ciphertext_size = input.size() - header_size - tag_size_val;

        ParsedEncryption result;
        result.header = std::move(header);
        result.ciphertext = input.subspan(header_size, ciphertext_size);
        result.tag = input.subspan(header_size + ciphertext_size, tag_size_val);

        return result;
    }
};

// ============================================================================
// AES-GCM ENCRYPTOR
// ============================================================================

/**
 * @brief AES-GCM authenticated encryption transformer
 *
 * Uses AES in Galois/Counter Mode for authenticated encryption.
 * Provides both confidentiality and integrity.
 */
class AesGcmTransformer final : public IEncryptor {
public:
    /**
     * @brief Key size enumeration
     */
    enum class KeyType { AES_128, AES_256 };

    /**
     * @brief Construct with key
     */
    explicit AesGcmTransformer(std::span<const uint8_t> key,
                                bool include_header = true)
        : IEncryptor(key, include_header),
          key_type_(key.size() == KeySize::AES_128 ? KeyType::AES_128 : KeyType::AES_256) {}

    /**
     * @brief Generate a random key
     */
    static std::vector<uint8_t> generate_key(KeyType type = KeyType::AES_256) {
        size_t size = (type == KeyType::AES_128) ? KeySize::AES_128 : KeySize::AES_256;
        return generate_random_bytes(size);
    }

    Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) override;
    Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) override;

    TransformerId id() const noexcept override {
        return (key_type_ == KeyType::AES_128)
            ? TransformerId::AES_128_GCM
            : TransformerId::AES_256_GCM;
    }

    size_t key_size() const noexcept override {
        return (key_type_ == KeyType::AES_128) ? KeySize::AES_128 : KeySize::AES_256;
    }

    size_t nonce_size() const noexcept override { return NonceSize::GCM; }
    size_t tag_size() const noexcept override { return TagSize::GCM; }

    double max_expansion_ratio() const noexcept override {
        // Header + nonce + tag overhead
        return 1.0 + (double)(EncryptionHeader::FIXED_SIZE + NonceSize::GCM + TagSize::GCM) / 100.0;
    }

    size_t estimate_output_size(size_t input_size) const noexcept override {
        return input_size + EncryptionHeader::FIXED_SIZE + NonceSize::GCM + TagSize::GCM;
    }

    std::unique_ptr<ITransformer> clone() const override {
        return std::make_unique<AesGcmTransformer>(key_, include_header_);
    }

private:
    KeyType key_type_;
};

// ============================================================================
// CHACHA20-POLY1305 ENCRYPTOR
// ============================================================================

/**
 * @brief ChaCha20-Poly1305 authenticated encryption transformer
 *
 * Alternative to AES-GCM, particularly efficient on platforms without
 * AES hardware acceleration (e.g., older ARM processors).
 */
class ChaCha20Poly1305Transformer final : public IEncryptor {
public:
    explicit ChaCha20Poly1305Transformer(std::span<const uint8_t> key,
                                          bool include_header = true)
        : IEncryptor(key, include_header) {}

    /**
     * @brief Generate a random key
     */
    static std::vector<uint8_t> generate_key() {
        return generate_random_bytes(KeySize::CHACHA);
    }

    Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) override;
    Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) override;

    TransformerId id() const noexcept override { return TransformerId::CHACHA20_POLY; }

    size_t key_size() const noexcept override { return KeySize::CHACHA; }
    size_t nonce_size() const noexcept override { return NonceSize::CHACHA; }
    size_t tag_size() const noexcept override { return TagSize::POLY1305; }

    double max_expansion_ratio() const noexcept override {
        return 1.0 + (double)(EncryptionHeader::FIXED_SIZE + NonceSize::CHACHA + TagSize::POLY1305) / 100.0;
    }

    size_t estimate_output_size(size_t input_size) const noexcept override {
        return input_size + EncryptionHeader::FIXED_SIZE + NonceSize::CHACHA + TagSize::POLY1305;
    }

    std::unique_ptr<ITransformer> clone() const override {
        return std::make_unique<ChaCha20Poly1305Transformer>(key_, include_header_);
    }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

/**
 * @brief Create an encryptor by algorithm ID
 */
inline std::unique_ptr<IEncryptor> make_encryptor(
    TransformerId algo,
    std::span<const uint8_t> key,
    bool include_header = true) {

    switch (algo) {
        case TransformerId::AES_128_GCM:
        case TransformerId::AES_256_GCM:
            return std::make_unique<AesGcmTransformer>(key, include_header);
        case TransformerId::CHACHA20_POLY:
            return std::make_unique<ChaCha20Poly1305Transformer>(key, include_header);
        default:
            return nullptr;
    }
}

/**
 * @brief Create an encryptor from config
 */
inline std::unique_ptr<IEncryptor> make_encryptor(const TransformConfig& config) {
    return make_encryptor(config.type, config.key, config.include_header);
}

/**
 * @brief Generate a key for the specified algorithm
 */
inline std::vector<uint8_t> generate_key_for(TransformerId algo) {
    switch (algo) {
        case TransformerId::AES_128_GCM:
            return generate_random_bytes(KeySize::AES_128);
        case TransformerId::AES_256_GCM:
            return generate_random_bytes(KeySize::AES_256);
        case TransformerId::CHACHA20_POLY:
            return generate_random_bytes(KeySize::CHACHA);
        default:
            return {};
    }
}

}  // namespace ipb::transform
