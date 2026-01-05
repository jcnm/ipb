/**
 * @file tls_openssl.cpp
 * @brief OpenSSL implementation of the TLS abstraction layer
 */

#include <ipb/security/tls_context.hpp>

#if defined(IPB_SSL_OPENSSL) || \
    !defined(IPB_SSL_MBEDTLS) && !defined(IPB_SSL_WOLFSSL) && !defined(IPB_SSL_NONE)

#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace ipb::security {

namespace {

// OpenSSL initialization state
std::once_flag ssl_init_flag;
std::atomic<bool> ssl_initialized{false};

// Error helper
std::string get_openssl_error() {
    unsigned long err = ERR_get_error();
    if (err == 0)
        return "Unknown error";

    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    return buf;
}

// Version conversion
// SECURITY: TLS 1.0 and 1.1 are deprecated (RFC 8996) and vulnerable to POODLE, BEAST attacks
// Minimum enforced version is TLS 1.2
int tls_version_to_openssl(TLSVersion version) {
    switch (version) {
        case TLSVersion::TLS_1_0:
            // SECURITY: TLS 1.0 is deprecated - upgrade to TLS 1.2
            std::cerr << "[SECURITY WARNING] TLS 1.0 requested but deprecated (RFC 8996). "
                      << "Using TLS 1.2 minimum instead." << std::endl;
            return TLS1_2_VERSION;
        case TLSVersion::TLS_1_1:
            // SECURITY: TLS 1.1 is deprecated - upgrade to TLS 1.2
            std::cerr << "[SECURITY WARNING] TLS 1.1 requested but deprecated (RFC 8996). "
                      << "Using TLS 1.2 minimum instead." << std::endl;
            return TLS1_2_VERSION;
        case TLSVersion::TLS_1_2:
            return TLS1_2_VERSION;
        case TLSVersion::TLS_1_3:
            return TLS1_3_VERSION;
        case TLSVersion::AUTO:
            // AUTO now defaults to TLS 1.2 minimum for security
            return TLS1_2_VERSION;
        default:
            return TLS1_2_VERSION;
    }
}

TLSVersion openssl_version_to_tls(int version) {
    switch (version) {
        case TLS1_VERSION:
            return TLSVersion::TLS_1_0;
        case TLS1_1_VERSION:
            return TLSVersion::TLS_1_1;
        case TLS1_2_VERSION:
            return TLSVersion::TLS_1_2;
        case TLS1_3_VERSION:
            return TLSVersion::TLS_1_3;
        default:
            return TLSVersion::AUTO;
    }
}

}  // anonymous namespace

// ============================================================================
// Certificate Implementation
// ============================================================================

Certificate::~Certificate() {
    if (handle_) {
        X509_free(static_cast<X509*>(handle_));
    }
}

Certificate::Certificate(Certificate&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

Certificate& Certificate::operator=(Certificate&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            X509_free(static_cast<X509*>(handle_));
        }
        handle_       = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

Result<Certificate> Certificate::from_pem_file(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "r");
    if (!fp) {
        return Result<Certificate>(SecurityError::FILE_NOT_FOUND,
                                   "Failed to open certificate file: " + path);
    }

    X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!cert) {
        return Result<Certificate>(SecurityError::CERTIFICATE_INVALID,
                                   "Failed to read certificate: " + get_openssl_error());
    }

    Certificate result;
    result.handle_ = cert;
    return result;
}

Result<Certificate> Certificate::from_pem_string(std::string_view pem) {
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) {
        return Result<Certificate>(SecurityError::MEMORY_ALLOCATION_FAILED, "Failed to create BIO");
    }

    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!cert) {
        return Result<Certificate>(SecurityError::CERTIFICATE_INVALID,
                                   "Failed to parse certificate: " + get_openssl_error());
    }

    Certificate result;
    result.handle_ = cert;
    return result;
}

Result<Certificate> Certificate::from_der(const std::vector<uint8_t>& der) {
    const unsigned char* p = der.data();
    X509* cert             = d2i_X509(nullptr, &p, static_cast<long>(der.size()));

    if (!cert) {
        return Result<Certificate>(SecurityError::CERTIFICATE_INVALID,
                                   "Failed to parse DER certificate: " + get_openssl_error());
    }

    Certificate result;
    result.handle_ = cert;
    return result;
}

std::string Certificate::subject() const {
    if (!handle_)
        return {};

    X509* cert      = static_cast<X509*>(handle_);
    X509_NAME* name = X509_get_subject_name(cert);
    if (!name)
        return {};

    char buf[256];
    X509_NAME_oneline(name, buf, sizeof(buf));
    return buf;
}

std::string Certificate::issuer() const {
    if (!handle_)
        return {};

    X509* cert      = static_cast<X509*>(handle_);
    X509_NAME* name = X509_get_issuer_name(cert);
    if (!name)
        return {};

    char buf[256];
    X509_NAME_oneline(name, buf, sizeof(buf));
    return buf;
}

std::string Certificate::serial_number() const {
    if (!handle_)
        return {};

    X509* cert           = static_cast<X509*>(handle_);
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    if (!serial)
        return {};

    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    if (!bn)
        return {};

    char* hex = BN_bn2hex(bn);
    BN_free(bn);

    if (!hex)
        return {};

    std::string result(hex);
    OPENSSL_free(hex);
    return result;
}

std::chrono::system_clock::time_point Certificate::not_before() const {
    if (!handle_)
        return {};

    X509* cert            = static_cast<X509*>(handle_);
    const ASN1_TIME* time = X509_get0_notBefore(cert);
    if (!time)
        return {};

    struct tm tm_time;
    ASN1_TIME_to_tm(time, &tm_time);
    return std::chrono::system_clock::from_time_t(mktime(&tm_time));
}

std::chrono::system_clock::time_point Certificate::not_after() const {
    if (!handle_)
        return {};

    X509* cert            = static_cast<X509*>(handle_);
    const ASN1_TIME* time = X509_get0_notAfter(cert);
    if (!time)
        return {};

    struct tm tm_time;
    ASN1_TIME_to_tm(time, &tm_time);
    return std::chrono::system_clock::from_time_t(mktime(&tm_time));
}

bool Certificate::is_valid() const {
    auto now = std::chrono::system_clock::now();
    return now >= not_before() && now <= not_after();
}

bool Certificate::expires_within(std::chrono::seconds duration) const {
    auto expiry    = not_after();
    auto threshold = std::chrono::system_clock::now() + duration;
    return expiry <= threshold;
}

// ============================================================================
// PrivateKey Implementation
// ============================================================================

PrivateKey::~PrivateKey() {
    if (handle_) {
        EVP_PKEY_free(static_cast<EVP_PKEY*>(handle_));
    }
}

PrivateKey::PrivateKey(PrivateKey&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

PrivateKey& PrivateKey::operator=(PrivateKey&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            EVP_PKEY_free(static_cast<EVP_PKEY*>(handle_));
        }
        handle_       = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

Result<PrivateKey> PrivateKey::from_pem_file(const std::string& path, std::string_view password) {
    FILE* fp = fopen(path.c_str(), "r");
    if (!fp) {
        return Result<PrivateKey>(SecurityError::FILE_NOT_FOUND,
                                  "Failed to open key file: " + path);
    }

    EVP_PKEY* key = PEM_read_PrivateKey(
        fp, nullptr, nullptr, password.empty() ? nullptr : const_cast<char*>(password.data()));
    fclose(fp);

    if (!key) {
        return Result<PrivateKey>(SecurityError::KEY_INVALID,
                                  "Failed to read private key: " + get_openssl_error());
    }

    PrivateKey result;
    result.handle_ = key;
    return result;
}

Result<PrivateKey> PrivateKey::from_pem_string(std::string_view pem, std::string_view password) {
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) {
        return Result<PrivateKey>(SecurityError::MEMORY_ALLOCATION_FAILED, "Failed to create BIO");
    }

    EVP_PKEY* key = PEM_read_bio_PrivateKey(
        bio, nullptr, nullptr, password.empty() ? nullptr : const_cast<char*>(password.data()));
    BIO_free(bio);

    if (!key) {
        return Result<PrivateKey>(SecurityError::KEY_INVALID,
                                  "Failed to parse private key: " + get_openssl_error());
    }

    PrivateKey result;
    result.handle_ = key;
    return result;
}

// ============================================================================
// OpenSSL TLS Context Implementation
// ============================================================================

class OpenSSLContext : public TLSContext {
public:
    explicit OpenSSLContext(const TLSConfig& config);
    ~OpenSSLContext() override;

    Result<void> load_certificate(const std::string& path) override;
    Result<void> load_certificate_chain(const std::string& path) override;
    Result<void> load_private_key(const std::string& path, std::string_view password) override;
    Result<void> load_ca_certificates(const std::string& path) override;
    Result<void> load_ca_path(const std::string& path) override;
    Result<void> set_certificate(Certificate cert, PrivateKey key) override;

    void set_version(TLSVersion min, TLSVersion max) override;
    Result<void> set_cipher_list(std::string_view ciphers) override;
    Result<void> set_cipher_suites(std::string_view suites) override;
    void set_verify_mode(VerifyMode mode) override;
    void set_verify_depth(int depth) override;
    Result<void> set_alpn_protocols(const std::vector<std::string>& protocols) override;

    Result<std::unique_ptr<TLSSocket>> wrap_socket(int fd) override;

    std::vector<std::string> get_available_ciphers() const override;
    bool is_valid() const override { return ctx_ != nullptr; }

    SSL_CTX* native_ctx() const { return ctx_; }

private:
    SSL_CTX* ctx_ = nullptr;
    TLSConfig config_;
    std::vector<uint8_t> alpn_data_;
};

// ============================================================================
// OpenSSL TLS Socket Implementation
// ============================================================================

class OpenSSLSocket : public TLSSocket {
public:
    OpenSSLSocket(SSL* ssl, int fd);
    ~OpenSSLSocket() override;

    HandshakeStatus do_handshake(std::chrono::milliseconds timeout) override;
    ssize_t tls_read(void* buffer, size_t length) override;
    ssize_t tls_write(const void* buffer, size_t length) override;
    Result<void> shutdown() override;

    std::string get_alpn_protocol() const override;
    TLSVersion get_version() const override;
    std::string get_cipher_name() const override;
    Result<Certificate> get_peer_certificate() const override;
    bool is_encrypted() const override;
    int native_fd() const override { return fd_; }
    bool has_pending_data() const override;
    std::string get_error_string() const override;

private:
    SSL* ssl_;
    int fd_;
    bool handshake_done_ = false;
};

// ============================================================================
// OpenSSL Context Implementation
// ============================================================================

OpenSSLContext::OpenSSLContext(const TLSConfig& config) : config_(config) {
    const SSL_METHOD* method =
        config.mode == TLSMode::SERVER ? TLS_server_method() : TLS_client_method();

    ctx_ = SSL_CTX_new(method);
    if (!ctx_) {
        return;
    }

    // Set version constraints
    set_version(config.min_version, config.max_version);

    // Set verification mode
    set_verify_mode(config.verify_mode);
    set_verify_depth(config.verify_depth);

    // Set cipher list based on security level
    std::string ciphers;
    switch (config.security_level) {
        case SecurityLevel::LOW:
            ciphers = "DEFAULT:!aNULL:!eNULL";
            break;
        case SecurityLevel::MEDIUM:
            ciphers = "HIGH:!aNULL:!eNULL:!MD5";
            break;
        case SecurityLevel::HIGH:
            ciphers = "ECDHE+AESGCM:DHE+AESGCM:ECDHE+CHACHA20:DHE+CHACHA20:!aNULL:!eNULL:!MD5:!DSS";
            break;
        case SecurityLevel::FIPS:
            ciphers = "ECDHE+AESGCM:DHE+AESGCM:!aNULL:!eNULL:!MD5:!DSS:!RC4:!3DES";
            break;
    }

    if (!config.cipher_list.empty()) {
        ciphers = config.cipher_list;
    }
    SSL_CTX_set_cipher_list(ctx_, ciphers.c_str());

    // Set TLS 1.3 cipher suites
    if (!config.cipher_suites.empty()) {
        SSL_CTX_set_ciphersuites(ctx_, config.cipher_suites.c_str());
    }

    // Load certificates if specified
    if (!config.cert_file.empty()) {
        load_certificate_chain(config.cert_file);
    }
    if (!config.key_file.empty()) {
        load_private_key(config.key_file, config.key_password);
    }
    if (!config.ca_file.empty()) {
        load_ca_certificates(config.ca_file);
    }
    if (!config.ca_path.empty()) {
        load_ca_path(config.ca_path);
    }

    // Configure session caching
    if (config.enable_session_cache) {
        SSL_CTX_set_session_cache_mode(ctx_, SSL_SESS_CACHE_BOTH);
        SSL_CTX_set_timeout(ctx_, static_cast<long>(config.session_timeout.count()));
    } else {
        SSL_CTX_set_session_cache_mode(ctx_, SSL_SESS_CACHE_OFF);
    }

    // Set ALPN if specified
    if (!config.alpn_protocols.empty()) {
        set_alpn_protocols(config.alpn_protocols);
    }
}

OpenSSLContext::~OpenSSLContext() {
    if (ctx_) {
        SSL_CTX_free(ctx_);
    }
}

Result<void> OpenSSLContext::load_certificate(const std::string& path) {
    if (SSL_CTX_use_certificate_file(ctx_, path.c_str(), SSL_FILETYPE_PEM) != 1) {
        return Result<void>(SecurityError::CERTIFICATE_INVALID,
                            "Failed to load certificate: " + get_openssl_error());
    }
    return Result<void>();
}

Result<void> OpenSSLContext::load_certificate_chain(const std::string& path) {
    if (SSL_CTX_use_certificate_chain_file(ctx_, path.c_str()) != 1) {
        return Result<void>(SecurityError::CERTIFICATE_INVALID,
                            "Failed to load certificate chain: " + get_openssl_error());
    }
    return Result<void>();
}

Result<void> OpenSSLContext::load_private_key(const std::string& path, std::string_view password) {
    if (!password.empty()) {
        SSL_CTX_set_default_passwd_cb_userdata(ctx_, const_cast<char*>(password.data()));
    }

    if (SSL_CTX_use_PrivateKey_file(ctx_, path.c_str(), SSL_FILETYPE_PEM) != 1) {
        return Result<void>(SecurityError::KEY_INVALID,
                            "Failed to load private key: " + get_openssl_error());
    }

    if (SSL_CTX_check_private_key(ctx_) != 1) {
        return Result<void>(SecurityError::KEY_INVALID, "Private key does not match certificate");
    }

    return Result<void>();
}

Result<void> OpenSSLContext::load_ca_certificates(const std::string& path) {
    if (SSL_CTX_load_verify_locations(ctx_, path.c_str(), nullptr) != 1) {
        return Result<void>(SecurityError::CERTIFICATE_INVALID,
                            "Failed to load CA certificates: " + get_openssl_error());
    }
    return Result<void>();
}

Result<void> OpenSSLContext::load_ca_path(const std::string& path) {
    if (SSL_CTX_load_verify_locations(ctx_, nullptr, path.c_str()) != 1) {
        return Result<void>(SecurityError::CERTIFICATE_INVALID,
                            "Failed to load CA path: " + get_openssl_error());
    }
    return Result<void>();
}

Result<void> OpenSSLContext::set_certificate(Certificate cert, PrivateKey key) {
    X509* x509     = static_cast<X509*>(cert.native_handle());
    EVP_PKEY* pkey = static_cast<EVP_PKEY*>(key.native_handle());

    if (SSL_CTX_use_certificate(ctx_, x509) != 1) {
        return Result<void>(SecurityError::CERTIFICATE_INVALID,
                            "Failed to set certificate: " + get_openssl_error());
    }

    if (SSL_CTX_use_PrivateKey(ctx_, pkey) != 1) {
        return Result<void>(SecurityError::KEY_INVALID,
                            "Failed to set private key: " + get_openssl_error());
    }

    return Result<void>();
}

void OpenSSLContext::set_version(TLSVersion min, TLSVersion max) {
    if (min != TLSVersion::AUTO) {
        SSL_CTX_set_min_proto_version(ctx_, tls_version_to_openssl(min));
    }
    if (max != TLSVersion::AUTO) {
        SSL_CTX_set_max_proto_version(ctx_, tls_version_to_openssl(max));
    }
}

Result<void> OpenSSLContext::set_cipher_list(std::string_view ciphers) {
    if (SSL_CTX_set_cipher_list(ctx_, std::string(ciphers).c_str()) != 1) {
        return Result<void>(SecurityError::CONFIG_INVALID,
                            "Invalid cipher list: " + get_openssl_error());
    }
    return Result<void>();
}

Result<void> OpenSSLContext::set_cipher_suites(std::string_view suites) {
    if (SSL_CTX_set_ciphersuites(ctx_, std::string(suites).c_str()) != 1) {
        return Result<void>(SecurityError::CONFIG_INVALID,
                            "Invalid cipher suites: " + get_openssl_error());
    }
    return Result<void>();
}

void OpenSSLContext::set_verify_mode(VerifyMode mode) {
    int ssl_mode;
    switch (mode) {
        case VerifyMode::NONE:
            // SECURITY WARNING: Certificate verification is DISABLED
            // This makes the connection vulnerable to man-in-the-middle attacks
            // Only use this mode for development/testing purposes
            std::cerr << "[SECURITY WARNING] TLS certificate verification DISABLED - "
                      << "connection is vulnerable to MITM attacks!" << std::endl;
            ssl_mode = SSL_VERIFY_NONE;
            break;
        case VerifyMode::PEER_OPTIONAL:
            ssl_mode = SSL_VERIFY_PEER;
            break;
        case VerifyMode::REQUIRED:
            ssl_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
            break;
        case VerifyMode::REQUIRE_ONCE:
            ssl_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE;
            break;
        default:
            ssl_mode = SSL_VERIFY_PEER;
    }
    SSL_CTX_set_verify(ctx_, ssl_mode, nullptr);
}

void OpenSSLContext::set_verify_depth(int depth) {
    SSL_CTX_set_verify_depth(ctx_, depth);
}

Result<void> OpenSSLContext::set_alpn_protocols(const std::vector<std::string>& protocols) {
    alpn_data_.clear();
    for (const auto& proto : protocols) {
        alpn_data_.push_back(static_cast<uint8_t>(proto.size()));
        alpn_data_.insert(alpn_data_.end(), proto.begin(), proto.end());
    }

    if (SSL_CTX_set_alpn_protos(ctx_, alpn_data_.data(), alpn_data_.size()) != 0) {
        return Result<void>(SecurityError::CONFIG_INVALID, "Failed to set ALPN protocols");
    }
    return Result<void>();
}

Result<std::unique_ptr<TLSSocket>> OpenSSLContext::wrap_socket(int fd) {
    SSL* ssl = SSL_new(ctx_);
    if (!ssl) {
        return Result<std::unique_ptr<TLSSocket>>(
            SecurityError::MEMORY_ALLOCATION_FAILED,
            "Failed to create SSL object: " + get_openssl_error());
    }

    if (SSL_set_fd(ssl, fd) != 1) {
        SSL_free(ssl);
        return Result<std::unique_ptr<TLSSocket>>(SecurityError::SOCKET_FAILURE,
                                                  "Failed to set socket: " + get_openssl_error());
    }

    // Set SNI if configured and client mode
    if (config_.mode == TLSMode::CLIENT && !config_.server_name.empty()) {
        SSL_set_tlsext_host_name(ssl, config_.server_name.c_str());
    }

    return Result<std::unique_ptr<TLSSocket>>(std::make_unique<OpenSSLSocket>(ssl, fd));
}

std::vector<std::string> OpenSSLContext::get_available_ciphers() const {
    std::vector<std::string> result;

    STACK_OF(SSL_CIPHER)* ciphers = SSL_CTX_get_ciphers(ctx_);
    if (ciphers) {
        int count = sk_SSL_CIPHER_num(ciphers);
        result.reserve(count);
        for (int i = 0; i < count; ++i) {
            const SSL_CIPHER* cipher = sk_SSL_CIPHER_value(ciphers, i);
            result.emplace_back(SSL_CIPHER_get_name(cipher));
        }
    }

    return result;
}

// ============================================================================
// OpenSSL Socket Implementation
// ============================================================================

OpenSSLSocket::OpenSSLSocket(SSL* ssl, int fd) : ssl_(ssl), fd_(fd) {}

OpenSSLSocket::~OpenSSLSocket() {
    if (ssl_) {
        SSL_free(ssl_);
    }
}

HandshakeStatus OpenSSLSocket::do_handshake(std::chrono::milliseconds /*timeout*/) {
    int ret = SSL_do_handshake(ssl_);
    if (ret == 1) {
        handshake_done_ = true;
        return HandshakeStatus::SUCCESS;
    }

    int err = SSL_get_error(ssl_, ret);
    switch (err) {
        case SSL_ERROR_WANT_READ:
            return HandshakeStatus::WANT_READ;
        case SSL_ERROR_WANT_WRITE:
            return HandshakeStatus::WANT_WRITE;
        default:
            return HandshakeStatus::FAILED;
    }
}

ssize_t OpenSSLSocket::tls_read(void* buffer, size_t length) {
    int ret = SSL_read(ssl_, buffer, static_cast<int>(length));
    if (ret <= 0) {
        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            return -1;  // Would block
        }
        return -2;  // Error
    }
    return ret;
}

ssize_t OpenSSLSocket::tls_write(const void* buffer, size_t length) {
    int ret = SSL_write(ssl_, buffer, static_cast<int>(length));
    if (ret <= 0) {
        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            return -1;  // Would block
        }
        return -2;  // Error
    }
    return ret;
}

Result<void> OpenSSLSocket::shutdown() {
    int ret = SSL_shutdown(ssl_);
    if (ret < 0) {
        return Result<void>(SecurityError::SOCKET_FAILURE,
                            "SSL shutdown failed: " + get_openssl_error());
    }
    return Result<void>();
}

std::string OpenSSLSocket::get_alpn_protocol() const {
    const unsigned char* data;
    unsigned int len;
    SSL_get0_alpn_selected(ssl_, &data, &len);
    if (data && len > 0) {
        return std::string(reinterpret_cast<const char*>(data), len);
    }
    return {};
}

TLSVersion OpenSSLSocket::get_version() const {
    return openssl_version_to_tls(SSL_version(ssl_));
}

std::string OpenSSLSocket::get_cipher_name() const {
    const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_);
    if (cipher) {
        return SSL_CIPHER_get_name(cipher);
    }
    return {};
}

Result<Certificate> OpenSSLSocket::get_peer_certificate() const {
    X509* cert = SSL_get_peer_certificate(ssl_);
    if (!cert) {
        return Result<Certificate>(SecurityError::CERTIFICATE_INVALID,
                                   "No peer certificate available");
    }

    Certificate result;
    result.handle_ = cert;
    return result;
}

bool OpenSSLSocket::is_encrypted() const {
    return handshake_done_ && SSL_get_current_cipher(ssl_) != nullptr;
}

bool OpenSSLSocket::has_pending_data() const {
    return SSL_pending(ssl_) > 0;
}

std::string OpenSSLSocket::get_error_string() const {
    return get_openssl_error();
}

// ============================================================================
// TLS Context Factory
// ============================================================================

Result<std::unique_ptr<TLSContext>> TLSContext::create(const TLSConfig& config) {
    // Ensure OpenSSL is initialized
    auto init_result = initialize();
    if (!init_result.is_success()) {
        return Result<std::unique_ptr<TLSContext>>(init_result.error(),
                                                   init_result.error_message());
    }

    auto ctx = std::make_unique<OpenSSLContext>(config);
    if (!ctx->is_valid()) {
        return Result<std::unique_ptr<TLSContext>>(
            SecurityError::INITIALIZATION_FAILED,
            "Failed to create TLS context: " + get_openssl_error());
    }

    return Result<std::unique_ptr<TLSContext>>(std::move(ctx));
}

std::string_view TLSContext::backend_name() {
    return "OpenSSL";
}

std::string TLSContext::backend_version() {
    return OpenSSL_version(OPENSSL_VERSION);
}

// ============================================================================
// Utility Functions
// ============================================================================

Result<void> initialize() {
    std::call_once(ssl_init_flag, []() {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        ssl_initialized.store(true);
    });

    if (!ssl_initialized.load()) {
        return Result<void>(SecurityError::INITIALIZATION_FAILED, "Failed to initialize OpenSSL");
    }

    return Result<void>();
}

void cleanup() {
    if (ssl_initialized.exchange(false)) {
        EVP_cleanup();
        ERR_free_strings();
    }
}

Result<std::vector<uint8_t>> random_bytes(size_t count) {
    std::vector<uint8_t> result(count);
    if (RAND_bytes(result.data(), static_cast<int>(count)) != 1) {
        return Result<std::vector<uint8_t>>(SecurityError::CRYPTO_ERROR,
                                            "Failed to generate random bytes");
    }
    return result;
}

std::string_view default_cipher_list(SecurityLevel level) {
    switch (level) {
        case SecurityLevel::LOW:
            return "DEFAULT:!aNULL:!eNULL";
        case SecurityLevel::MEDIUM:
            return "HIGH:!aNULL:!eNULL:!MD5";
        case SecurityLevel::HIGH:
            return "ECDHE+AESGCM:DHE+AESGCM:ECDHE+CHACHA20:DHE+CHACHA20:!aNULL:!eNULL:!MD5:!DSS";
        case SecurityLevel::FIPS:
            return "ECDHE+AESGCM:DHE+AESGCM:!aNULL:!eNULL:!MD5:!DSS:!RC4:!3DES";
        default:
            return "HIGH:!aNULL:!eNULL:!MD5";
    }
}

std::string_view default_cipher_suites(SecurityLevel level) {
    switch (level) {
        case SecurityLevel::LOW:
        case SecurityLevel::MEDIUM:
            return "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";
        case SecurityLevel::HIGH:
        case SecurityLevel::FIPS:
            return "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";
        default:
            return "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";
    }
}

}  // namespace ipb::security

#endif  // IPB_SSL_OPENSSL
