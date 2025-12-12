#pragma once

/**
 * @file http_client.hpp
 * @brief High-level HTTP client for IPB
 *
 * Provides a simple, high-level API for HTTP operations.
 * Supports multiple backends (curl, beast) with automatic selection.
 */

#include "http_backend.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <chrono>
#include <optional>

namespace ipb::transport::http {

//=============================================================================
// HTTP Client Configuration
//=============================================================================

/**
 * @brief HTTP client configuration
 */
struct HTTPConfig {
    // Backend selection
    BackendType backend = default_backend_type();

    // Base URL for relative paths
    std::string base_url;

    // Default headers
    Headers default_headers;

    // Default timeouts
    std::chrono::milliseconds connect_timeout{30000};
    std::chrono::milliseconds timeout{60000};

    // TLS configuration
    bool verify_ssl = true;
    std::string ca_cert_path;
    std::string client_cert_path;
    std::string client_key_path;

    // HTTP/2 support
    bool use_http2 = true;

    // Connection pooling
    bool enable_connection_pool = true;
    size_t max_connections_per_host = 6;

    // Authentication
    std::string bearer_token;
    std::string basic_auth_user;
    std::string basic_auth_password;

    // Retry configuration
    int max_retries = 3;
    std::chrono::milliseconds retry_delay{1000};

    /**
     * @brief Create default configuration
     */
    static HTTPConfig default_config() {
        return HTTPConfig{};
    }
};

//=============================================================================
// HTTP Client
//=============================================================================

/**
 * @brief High-level HTTP client
 *
 * Usage:
 * @code
 * HTTPClient client;
 *
 * // Simple GET
 * auto response = client.get("https://api.example.com/data");
 *
 * // POST with JSON
 * auto response = client.post_json("https://api.example.com/data",
 *                                   R"({"key": "value"})");
 *
 * // Custom request
 * Request req;
 * req.method = Method::PUT;
 * req.url = "https://api.example.com/data/1";
 * req.set_json_content();
 * req.set_body(R"({"updated": true})");
 * auto response = client.execute(req);
 * @endcode
 */
class HTTPClient {
public:
    /**
     * @brief Construct with default configuration
     */
    HTTPClient();

    /**
     * @brief Construct with custom configuration
     */
    explicit HTTPClient(const HTTPConfig& config);

    /**
     * @brief Destructor
     */
    ~HTTPClient();

    // Non-copyable, movable
    HTTPClient(const HTTPClient&) = delete;
    HTTPClient& operator=(const HTTPClient&) = delete;
    HTTPClient(HTTPClient&&) noexcept;
    HTTPClient& operator=(HTTPClient&&) noexcept;

    //=========================================================================
    // Configuration
    //=========================================================================

    /**
     * @brief Get current configuration
     */
    const HTTPConfig& config() const noexcept;

    /**
     * @brief Set base URL for relative paths
     */
    void set_base_url(const std::string& url);

    /**
     * @brief Set default header
     */
    void set_default_header(const std::string& name, const std::string& value);

    /**
     * @brief Set bearer token for authentication
     */
    void set_bearer_token(const std::string& token);

    /**
     * @brief Set basic authentication
     */
    void set_basic_auth(const std::string& username, const std::string& password);

    /**
     * @brief Get the backend being used
     */
    BackendType backend_type() const noexcept;

    //=========================================================================
    // Simple Request Methods
    //=========================================================================

    /**
     * @brief Execute GET request
     */
    Response get(const std::string& url);

    /**
     * @brief Execute GET request with headers
     */
    Response get(const std::string& url, const Headers& headers);

    /**
     * @brief Execute POST request
     */
    Response post(const std::string& url, std::string_view body);

    /**
     * @brief Execute POST request with JSON body
     */
    Response post_json(const std::string& url, std::string_view json);

    /**
     * @brief Execute POST request with form data
     */
    Response post_form(const std::string& url, const std::map<std::string, std::string>& form_data);

    /**
     * @brief Execute PUT request
     */
    Response put(const std::string& url, std::string_view body);

    /**
     * @brief Execute PUT request with JSON body
     */
    Response put_json(const std::string& url, std::string_view json);

    /**
     * @brief Execute PATCH request
     */
    Response patch(const std::string& url, std::string_view body);

    /**
     * @brief Execute DELETE request
     */
    Response delete_(const std::string& url);

    /**
     * @brief Execute HEAD request
     */
    Response head(const std::string& url);

    //=========================================================================
    // Custom Request
    //=========================================================================

    /**
     * @brief Execute custom request (synchronous)
     */
    Response execute(const Request& request);

    /**
     * @brief Execute custom request (asynchronous)
     */
    void execute_async(const Request& request, ResponseCallback callback);

    //=========================================================================
    // Connection Management
    //=========================================================================

    /**
     * @brief Close all connections
     */
    void close_all();

    //=========================================================================
    // Statistics
    //=========================================================================

    /**
     * @brief Get statistics
     */
    const BackendStats& stats() const noexcept;

    /**
     * @brief Reset statistics
     */
    void reset_stats();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

//=============================================================================
// Utility Functions
//=============================================================================

/**
 * @brief URL encode a string
 */
std::string url_encode(std::string_view str);

/**
 * @brief URL decode a string
 */
std::string url_decode(std::string_view str);

/**
 * @brief Build query string from parameters
 */
std::string build_query_string(const std::map<std::string, std::string>& params);

/**
 * @brief Parse URL into components
 */
struct URLComponents {
    std::string scheme;
    std::string host;
    uint16_t port = 0;
    std::string path;
    std::string query;
};

std::optional<URLComponents> parse_url(const std::string& url);

} // namespace ipb::transport::http
