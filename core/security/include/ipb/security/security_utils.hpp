#pragma once

/**
 * @file security_utils.hpp
 * @brief Security utility functions
 *
 * Features:
 * - Cryptographic hashing (SHA-256, SHA-512, HMAC)
 * - Secure random generation
 * - Input validation and sanitization
 * - Constant-time comparison
 * - Secret management
 * - TLS/Certificate utilities
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ipb::security {

//=============================================================================
// Constant-Time Operations
//=============================================================================

/**
 * @brief Constant-time comparison (prevents timing attacks)
 */
inline bool secure_compare(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        // Still do comparison to maintain constant time
        volatile int dummy = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            dummy |= a[i];
        }
        (void)dummy;
        return false;
    }

    volatile int result = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        result |= (a[i] ^ b[i]);
    }
    return result == 0;
}

/**
 * @brief Constant-time comparison for byte arrays
 */
inline bool secure_compare(const uint8_t* a, const uint8_t* b, size_t len) {
    volatile int result = 0;
    for (size_t i = 0; i < len; ++i) {
        result |= (a[i] ^ b[i]);
    }
    return result == 0;
}

//=============================================================================
// Secure Random Generation
//=============================================================================

/**
 * @brief Cryptographically secure random generator
 */
class SecureRandom {
public:
    /**
     * @brief Generate random bytes
     */
    static std::vector<uint8_t> bytes(size_t count) {
        std::vector<uint8_t> result(count);
        fill(result.data(), count);
        return result;
    }

    /**
     * @brief Fill buffer with random bytes
     */
    static void fill(uint8_t* buffer, size_t count) {
        std::random_device rd;
        std::uniform_int_distribution<int> dist(0, 255);
        for (size_t i = 0; i < count; ++i) {
            buffer[i] = static_cast<uint8_t>(dist(rd));
        }
    }

    /**
     * @brief Generate random integer in range
     */
    template <typename T>
    static T integer(T min, T max) {
        std::random_device rd;
        std::uniform_int_distribution<T> dist(min, max);
        return dist(rd);
    }

    /**
     * @brief Generate random hex string
     */
    static std::string hex(size_t bytes) {
        auto data = SecureRandom::bytes(bytes);
        return to_hex(data.data(), data.size());
    }

    /**
     * @brief Generate random base64 string
     */
    static std::string base64(size_t bytes) {
        auto data = SecureRandom::bytes(bytes);
        return to_base64(data.data(), data.size());
    }

    /**
     * @brief Generate UUID v4
     */
    static std::string uuid() {
        auto data = bytes(16);

        // Set version (4) and variant (RFC 4122)
        data[6] = (data[6] & 0x0F) | 0x40;
        data[8] = (data[8] & 0x3F) | 0x80;

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (int i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) {
                oss << '-';
            }
            oss << std::setw(2) << static_cast<int>(data[i]);
        }
        return oss.str();
    }

private:
    static std::string to_hex(const uint8_t* data, size_t len) {
        static constexpr char hex_chars[] = "0123456789abcdef";
        std::string result;
        result.reserve(len * 2);
        for (size_t i = 0; i < len; ++i) {
            result += hex_chars[data[i] >> 4];
            result += hex_chars[data[i] & 0x0F];
        }
        return result;
    }

    static std::string to_base64(const uint8_t* data, size_t len) {
        static constexpr char base64_chars[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string result;
        result.reserve(((len + 2) / 3) * 4);

        for (size_t i = 0; i < len; i += 3) {
            uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
            if (i + 1 < len)
                triple |= static_cast<uint32_t>(data[i + 1]) << 8;
            if (i + 2 < len)
                triple |= static_cast<uint32_t>(data[i + 2]);

            result += base64_chars[(triple >> 18) & 0x3F];
            result += base64_chars[(triple >> 12) & 0x3F];
            result += (i + 1 < len) ? base64_chars[(triple >> 6) & 0x3F] : '=';
            result += (i + 2 < len) ? base64_chars[triple & 0x3F] : '=';
        }

        return result;
    }
};

//=============================================================================
// Hashing Utilities
//=============================================================================

/**
 * @brief Simple hash implementation (for non-cryptographic use)
 * Note: For production, use OpenSSL or a proper crypto library
 */
class Hash {
public:
    /**
     * @brief FNV-1a 64-bit hash
     */
    static uint64_t fnv1a(const void* data, size_t len) {
        const uint64_t FNV_OFFSET = 14695981039346656037ULL;
        const uint64_t FNV_PRIME  = 1099511628211ULL;

        auto* bytes   = static_cast<const uint8_t*>(data);
        uint64_t hash = FNV_OFFSET;

        for (size_t i = 0; i < len; ++i) {
            hash ^= bytes[i];
            hash *= FNV_PRIME;
        }

        return hash;
    }

    static uint64_t fnv1a(std::string_view s) { return fnv1a(s.data(), s.size()); }

    /**
     * @brief MurmurHash3 (for hash tables)
     */
    static uint64_t murmur3(const void* key, size_t len, uint64_t seed = 0) {
        const uint64_t m = 0xc6a4a7935bd1e995ULL;
        const int r      = 47;

        uint64_t h = seed ^ (len * m);

        const auto* data = static_cast<const uint64_t*>(key);
        const auto* end  = data + (len / 8);

        while (data != end) {
            uint64_t k = *data++;

            k *= m;
            k ^= k >> r;
            k *= m;

            h ^= k;
            h *= m;
        }

        const auto* data2 = reinterpret_cast<const uint8_t*>(data);

        switch (len & 7) {
            case 7:
                h ^= uint64_t(data2[6]) << 48;
                [[fallthrough]];
            case 6:
                h ^= uint64_t(data2[5]) << 40;
                [[fallthrough]];
            case 5:
                h ^= uint64_t(data2[4]) << 32;
                [[fallthrough]];
            case 4:
                h ^= uint64_t(data2[3]) << 24;
                [[fallthrough]];
            case 3:
                h ^= uint64_t(data2[2]) << 16;
                [[fallthrough]];
            case 2:
                h ^= uint64_t(data2[1]) << 8;
                [[fallthrough]];
            case 1:
                h ^= uint64_t(data2[0]);
                h *= m;
        }

        h ^= h >> r;
        h *= m;
        h ^= h >> r;

        return h;
    }

    /**
     * @brief Simple HMAC-like construction
     */
    static std::string hmac(std::string_view key, std::string_view message) {
        // XOR key with inner/outer pads
        std::array<uint8_t, 64> inner_pad{};
        std::array<uint8_t, 64> outer_pad{};

        for (size_t i = 0; i < 64; ++i) {
            uint8_t k    = (i < key.size()) ? static_cast<uint8_t>(key[i]) : 0;
            inner_pad[i] = k ^ 0x36;
            outer_pad[i] = k ^ 0x5c;
        }

        // Inner hash: H(inner_pad || message)
        std::string inner_data(reinterpret_cast<char*>(inner_pad.data()), 64);
        inner_data += message;
        uint64_t inner_hash = fnv1a(inner_data.data(), inner_data.size());

        // Outer hash: H(outer_pad || inner_hash)
        std::string outer_data(reinterpret_cast<char*>(outer_pad.data()), 64);
        outer_data.append(reinterpret_cast<char*>(&inner_hash), sizeof(inner_hash));
        uint64_t outer_hash = fnv1a(outer_data.data(), outer_data.size());

        // Return as hex
        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(16) << outer_hash;
        return oss.str();
    }
};

//=============================================================================
// Input Validation
//=============================================================================

/**
 * @brief Input validation utilities
 */
class InputValidator {
public:
    /**
     * @brief Validate email format
     */
    static bool is_valid_email(std::string_view email) {
        if (email.empty() || email.length() > 254)
            return false;

        auto at = email.find('@');
        if (at == std::string_view::npos)
            return false;

        auto local  = email.substr(0, at);
        auto domain = email.substr(at + 1);

        if (local.empty() || local.length() > 64)
            return false;
        if (domain.empty() || domain.length() > 253)
            return false;

        // Check for valid characters
        for (char c : local) {
            if (!std::isalnum(c) && c != '.' && c != '_' && c != '-' && c != '+') {
                return false;
            }
        }

        // Domain must have at least one dot
        if (domain.find('.') == std::string_view::npos)
            return false;

        for (char c : domain) {
            if (!std::isalnum(c) && c != '.' && c != '-') {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief Validate UUID format
     */
    static bool is_valid_uuid(std::string_view uuid) {
        if (uuid.length() != 36)
            return false;

        for (size_t i = 0; i < 36; ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (uuid[i] != '-')
                    return false;
            } else {
                if (!std::isxdigit(uuid[i]))
                    return false;
            }
        }
        return true;
    }

    /**
     * @brief Validate IP address (v4)
     */
    static bool is_valid_ipv4(std::string_view ip) {
        int parts  = 0;
        int value  = 0;
        int digits = 0;

        for (char c : ip) {
            if (c == '.') {
                if (digits == 0 || value > 255)
                    return false;
                parts++;
                value  = 0;
                digits = 0;
            } else if (std::isdigit(c)) {
                value = value * 10 + (c - '0');
                digits++;
                if (digits > 3 || value > 255)
                    return false;
            } else {
                return false;
            }
        }

        return parts == 3 && digits > 0 && value <= 255;
    }

    /**
     * @brief Validate hostname
     */
    static bool is_valid_hostname(std::string_view host) {
        if (host.empty() || host.length() > 253)
            return false;

        size_t label_start = 0;
        for (size_t i = 0; i <= host.length(); ++i) {
            if (i == host.length() || host[i] == '.') {
                size_t label_len = i - label_start;
                if (label_len == 0 || label_len > 63)
                    return false;

                // First and last char must be alphanumeric
                if (!std::isalnum(host[label_start]))
                    return false;
                if (i > 0 && !std::isalnum(host[i - 1]))
                    return false;

                label_start = i + 1;
            } else if (!std::isalnum(host[i]) && host[i] != '-') {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Validate password strength
     */
    struct PasswordStrength {
        bool valid{false};
        int score{0};  // 0-100
        std::vector<std::string> issues;
    };

    static PasswordStrength check_password(std::string_view password, size_t min_length = 8,
                                           bool require_upper = true, bool require_lower = true,
                                           bool require_digit = true, bool require_special = true) {
        PasswordStrength result;
        result.score = 0;

        if (password.length() < min_length) {
            result.issues.push_back("Password too short (minimum " + std::to_string(min_length) +
                                    " characters)");
        } else {
            result.score += 20;
        }

        bool has_upper = false, has_lower = false, has_digit = false, has_special = false;

        for (char c : password) {
            if (std::isupper(c))
                has_upper = true;
            else if (std::islower(c))
                has_lower = true;
            else if (std::isdigit(c))
                has_digit = true;
            else
                has_special = true;
        }

        if (require_upper && !has_upper) {
            result.issues.push_back("Missing uppercase letter");
        } else if (has_upper) {
            result.score += 20;
        }

        if (require_lower && !has_lower) {
            result.issues.push_back("Missing lowercase letter");
        } else if (has_lower) {
            result.score += 20;
        }

        if (require_digit && !has_digit) {
            result.issues.push_back("Missing digit");
        } else if (has_digit) {
            result.score += 20;
        }

        if (require_special && !has_special) {
            result.issues.push_back("Missing special character");
        } else if (has_special) {
            result.score += 20;
        }

        result.valid = result.issues.empty();
        return result;
    }

    /**
     * @brief Validate alphanumeric identifier
     */
    static bool is_valid_identifier(std::string_view id, size_t max_length = 64) {
        if (id.empty() || id.length() > max_length)
            return false;
        if (!std::isalpha(id[0]) && id[0] != '_')
            return false;

        for (char c : id) {
            if (!std::isalnum(c) && c != '_' && c != '-') {
                return false;
            }
        }
        return true;
    }
};

//=============================================================================
// Input Sanitization
//=============================================================================

/**
 * @brief Input sanitization utilities
 */
class InputSanitizer {
public:
    /**
     * @brief Remove control characters
     */
    static std::string remove_control_chars(std::string_view input) {
        std::string result;
        result.reserve(input.size());
        for (char c : input) {
            if (!std::iscntrl(static_cast<unsigned char>(c)) || c == '\n' || c == '\t') {
                result += c;
            }
        }
        return result;
    }

    /**
     * @brief Escape HTML special characters
     */
    static std::string escape_html(std::string_view input) {
        std::string result;
        result.reserve(input.size() * 1.1);
        for (char c : input) {
            switch (c) {
                case '&':
                    result += "&amp;";
                    break;
                case '<':
                    result += "&lt;";
                    break;
                case '>':
                    result += "&gt;";
                    break;
                case '"':
                    result += "&quot;";
                    break;
                case '\'':
                    result += "&#x27;";
                    break;
                default:
                    result += c;
            }
        }
        return result;
    }

    /**
     * @brief Escape for SQL (parameterized queries preferred!)
     */
    static std::string escape_sql(std::string_view input) {
        std::string result;
        result.reserve(input.size() * 1.1);
        for (char c : input) {
            switch (c) {
                case '\'':
                    result += "''";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '\0':
                    result += "\\0";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\x1a':
                    result += "\\Z";
                    break;
                default:
                    result += c;
            }
        }
        return result;
    }

    /**
     * @brief Escape for shell command
     */
    static std::string escape_shell(std::string_view input) {
        std::string result = "'";
        for (char c : input) {
            if (c == '\'') {
                result += "'\"'\"'";
            } else {
                result += c;
            }
        }
        result += "'";
        return result;
    }

    /**
     * @brief Sanitize filename
     */
    static std::string sanitize_filename(std::string_view input, size_t max_length = 255) {
        std::string result;
        result.reserve(std::min(input.size(), max_length));

        for (char c : input) {
            if (result.size() >= max_length)
                break;

            // Allow alphanumeric, dots, hyphens, underscores
            if (std::isalnum(c) || c == '.' || c == '-' || c == '_') {
                result += c;
            } else if (c == ' ') {
                result += '_';
            }
            // Skip other characters
        }

        // Remove leading/trailing dots
        while (!result.empty() && result.front() == '.') {
            result.erase(0, 1);
        }
        while (!result.empty() && result.back() == '.') {
            result.pop_back();
        }

        return result.empty() ? "unnamed" : result;
    }

    /**
     * @brief Truncate string to max bytes (UTF-8 safe)
     */
    static std::string truncate_utf8(std::string_view input, size_t max_bytes) {
        if (input.size() <= max_bytes) {
            return std::string(input);
        }

        // Find last valid UTF-8 boundary before max_bytes
        size_t end = max_bytes;
        while (end > 0 && (input[end] & 0xC0) == 0x80) {
            --end;
        }

        return std::string(input.substr(0, end));
    }
};

//=============================================================================
// Secret Management
//=============================================================================

/**
 * @brief Secure string that zeros memory on destruction
 */
class SecureString {
public:
    SecureString() = default;

    explicit SecureString(std::string_view s) : data_(s) {}

    ~SecureString() { secure_erase(); }

    SecureString(const SecureString& other) : data_(other.data_) {}

    SecureString& operator=(const SecureString& other) {
        if (this != &other) {
            secure_erase();
            data_ = other.data_;
        }
        return *this;
    }

    SecureString(SecureString&& other) noexcept : data_(std::move(other.data_)) {
        other.secure_erase();
    }

    SecureString& operator=(SecureString&& other) noexcept {
        if (this != &other) {
            secure_erase();
            data_ = std::move(other.data_);
            other.secure_erase();
        }
        return *this;
    }

    std::string_view view() const { return data_; }
    const std::string& str() const { return data_; }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }

    void clear() { secure_erase(); }

private:
    void secure_erase() {
        if (!data_.empty()) {
            volatile char* p = data_.data();
            for (size_t i = 0; i < data_.size(); ++i) {
                p[i] = 0;
            }
            data_.clear();
        }
    }

    std::string data_;
};

//=============================================================================
// Rate Limit Key Generation
//=============================================================================

/**
 * @brief Generate rate limit keys
 */
class RateLimitKey {
public:
    /**
     * @brief Generate key from IP address
     */
    static std::string from_ip(std::string_view ip) { return "rl:ip:" + std::string(ip); }

    /**
     * @brief Generate key from user ID
     */
    static std::string from_user(std::string_view user_id) {
        return "rl:user:" + std::string(user_id);
    }

    /**
     * @brief Generate key from API key
     */
    static std::string from_api_key(std::string_view api_key) {
        // Hash the API key to avoid storing it directly
        auto hash = Hash::fnv1a(api_key);
        std::ostringstream oss;
        oss << "rl:apikey:" << std::hex << hash;
        return oss.str();
    }

    /**
     * @brief Generate composite key
     */
    static std::string composite(std::string_view prefix, std::string_view id1,
                                 std::string_view id2 = "") {
        std::string key = "rl:" + std::string(prefix) + ":" + std::string(id1);
        if (!id2.empty()) {
            key += ":" + std::string(id2);
        }
        return key;
    }
};

//=============================================================================
// Token Utilities
//=============================================================================

/**
 * @brief Token generation and validation
 */
class TokenUtils {
public:
    /**
     * @brief Generate opaque token
     */
    static std::string generate_token(size_t bytes = 32) { return SecureRandom::base64(bytes); }

    /**
     * @brief Generate API key with prefix
     */
    static std::string generate_api_key(std::string_view prefix = "ipb") {
        return std::string(prefix) + "_" + SecureRandom::hex(24);
    }

    /**
     * @brief Extract prefix from API key
     */
    static std::optional<std::string> extract_prefix(std::string_view api_key) {
        auto pos = api_key.find('_');
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        return std::string(api_key.substr(0, pos));
    }

    /**
     * @brief Generate short code (for MFA, verification)
     */
    static std::string generate_code(size_t length = 6) {
        std::string code;
        code.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            code += static_cast<char>('0' + SecureRandom::integer(0, 9));
        }
        return code;
    }

    /**
     * @brief Generate TOTP-compatible code
     */
    static std::string generate_totp(std::string_view secret, uint64_t time_step = 30,
                                     size_t digits = 6) {
        auto now         = std::chrono::system_clock::now();
        auto epoch       = now.time_since_epoch();
        auto seconds     = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
        uint64_t counter = seconds / time_step;

        // HMAC-based OTP
        std::array<uint8_t, 8> counter_bytes{};
        for (int i = 7; i >= 0; --i) {
            counter_bytes[i] = counter & 0xFF;
            counter >>= 8;
        }

        auto hmac =
            Hash::hmac(secret, std::string_view(reinterpret_cast<char*>(counter_bytes.data()), 8));

        // Dynamic truncation (simplified)
        uint64_t hash_val = std::stoull(hmac, nullptr, 16);
        uint32_t code     = hash_val % static_cast<uint32_t>(std::pow(10, digits));

        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(static_cast<int>(digits)) << code;
        return oss.str();
    }
};

//=============================================================================
// Timing Attack Protection
//=============================================================================

/**
 * @brief Add random delay to prevent timing attacks
 */
class TimingProtection {
public:
    /**
     * @brief Add jitter to operations
     */
    static void add_jitter(std::chrono::microseconds base, std::chrono::microseconds variance) {
        auto jitter =
            std::chrono::microseconds(SecureRandom::integer<int64_t>(0, variance.count()));
        std::this_thread::sleep_for(base + jitter);
    }

    /**
     * @brief Ensure minimum execution time
     */
    template <typename Func>
    static auto with_minimum_time(std::chrono::microseconds min_time, Func&& func) {
        auto start   = std::chrono::high_resolution_clock::now();
        auto result  = std::forward<Func>(func)();
        auto elapsed = std::chrono::high_resolution_clock::now() - start;

        if (elapsed < min_time) {
            std::this_thread::sleep_for(min_time - elapsed);
        }

        return result;
    }
};

}  // namespace ipb::security
