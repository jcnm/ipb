/**
 * @file encryption.cpp
 * @brief Encryption transformer implementations using OpenSSL
 */

#include <ipb/transform/encryption/encryption.hpp>

// OpenSSL support
#if __has_include(<openssl/evp.h>)
#define IPB_HAS_OPENSSL 1
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#else
#define IPB_HAS_OPENSSL 0
#endif

namespace ipb::transform {

// ============================================================================
// OpenSSL RAII Helpers
// ============================================================================

#if IPB_HAS_OPENSSL

/**
 * @brief RAII wrapper for EVP_CIPHER_CTX
 */
class CipherContext {
public:
    CipherContext() : ctx_(EVP_CIPHER_CTX_new()) {}

    ~CipherContext() {
        if (ctx_) {
            EVP_CIPHER_CTX_free(ctx_);
        }
    }

    CipherContext(const CipherContext&) = delete;
    CipherContext& operator=(const CipherContext&) = delete;

    EVP_CIPHER_CTX* get() { return ctx_; }
    operator EVP_CIPHER_CTX*() { return ctx_; }

    bool valid() const { return ctx_ != nullptr; }

private:
    EVP_CIPHER_CTX* ctx_;
};

/**
 * @brief Generate cryptographically secure random bytes using OpenSSL
 */
static std::vector<uint8_t> openssl_random_bytes(size_t count) {
    std::vector<uint8_t> bytes(count);
    if (RAND_bytes(bytes.data(), static_cast<int>(count)) != 1) {
        // Fallback to std::random_device
        std::random_device rd;
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& byte : bytes) {
            byte = static_cast<uint8_t>(dist(rd));
        }
    }
    return bytes;
}

#endif  // IPB_HAS_OPENSSL

// ============================================================================
// AES-GCM IMPLEMENTATION
// ============================================================================

Result<std::vector<uint8_t>> AesGcmTransformer::transform(std::span<const uint8_t> input) {
#if IPB_HAS_OPENSSL
    if (!verify_key()) {
        return ErrorCode::KEY_INVALID;
    }

    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    // Generate random nonce
    auto nonce = openssl_random_bytes(NonceSize::GCM);

    // Select cipher based on key size
    const EVP_CIPHER* cipher = (key_type_ == KeyType::AES_128)
        ? EVP_aes_128_gcm()
        : EVP_aes_256_gcm();

    CipherContext ctx;
    if (!ctx.valid()) {
        return ErrorCode::ENCRYPTION_FAILED;
    }

    // Initialize encryption
    if (EVP_EncryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) != 1) {
        return ErrorCode::ENCRYPTION_FAILED;
    }

    // Set IV length
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                            static_cast<int>(NonceSize::GCM), nullptr) != 1) {
        return ErrorCode::ENCRYPTION_FAILED;
    }

    // Set key and IV
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key_.data(), nonce.data()) != 1) {
        return ErrorCode::ENCRYPTION_FAILED;
    }

    // Allocate output buffer
    std::vector<uint8_t> ciphertext(input.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int ciphertext_len = 0;

    // Encrypt
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                          input.data(), static_cast<int>(input.size())) != 1) {
        return ErrorCode::ENCRYPTION_FAILED;
    }
    ciphertext_len = len;

    // Finalize
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        return ErrorCode::ENCRYPTION_FAILED;
    }
    ciphertext_len += len;
    ciphertext.resize(static_cast<size_t>(ciphertext_len));

    // Get auth tag
    std::vector<uint8_t> tag(TagSize::GCM);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
                            static_cast<int>(TagSize::GCM), tag.data()) != 1) {
        return ErrorCode::ENCRYPTION_FAILED;
    }

    if (include_header_) {
        return wrap_with_header(ciphertext, nonce, tag);
    } else {
        // Without header, prepend nonce and append tag
        std::vector<uint8_t> output;
        output.reserve(nonce.size() + ciphertext.size() + tag.size());
        output.insert(output.end(), nonce.begin(), nonce.end());
        output.insert(output.end(), ciphertext.begin(), ciphertext.end());
        output.insert(output.end(), tag.begin(), tag.end());
        return output;
    }
#else
    (void)input;
    return ErrorCode::FEATURE_UNAVAILABLE;
#endif
}

Result<std::vector<uint8_t>> AesGcmTransformer::inverse(std::span<const uint8_t> input) {
#if IPB_HAS_OPENSSL
    if (!verify_key()) {
        return ErrorCode::KEY_INVALID;
    }

    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    std::vector<uint8_t> nonce_storage;  // Storage to keep nonce alive
    std::span<const uint8_t> nonce;
    std::span<const uint8_t> ciphertext;
    std::span<const uint8_t> tag;

    if (include_header_) {
        auto parsed = parse_encrypted(input);
        if (parsed.is_error()) {
            return parsed.error();
        }
        // Copy nonce to local storage to avoid dangling span
        nonce_storage = std::move(parsed.value().header.nonce);
        nonce = nonce_storage;
        ciphertext = parsed.value().ciphertext;
        tag = parsed.value().tag;
    } else {
        // Without header: nonce || ciphertext || tag
        if (input.size() < NonceSize::GCM + TagSize::GCM) {
            return ErrorCode::TRUNCATED_DATA;
        }
        nonce = input.subspan(0, NonceSize::GCM);
        tag = input.subspan(input.size() - TagSize::GCM, TagSize::GCM);
        ciphertext = input.subspan(NonceSize::GCM,
                                   input.size() - NonceSize::GCM - TagSize::GCM);
    }

    // Select cipher
    const EVP_CIPHER* cipher = (key_type_ == KeyType::AES_128)
        ? EVP_aes_128_gcm()
        : EVP_aes_256_gcm();

    CipherContext ctx;
    if (!ctx.valid()) {
        return ErrorCode::DECRYPTION_FAILED;
    }

    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) != 1) {
        return ErrorCode::DECRYPTION_FAILED;
    }

    // Set IV length
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                            static_cast<int>(nonce.size()), nullptr) != 1) {
        return ErrorCode::DECRYPTION_FAILED;
    }

    // Set key and IV
    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key_.data(), nonce.data()) != 1) {
        return ErrorCode::DECRYPTION_FAILED;
    }

    // Allocate output buffer
    std::vector<uint8_t> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int plaintext_len = 0;

    // Decrypt
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                          ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
        return ErrorCode::DECRYPTION_FAILED;
    }
    plaintext_len = len;

    // Set expected tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
                            static_cast<int>(tag.size()),
                            const_cast<uint8_t*>(tag.data())) != 1) {
        return ErrorCode::DECRYPTION_FAILED;
    }

    // Finalize and verify tag
    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    if (ret <= 0) {
        // Tag verification failed - data may be corrupted or tampered
        return ErrorCode::SIGNATURE_INVALID;
    }
    plaintext_len += len;
    plaintext.resize(static_cast<size_t>(plaintext_len));

    return plaintext;
#else
    (void)input;
    return ErrorCode::FEATURE_UNAVAILABLE;
#endif
}

// ============================================================================
// CHACHA20-POLY1305 IMPLEMENTATION
// ============================================================================

Result<std::vector<uint8_t>> ChaCha20Poly1305Transformer::transform(std::span<const uint8_t> input) {
#if IPB_HAS_OPENSSL
    if (!verify_key()) {
        return ErrorCode::KEY_INVALID;
    }

    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    // Generate random nonce
    auto nonce = openssl_random_bytes(NonceSize::CHACHA);

    const EVP_CIPHER* cipher = EVP_chacha20_poly1305();

    CipherContext ctx;
    if (!ctx.valid()) {
        return ErrorCode::ENCRYPTION_FAILED;
    }

    // Initialize encryption
    if (EVP_EncryptInit_ex(ctx, cipher, nullptr, key_.data(), nonce.data()) != 1) {
        return ErrorCode::ENCRYPTION_FAILED;
    }

    // Allocate output buffer
    std::vector<uint8_t> ciphertext(input.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int ciphertext_len = 0;

    // Encrypt
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                          input.data(), static_cast<int>(input.size())) != 1) {
        return ErrorCode::ENCRYPTION_FAILED;
    }
    ciphertext_len = len;

    // Finalize
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        return ErrorCode::ENCRYPTION_FAILED;
    }
    ciphertext_len += len;
    ciphertext.resize(static_cast<size_t>(ciphertext_len));

    // Get auth tag
    std::vector<uint8_t> tag(TagSize::POLY1305);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG,
                            static_cast<int>(TagSize::POLY1305), tag.data()) != 1) {
        return ErrorCode::ENCRYPTION_FAILED;
    }

    if (include_header_) {
        return wrap_with_header(ciphertext, nonce, tag);
    } else {
        std::vector<uint8_t> output;
        output.reserve(nonce.size() + ciphertext.size() + tag.size());
        output.insert(output.end(), nonce.begin(), nonce.end());
        output.insert(output.end(), ciphertext.begin(), ciphertext.end());
        output.insert(output.end(), tag.begin(), tag.end());
        return output;
    }
#else
    (void)input;
    return ErrorCode::FEATURE_UNAVAILABLE;
#endif
}

Result<std::vector<uint8_t>> ChaCha20Poly1305Transformer::inverse(std::span<const uint8_t> input) {
#if IPB_HAS_OPENSSL
    if (!verify_key()) {
        return ErrorCode::KEY_INVALID;
    }

    if (input.empty()) {
        return std::vector<uint8_t>{};
    }

    std::vector<uint8_t> nonce_storage;  // Storage to keep nonce alive
    std::span<const uint8_t> nonce;
    std::span<const uint8_t> ciphertext;
    std::span<const uint8_t> tag;

    if (include_header_) {
        auto parsed = parse_encrypted(input);
        if (parsed.is_error()) {
            return parsed.error();
        }
        // Copy nonce to local storage to avoid dangling span
        nonce_storage = std::move(parsed.value().header.nonce);
        nonce = nonce_storage;
        ciphertext = parsed.value().ciphertext;
        tag = parsed.value().tag;
    } else {
        if (input.size() < NonceSize::CHACHA + TagSize::POLY1305) {
            return ErrorCode::TRUNCATED_DATA;
        }
        nonce = input.subspan(0, NonceSize::CHACHA);
        tag = input.subspan(input.size() - TagSize::POLY1305, TagSize::POLY1305);
        ciphertext = input.subspan(NonceSize::CHACHA,
                                   input.size() - NonceSize::CHACHA - TagSize::POLY1305);
    }

    const EVP_CIPHER* cipher = EVP_chacha20_poly1305();

    CipherContext ctx;
    if (!ctx.valid()) {
        return ErrorCode::DECRYPTION_FAILED;
    }

    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, cipher, nullptr, key_.data(), nonce.data()) != 1) {
        return ErrorCode::DECRYPTION_FAILED;
    }

    // Allocate output buffer
    std::vector<uint8_t> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int plaintext_len = 0;

    // Decrypt
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                          ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
        return ErrorCode::DECRYPTION_FAILED;
    }
    plaintext_len = len;

    // Set expected tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG,
                            static_cast<int>(tag.size()),
                            const_cast<uint8_t*>(tag.data())) != 1) {
        return ErrorCode::DECRYPTION_FAILED;
    }

    // Finalize and verify tag
    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    if (ret <= 0) {
        return ErrorCode::SIGNATURE_INVALID;
    }
    plaintext_len += len;
    plaintext.resize(static_cast<size_t>(plaintext_len));

    return plaintext;
#else
    (void)input;
    return ErrorCode::FEATURE_UNAVAILABLE;
#endif
}

}  // namespace ipb::transform
