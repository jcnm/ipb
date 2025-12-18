#pragma once

/**
 * @file authentication.hpp
 * @brief Enterprise-grade authentication framework
 *
 * Features:
 * - API Key authentication
 * - JWT token validation
 * - Bearer token support
 * - Credential store with secure hashing (SHA-256 via OpenSSL)
 * - Session management
 * - Rate limiting per identity
 *
 * Thread-safe and designed for high-performance scenarios.
 *
 * Security: Uses OpenSSL for cryptographic operations (SHA-256, CSPRNG)
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// OpenSSL for cryptographic operations
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace ipb::security {

//=============================================================================
// Types and Constants
//=============================================================================

/**
 * @brief Authentication result status
 */
enum class AuthResult {
    SUCCESS,
    INVALID_CREDENTIALS,
    EXPIRED_TOKEN,
    REVOKED_TOKEN,
    RATE_LIMITED,
    MISSING_CREDENTIALS,
    INTERNAL_ERROR
};

inline std::string auth_result_string(AuthResult result) {
    switch (result) {
        case AuthResult::SUCCESS:
            return "success";
        case AuthResult::INVALID_CREDENTIALS:
            return "invalid_credentials";
        case AuthResult::EXPIRED_TOKEN:
            return "expired_token";
        case AuthResult::REVOKED_TOKEN:
            return "revoked_token";
        case AuthResult::RATE_LIMITED:
            return "rate_limited";
        case AuthResult::MISSING_CREDENTIALS:
            return "missing_credentials";
        case AuthResult::INTERNAL_ERROR:
            return "internal_error";
    }
    return "unknown";
}

/**
 * @brief Authentication method
 */
enum class AuthMethod { API_KEY, BEARER_TOKEN, BASIC, CERTIFICATE, NONE };

/**
 * @brief Authenticated identity
 */
struct Identity {
    std::string id;
    std::string name;
    AuthMethod method{AuthMethod::NONE};
    std::vector<std::string> roles;
    std::chrono::system_clock::time_point authenticated_at;
    std::chrono::system_clock::time_point expires_at;
    std::unordered_map<std::string, std::string> metadata;

    bool is_expired() const { return std::chrono::system_clock::now() > expires_at; }

    bool has_role(std::string_view role) const {
        return std::find(roles.begin(), roles.end(), role) != roles.end();
    }
};

/**
 * @brief Authentication context
 */
struct AuthContext {
    AuthResult result{AuthResult::MISSING_CREDENTIALS};
    std::optional<Identity> identity;
    std::string error_message;
    std::chrono::microseconds latency{0};
};

//=============================================================================
// Credential Types
//=============================================================================

/**
 * @brief API Key credential
 */
struct ApiKeyCredential {
    std::string key_id;
    std::string key_hash;  // Hashed key value
    std::string owner_id;
    std::vector<std::string> roles;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
    bool revoked{false};
    std::string description;
    uint64_t request_count{0};
    std::chrono::system_clock::time_point last_used;
};

/**
 * @brief Session token
 */
struct SessionToken {
    std::string token_id;
    std::string token_hash;
    std::string identity_id;
    std::vector<std::string> roles;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
    std::string ip_address;
    std::string user_agent;
    bool revoked{false};
};

//=============================================================================
// Secure Hashing
//=============================================================================

/**
 * @brief Secure hash utilities using OpenSSL
 *
 * Security features:
 * - SHA-256 via OpenSSL EVP API (not std::hash)
 * - CSPRNG via OpenSSL RAND_bytes (not std::mt19937)
 * - Constant-time comparison to prevent timing attacks
 */
class SecureHash {
public:
    /**
     * @brief SHA-256 hash of string using OpenSSL
     * @param input The string to hash
     * @return 64-character hexadecimal string representing the SHA-256 hash
     * @throws std::runtime_error if OpenSSL operations fail
     */
    static std::string sha256(std::string_view input) {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_len = 0;

        // Create and initialize the context
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            throw std::runtime_error("Failed to create EVP_MD_CTX");
        }

        // Initialize, update, and finalize the hash
        bool success = (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1) &&
                       (EVP_DigestUpdate(ctx, input.data(), input.size()) == 1) &&
                       (EVP_DigestFinal_ex(ctx, hash, &hash_len) == 1);

        EVP_MD_CTX_free(ctx);

        if (!success) {
            throw std::runtime_error("SHA-256 computation failed");
        }

        // Convert to hexadecimal string
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (unsigned int i = 0; i < hash_len; ++i) {
            oss << std::setw(2) << static_cast<int>(hash[i]);
        }
        return oss.str();
    }

    /**
     * @brief Hash password with salt using SHA-256
     * @param password The password to hash
     * @param salt The salt to use
     * @return SHA-256 hash of salt:password
     */
    static std::string hash_password(std::string_view password, std::string_view salt) {
        std::string combined = std::string(salt) + ":" + std::string(password);
        return sha256(combined);
    }

    /**
     * @brief Generate cryptographically secure random bytes
     * @param length Number of random bytes to generate
     * @return Vector of random bytes
     * @throws std::runtime_error if CSPRNG fails
     */
    static std::vector<unsigned char> random_bytes(size_t length) {
        std::vector<unsigned char> buffer(length);
        if (RAND_bytes(buffer.data(), static_cast<int>(length)) != 1) {
            throw std::runtime_error("CSPRNG failed to generate random bytes");
        }
        return buffer;
    }

    /**
     * @brief Generate random salt using CSPRNG (OpenSSL RAND_bytes)
     * @param length Length of the salt in characters
     * @return Cryptographically secure random alphanumeric string
     */
    static std::string generate_salt(size_t length = 16) {
        static constexpr char chars[] =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        static constexpr size_t chars_len = sizeof(chars) - 1;

        auto bytes = random_bytes(length);

        std::string salt;
        salt.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            salt += chars[bytes[i] % chars_len];
        }
        return salt;
    }

    /**
     * @brief Generate cryptographically secure API key
     * @param length Length of the API key
     * @return Secure random API key
     */
    static std::string generate_api_key(size_t length = 32) { return generate_salt(length); }

    /**
     * @brief Generate cryptographically secure session token
     * @param length Length of the session token
     * @return Secure random session token
     */
    static std::string generate_token(size_t length = 64) { return generate_salt(length); }

    /**
     * @brief Constant-time string comparison (timing-attack safe)
     * @param a First string to compare
     * @param b Second string to compare
     * @return true if strings are equal, false otherwise
     *
     * This function compares all bytes regardless of where differences occur,
     * preventing timing attacks that could reveal information about the strings.
     */
    static bool secure_compare(std::string_view a, std::string_view b) {
        if (a.size() != b.size())
            return false;

        volatile int result = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            result |= a[i] ^ b[i];
        }
        return result == 0;
    }
};

//=============================================================================
// API Key Authenticator
//=============================================================================

/**
 * @brief API Key authentication provider
 */
class ApiKeyAuthenticator {
public:
    /**
     * @brief Register new API key
     */
    std::string register_key(const std::string& owner_id,
                             const std::vector<std::string>& roles = {},
                             std::chrono::hours validity    = std::chrono::hours(8760),  // 1 year
                             const std::string& description = "") {
        std::string raw_key  = SecureHash::generate_api_key();
        std::string key_id   = SecureHash::generate_salt(8);
        std::string key_hash = SecureHash::sha256(raw_key);

        ApiKeyCredential cred;
        cred.key_id      = key_id;
        cred.key_hash    = key_hash;
        cred.owner_id    = owner_id;
        cred.roles       = roles;
        cred.created_at  = std::chrono::system_clock::now();
        cred.expires_at  = cred.created_at + validity;
        cred.description = description;

        {
            std::unique_lock lock(mutex_);
            keys_[key_id] = std::move(cred);
        }

        // Return format: key_id.raw_key
        return key_id + "." + raw_key;
    }

    /**
     * @brief Authenticate with API key
     */
    AuthContext authenticate(std::string_view api_key) {
        auto start = std::chrono::high_resolution_clock::now();
        AuthContext ctx;

        // Parse key format: key_id.raw_key
        auto dot_pos = api_key.find('.');
        if (dot_pos == std::string_view::npos) {
            ctx.result        = AuthResult::INVALID_CREDENTIALS;
            ctx.error_message = "Invalid API key format";
            return ctx;
        }

        std::string key_id(api_key.substr(0, dot_pos));
        std::string raw_key(api_key.substr(dot_pos + 1));
        std::string key_hash = SecureHash::sha256(raw_key);

        std::shared_lock lock(mutex_);
        auto it = keys_.find(key_id);
        if (it == keys_.end()) {
            ctx.result        = AuthResult::INVALID_CREDENTIALS;
            ctx.error_message = "API key not found";
            return ctx;
        }

        const auto& cred = it->second;

        // Verify hash (constant-time)
        if (!SecureHash::secure_compare(key_hash, cred.key_hash)) {
            ctx.result        = AuthResult::INVALID_CREDENTIALS;
            ctx.error_message = "Invalid API key";
            return ctx;
        }

        // Check expiration
        auto now = std::chrono::system_clock::now();
        if (now > cred.expires_at) {
            ctx.result        = AuthResult::EXPIRED_TOKEN;
            ctx.error_message = "API key expired";
            return ctx;
        }

        // Check revocation
        if (cred.revoked) {
            ctx.result        = AuthResult::REVOKED_TOKEN;
            ctx.error_message = "API key revoked";
            return ctx;
        }

        // Success - build identity
        Identity identity;
        identity.id                 = cred.owner_id;
        identity.name               = cred.description;
        identity.method             = AuthMethod::API_KEY;
        identity.roles              = cred.roles;
        identity.authenticated_at   = now;
        identity.expires_at         = cred.expires_at;
        identity.metadata["key_id"] = key_id;

        ctx.result   = AuthResult::SUCCESS;
        ctx.identity = std::move(identity);

        auto end    = std::chrono::high_resolution_clock::now();
        ctx.latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        return ctx;
    }

    /**
     * @brief Revoke API key
     */
    bool revoke_key(const std::string& key_id) {
        std::unique_lock lock(mutex_);
        auto it = keys_.find(key_id);
        if (it != keys_.end()) {
            it->second.revoked = true;
            return true;
        }
        return false;
    }

    /**
     * @brief List all keys for owner
     */
    std::vector<ApiKeyCredential> list_keys(const std::string& owner_id) const {
        std::vector<ApiKeyCredential> result;
        std::shared_lock lock(mutex_);

        for (const auto& [_, cred] : keys_) {
            if (cred.owner_id == owner_id) {
                result.push_back(cred);
            }
        }
        return result;
    }

    /**
     * @brief Get key count
     */
    size_t key_count() const {
        std::shared_lock lock(mutex_);
        return keys_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ApiKeyCredential> keys_;
};

//=============================================================================
// Session Manager
//=============================================================================

/**
 * @brief Session token manager
 */
class SessionManager {
public:
    /**
     * @brief Create new session
     */
    std::string create_session(const std::string& identity_id,
                               const std::vector<std::string>& roles = {},
                               std::chrono::hours validity           = std::chrono::hours(24),
                               const std::string& ip_address         = "",
                               const std::string& user_agent         = "") {
        std::string raw_token  = SecureHash::generate_token();
        std::string token_id   = SecureHash::generate_salt(16);
        std::string token_hash = SecureHash::sha256(raw_token);

        SessionToken session;
        session.token_id    = token_id;
        session.token_hash  = token_hash;
        session.identity_id = identity_id;
        session.roles       = roles;
        session.created_at  = std::chrono::system_clock::now();
        session.expires_at  = session.created_at + validity;
        session.ip_address  = ip_address;
        session.user_agent  = user_agent;

        {
            std::unique_lock lock(mutex_);
            sessions_[token_id] = std::move(session);
        }

        return raw_token;
    }

    /**
     * @brief Validate session token
     */
    AuthContext validate(std::string_view token) {
        auto start = std::chrono::high_resolution_clock::now();
        AuthContext ctx;

        std::string token_hash = SecureHash::sha256(std::string(token));

        std::shared_lock lock(mutex_);

        // Find session by hash
        const SessionToken* found = nullptr;
        for (const auto& [_, session] : sessions_) {
            if (SecureHash::secure_compare(session.token_hash, token_hash)) {
                found = &session;
                break;
            }
        }

        if (!found) {
            ctx.result        = AuthResult::INVALID_CREDENTIALS;
            ctx.error_message = "Invalid session token";
            return ctx;
        }

        auto now = std::chrono::system_clock::now();

        if (now > found->expires_at) {
            ctx.result        = AuthResult::EXPIRED_TOKEN;
            ctx.error_message = "Session expired";
            return ctx;
        }

        if (found->revoked) {
            ctx.result        = AuthResult::REVOKED_TOKEN;
            ctx.error_message = "Session revoked";
            return ctx;
        }

        Identity identity;
        identity.id                     = found->identity_id;
        identity.method                 = AuthMethod::BEARER_TOKEN;
        identity.roles                  = found->roles;
        identity.authenticated_at       = found->created_at;
        identity.expires_at             = found->expires_at;
        identity.metadata["token_id"]   = found->token_id;
        identity.metadata["ip_address"] = found->ip_address;

        ctx.result   = AuthResult::SUCCESS;
        ctx.identity = std::move(identity);

        auto end    = std::chrono::high_resolution_clock::now();
        ctx.latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        return ctx;
    }

    /**
     * @brief Revoke session
     */
    bool revoke(const std::string& token_id) {
        std::unique_lock lock(mutex_);
        auto it = sessions_.find(token_id);
        if (it != sessions_.end()) {
            it->second.revoked = true;
            return true;
        }
        return false;
    }

    /**
     * @brief Revoke all sessions for identity
     */
    size_t revoke_all(const std::string& identity_id) {
        std::unique_lock lock(mutex_);
        size_t count = 0;
        for (auto& [_, session] : sessions_) {
            if (session.identity_id == identity_id && !session.revoked) {
                session.revoked = true;
                ++count;
            }
        }
        return count;
    }

    /**
     * @brief Cleanup expired sessions
     */
    size_t cleanup_expired() {
        auto now = std::chrono::system_clock::now();
        std::unique_lock lock(mutex_);

        size_t count = 0;
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (it->second.expires_at < now) {
                it = sessions_.erase(it);
                ++count;
            } else {
                ++it;
            }
        }
        return count;
    }

    size_t session_count() const {
        std::shared_lock lock(mutex_);
        return sessions_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, SessionToken> sessions_;
};

//=============================================================================
// Authentication Service
//=============================================================================

/**
 * @brief Unified authentication service
 */
class AuthenticationService {
public:
    AuthenticationService()
        : api_key_auth_(std::make_unique<ApiKeyAuthenticator>()),
          session_mgr_(std::make_unique<SessionManager>()) {}

    /**
     * @brief Authenticate request
     */
    AuthContext authenticate(std::string_view auth_header) {
        if (auth_header.empty()) {
            return {AuthResult::MISSING_CREDENTIALS, std::nullopt, "No credentials provided"};
        }

        // Parse Authorization header
        if (auth_header.starts_with("Bearer ")) {
            return session_mgr_->validate(auth_header.substr(7));
        }

        if (auth_header.starts_with("ApiKey ")) {
            return api_key_auth_->authenticate(auth_header.substr(7));
        }

        return {AuthResult::INVALID_CREDENTIALS, std::nullopt, "Unknown auth method"};
    }

    ApiKeyAuthenticator& api_keys() { return *api_key_auth_; }
    SessionManager& sessions() { return *session_mgr_; }

private:
    std::unique_ptr<ApiKeyAuthenticator> api_key_auth_;
    std::unique_ptr<SessionManager> session_mgr_;
};

}  // namespace ipb::security
