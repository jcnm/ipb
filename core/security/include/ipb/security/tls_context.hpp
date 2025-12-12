#pragma once

/**
 * @file tls_context.hpp
 * @brief Abstract TLS/SSL context interface supporting multiple backends
 *
 * This header provides a unified TLS interface that abstracts the underlying
 * SSL library (OpenSSL, mbedTLS, wolfSSL). The implementation is selected
 * at compile time based on the IPB_SSL_BACKEND configuration.
 *
 * Features:
 * - Certificate and key management
 * - Server and client context creation
 * - Cipher suite configuration
 * - TLS version control
 * - Certificate verification
 */

#include <ipb/common/platform.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <chrono>
#include <cstdint>
#include <optional>

namespace ipb::security {

// ============================================================================
// SIMPLE RESULT TYPE FOR SECURITY OPERATIONS
// ============================================================================

/**
 * @brief Security error codes
 */
enum class SecurityError : uint32_t {
    SUCCESS = 0,
    INITIALIZATION_FAILED,
    CERTIFICATE_INVALID,
    CERTIFICATE_EXPIRED,
    KEY_INVALID,
    HANDSHAKE_FAILED,
    VERIFICATION_FAILED,
    CRYPTO_ERROR,
    FILE_NOT_FOUND,
    SOCKET_ERROR,
    MEMORY_ALLOCATION_FAILED,
    CONFIG_INVALID,
    NOT_SUPPORTED,
    INTERNAL_ERROR
};

/**
 * @brief Result type for security operations
 */
template<typename T>
class SecurityResult {
public:
    SecurityResult(T value) : value_(std::move(value)), error_(SecurityError::SUCCESS) {}
    SecurityResult(SecurityError error, std::string msg = {})
        : error_(error), error_message_(std::move(msg)) {}

    bool is_success() const noexcept { return error_ == SecurityError::SUCCESS; }
    bool is_error() const noexcept { return error_ != SecurityError::SUCCESS; }
    explicit operator bool() const noexcept { return is_success(); }

    SecurityError error() const noexcept { return error_; }
    const std::string& error_message() const noexcept { return error_message_; }

    T& value() & { return *value_; }
    const T& value() const& { return *value_; }
    T&& value() && { return std::move(*value_); }

private:
    std::optional<T> value_;
    SecurityError error_;
    std::string error_message_;
};

template<>
class SecurityResult<void> {
public:
    SecurityResult() : error_(SecurityError::SUCCESS) {}
    SecurityResult(SecurityError error, std::string msg = {})
        : error_(error), error_message_(std::move(msg)) {}

    bool is_success() const noexcept { return error_ == SecurityError::SUCCESS; }
    bool is_error() const noexcept { return error_ != SecurityError::SUCCESS; }
    explicit operator bool() const noexcept { return is_success(); }

    SecurityError error() const noexcept { return error_; }
    const std::string& error_message() const noexcept { return error_message_; }

private:
    SecurityError error_;
    std::string error_message_;
};

// Type alias for convenience
template<typename T = void>
using Result = SecurityResult<T>;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

class TLSContext;
class TLSSocket;
class Certificate;
class PrivateKey;

// ============================================================================
// ENUMS AND CONSTANTS
// ============================================================================

/**
 * @brief TLS protocol versions
 */
enum class TLSVersion : uint8_t {
    TLS_1_0 = 0x10,  // Legacy, not recommended
    TLS_1_1 = 0x11,  // Legacy, not recommended
    TLS_1_2 = 0x12,  // Recommended minimum
    TLS_1_3 = 0x13,  // Latest and most secure
    AUTO    = 0xFF   // Let library choose best
};

/**
 * @brief TLS context mode
 */
enum class TLSMode : uint8_t {
    CLIENT,
    SERVER
};

/**
 * @brief Certificate verification mode
 */
enum class VerifyMode : uint8_t {
    NONE,           // No verification (insecure)
    OPTIONAL,       // Verify if presented
    REQUIRED,       // Must verify successfully
    REQUIRE_ONCE    // Verify only on first connection
};

/**
 * @brief TLS handshake status
 */
enum class HandshakeStatus : uint8_t {
    SUCCESS,
    WANT_READ,
    WANT_WRITE,
    FAILED,
    TIMEOUT
};

/**
 * @brief Security level presets
 */
enum class SecurityLevel : uint8_t {
    LOW,        // Allow legacy ciphers (compatibility)
    MEDIUM,     // Balance security and compatibility
    HIGH,       // Strong ciphers only
    FIPS        // FIPS-140-2 compliant
};

// ============================================================================
// CERTIFICATE AND KEY CLASSES
// ============================================================================

/**
 * @brief X.509 Certificate wrapper
 */
class Certificate {
public:
    Certificate() = default;
    ~Certificate();

    Certificate(Certificate&& other) noexcept;
    Certificate& operator=(Certificate&& other) noexcept;

    Certificate(const Certificate&) = delete;
    Certificate& operator=(const Certificate&) = delete;

    /**
     * @brief Load certificate from PEM file
     */
    static Result<Certificate> from_pem_file(const std::string& path);

    /**
     * @brief Load certificate from PEM string
     */
    static Result<Certificate> from_pem_string(std::string_view pem);

    /**
     * @brief Load certificate from DER buffer
     */
    static Result<Certificate> from_der(const std::vector<uint8_t>& der);

    /**
     * @brief Get certificate subject name
     */
    std::string subject() const;

    /**
     * @brief Get certificate issuer name
     */
    std::string issuer() const;

    /**
     * @brief Get certificate serial number
     */
    std::string serial_number() const;

    /**
     * @brief Get certificate validity start
     */
    std::chrono::system_clock::time_point not_before() const;

    /**
     * @brief Get certificate validity end
     */
    std::chrono::system_clock::time_point not_after() const;

    /**
     * @brief Check if certificate is currently valid
     */
    bool is_valid() const;

    /**
     * @brief Check if certificate will expire within given duration
     */
    bool expires_within(std::chrono::seconds duration) const;

    /**
     * @brief Get the internal handle (backend-specific)
     */
    void* native_handle() const { return handle_; }

private:
    void* handle_ = nullptr;

    friend class TLSContext;
    friend class OpenSSLSocket;
    friend class MbedTLSSocket;
    friend class WolfSSLSocket;
};

/**
 * @brief Private key wrapper
 */
class PrivateKey {
public:
    PrivateKey() = default;
    ~PrivateKey();

    PrivateKey(PrivateKey&& other) noexcept;
    PrivateKey& operator=(PrivateKey&& other) noexcept;

    PrivateKey(const PrivateKey&) = delete;
    PrivateKey& operator=(const PrivateKey&) = delete;

    /**
     * @brief Load private key from PEM file
     * @param path Path to PEM file
     * @param password Optional password for encrypted keys
     */
    static Result<PrivateKey> from_pem_file(
        const std::string& path,
        std::string_view password = {});

    /**
     * @brief Load private key from PEM string
     */
    static Result<PrivateKey> from_pem_string(
        std::string_view pem,
        std::string_view password = {});

    /**
     * @brief Get the internal handle (backend-specific)
     */
    void* native_handle() const { return handle_; }

private:
    void* handle_ = nullptr;

    friend class TLSContext;
};

// ============================================================================
// TLS CONTEXT CONFIGURATION
// ============================================================================

/**
 * @brief TLS context configuration
 */
struct TLSConfig {
    // Mode
    TLSMode mode = TLSMode::CLIENT;

    // Version constraints
    TLSVersion min_version = TLSVersion::TLS_1_2;
    TLSVersion max_version = TLSVersion::TLS_1_3;

    // Security level
    SecurityLevel security_level = SecurityLevel::HIGH;

    // Verification
    VerifyMode verify_mode = VerifyMode::REQUIRED;
    int verify_depth = 4;

    // Certificates and keys
    std::string cert_file;          // Server/client certificate
    std::string key_file;           // Private key
    std::string key_password;       // Key password (if encrypted)
    std::string ca_file;            // CA certificate(s)
    std::string ca_path;            // CA certificate directory

    // Cipher configuration
    std::string cipher_list;        // TLS 1.2 ciphers (OpenSSL format)
    std::string cipher_suites;      // TLS 1.3 cipher suites

    // SNI (Server Name Indication)
    std::string server_name;        // Expected server name for clients

    // ALPN (Application-Layer Protocol Negotiation)
    std::vector<std::string> alpn_protocols;

    // Session configuration
    bool enable_session_cache = true;
    std::chrono::seconds session_timeout{7200};  // 2 hours

    // Advanced options
    bool enable_ocsp_stapling = false;
    bool enable_sct = false;         // Signed Certificate Timestamps
    bool allow_renegotiation = false;

    /**
     * @brief Create a default client configuration
     */
    static TLSConfig default_client() {
        TLSConfig config;
        config.mode = TLSMode::CLIENT;
        return config;
    }

    /**
     * @brief Create a default server configuration
     */
    static TLSConfig default_server() {
        TLSConfig config;
        config.mode = TLSMode::SERVER;
        return config;
    }
};

// ============================================================================
// TLS CONTEXT
// ============================================================================

/**
 * @brief Abstract TLS context class
 *
 * This class provides a unified interface for TLS operations across
 * different SSL backends. Use TLSContext::create() to get the
 * appropriate implementation for the configured backend.
 */
class TLSContext {
public:
    virtual ~TLSContext() = default;

    /**
     * @brief Create a TLS context with the configured backend
     * @param config TLS configuration
     * @return Result containing TLS context or error
     */
    static Result<std::unique_ptr<TLSContext>> create(const TLSConfig& config);

    /**
     * @brief Get the SSL backend name
     */
    static std::string_view backend_name();

    /**
     * @brief Get the SSL backend version
     */
    static std::string backend_version();

    // -------------------------------------------------------------------------
    // Certificate Management
    // -------------------------------------------------------------------------

    /**
     * @brief Load certificate from file
     */
    virtual Result<void> load_certificate(const std::string& path) = 0;

    /**
     * @brief Load certificate chain from file
     */
    virtual Result<void> load_certificate_chain(const std::string& path) = 0;

    /**
     * @brief Load private key from file
     */
    virtual Result<void> load_private_key(
        const std::string& path,
        std::string_view password = {}) = 0;

    /**
     * @brief Load CA certificates from file
     */
    virtual Result<void> load_ca_certificates(const std::string& path) = 0;

    /**
     * @brief Load CA certificates from directory
     */
    virtual Result<void> load_ca_path(const std::string& path) = 0;

    /**
     * @brief Set the certificate and key directly
     */
    virtual Result<void> set_certificate(Certificate cert, PrivateKey key) = 0;

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /**
     * @brief Set TLS version constraints
     */
    virtual void set_version(TLSVersion min, TLSVersion max) = 0;

    /**
     * @brief Set cipher list (TLS 1.2 and below)
     */
    virtual Result<void> set_cipher_list(std::string_view ciphers) = 0;

    /**
     * @brief Set cipher suites (TLS 1.3)
     */
    virtual Result<void> set_cipher_suites(std::string_view suites) = 0;

    /**
     * @brief Set verification mode
     */
    virtual void set_verify_mode(VerifyMode mode) = 0;

    /**
     * @brief Set verification depth
     */
    virtual void set_verify_depth(int depth) = 0;

    /**
     * @brief Set ALPN protocols
     */
    virtual Result<void> set_alpn_protocols(
        const std::vector<std::string>& protocols) = 0;

    // -------------------------------------------------------------------------
    // Socket Creation
    // -------------------------------------------------------------------------

    /**
     * @brief Create a TLS socket wrapper for an existing socket
     * @param fd Native socket file descriptor
     * @return TLS socket or error
     */
    virtual Result<std::unique_ptr<TLSSocket>> wrap_socket(int fd) = 0;

    // -------------------------------------------------------------------------
    // Diagnostics
    // -------------------------------------------------------------------------

    /**
     * @brief Get list of available ciphers
     */
    virtual std::vector<std::string> get_available_ciphers() const = 0;

    /**
     * @brief Check if the context is properly initialized
     */
    virtual bool is_valid() const = 0;

protected:
    TLSContext() = default;
};

// ============================================================================
// TLS SOCKET
// ============================================================================

/**
 * @brief TLS socket wrapper
 *
 * Wraps a native socket with TLS encryption. Provides read/write
 * operations with automatic encryption/decryption.
 */
class TLSSocket {
public:
    virtual ~TLSSocket() = default;

    /**
     * @brief Perform TLS handshake
     * @param timeout Handshake timeout (0 = no timeout)
     * @return Handshake status
     */
    virtual HandshakeStatus do_handshake(
        std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) = 0;

    /**
     * @brief Read decrypted data
     * @param buffer Buffer to read into
     * @param length Maximum bytes to read
     * @return Number of bytes read, or negative on error
     */
    virtual ssize_t read(void* buffer, size_t length) = 0;

    /**
     * @brief Write data (will be encrypted)
     * @param buffer Data to write
     * @param length Number of bytes to write
     * @return Number of bytes written, or negative on error
     */
    virtual ssize_t write(const void* buffer, size_t length) = 0;

    /**
     * @brief Perform TLS shutdown
     */
    virtual Result<void> shutdown() = 0;

    /**
     * @brief Get the negotiated protocol (ALPN)
     */
    virtual std::string get_alpn_protocol() const = 0;

    /**
     * @brief Get the negotiated TLS version
     */
    virtual TLSVersion get_version() const = 0;

    /**
     * @brief Get the negotiated cipher name
     */
    virtual std::string get_cipher_name() const = 0;

    /**
     * @brief Get the peer certificate (if available)
     */
    virtual Result<Certificate> get_peer_certificate() const = 0;

    /**
     * @brief Check if the connection is encrypted
     */
    virtual bool is_encrypted() const = 0;

    /**
     * @brief Get the underlying socket file descriptor
     */
    virtual int native_fd() const = 0;

    /**
     * @brief Check if there's pending data in SSL buffer
     */
    virtual bool has_pending_data() const = 0;

    /**
     * @brief Get last error message
     */
    virtual std::string get_error_string() const = 0;

protected:
    TLSSocket() = default;
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * @brief Initialize the TLS library (call once at startup)
 */
Result<void> initialize();

/**
 * @brief Cleanup the TLS library (call at shutdown)
 */
void cleanup();

/**
 * @brief Generate a random byte array
 */
Result<std::vector<uint8_t>> random_bytes(size_t count);

/**
 * @brief Get default cipher list for security level
 */
std::string_view default_cipher_list(SecurityLevel level);

/**
 * @brief Get default TLS 1.3 cipher suites for security level
 */
std::string_view default_cipher_suites(SecurityLevel level);

} // namespace ipb::security
