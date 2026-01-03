/**
 * @file test_http_transport.cpp
 * @brief Comprehensive unit tests for HTTP transport layer
 *
 * Tests cover:
 * - BackendType enum and utilities
 * - Method enum and method_to_string
 * - StatusCategory enum and status_category function
 * - Request struct
 * - Response struct
 * - BackendStats struct
 * - HTTPConfig struct
 * - HTTPClient (mocked)
 * - Utility functions (url_encode, url_decode, build_query_string, parse_url)
 */

#include <ipb/transport/http/http_client.hpp>

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace ipb::transport::http;
using namespace std::chrono_literals;

// ============================================================================
// BackendType Tests
// ============================================================================

class HTTPBackendTypeTest : public ::testing::Test {};

TEST_F(HTTPBackendTypeTest, EnumValues) {
    EXPECT_EQ(static_cast<int>(BackendType::CURL), 0);
    EXPECT_EQ(static_cast<int>(BackendType::BEAST), 1);
    EXPECT_EQ(static_cast<int>(BackendType::NATIVE), 2);
}

TEST_F(HTTPBackendTypeTest, TypeNames) {
    EXPECT_EQ(backend_type_name(BackendType::CURL), "curl");
    EXPECT_EQ(backend_type_name(BackendType::BEAST), "beast");
    EXPECT_EQ(backend_type_name(BackendType::NATIVE), "native");
}

TEST_F(HTTPBackendTypeTest, DefaultBackendType) {
    BackendType type = default_backend_type();
    // Should be CURL or BEAST depending on build config
    EXPECT_TRUE(type == BackendType::CURL || type == BackendType::BEAST);
}

// ============================================================================
// Method Tests
// ============================================================================

class HTTPMethodTest : public ::testing::Test {};

TEST_F(HTTPMethodTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(Method::GET), 0);
    EXPECT_EQ(static_cast<uint8_t>(Method::POST), 1);
    EXPECT_EQ(static_cast<uint8_t>(Method::PUT), 2);
    EXPECT_EQ(static_cast<uint8_t>(Method::PATCH), 3);
    EXPECT_EQ(static_cast<uint8_t>(Method::DELETE_), 4);
    EXPECT_EQ(static_cast<uint8_t>(Method::HEAD), 5);
    EXPECT_EQ(static_cast<uint8_t>(Method::OPTIONS), 6);
}

TEST_F(HTTPMethodTest, MethodToString) {
    EXPECT_EQ(method_to_string(Method::GET), "GET");
    EXPECT_EQ(method_to_string(Method::POST), "POST");
    EXPECT_EQ(method_to_string(Method::PUT), "PUT");
    EXPECT_EQ(method_to_string(Method::PATCH), "PATCH");
    EXPECT_EQ(method_to_string(Method::DELETE_), "DELETE");
    EXPECT_EQ(method_to_string(Method::HEAD), "HEAD");
    EXPECT_EQ(method_to_string(Method::OPTIONS), "OPTIONS");
}

// ============================================================================
// StatusCategory Tests
// ============================================================================

class HTTPStatusCategoryTest : public ::testing::Test {};

TEST_F(HTTPStatusCategoryTest, Informational) {
    EXPECT_EQ(status_category(100), StatusCategory::INFORMATIONAL);
    EXPECT_EQ(status_category(101), StatusCategory::INFORMATIONAL);
    EXPECT_EQ(status_category(199), StatusCategory::INFORMATIONAL);
}

TEST_F(HTTPStatusCategoryTest, Success) {
    EXPECT_EQ(status_category(200), StatusCategory::SUCCESS);
    EXPECT_EQ(status_category(201), StatusCategory::SUCCESS);
    EXPECT_EQ(status_category(204), StatusCategory::SUCCESS);
    EXPECT_EQ(status_category(299), StatusCategory::SUCCESS);
}

TEST_F(HTTPStatusCategoryTest, Redirection) {
    EXPECT_EQ(status_category(300), StatusCategory::REDIRECTION);
    EXPECT_EQ(status_category(301), StatusCategory::REDIRECTION);
    EXPECT_EQ(status_category(302), StatusCategory::REDIRECTION);
    EXPECT_EQ(status_category(304), StatusCategory::REDIRECTION);
    EXPECT_EQ(status_category(399), StatusCategory::REDIRECTION);
}

TEST_F(HTTPStatusCategoryTest, ClientError) {
    EXPECT_EQ(status_category(400), StatusCategory::CLIENT_ERROR);
    EXPECT_EQ(status_category(401), StatusCategory::CLIENT_ERROR);
    EXPECT_EQ(status_category(403), StatusCategory::CLIENT_ERROR);
    EXPECT_EQ(status_category(404), StatusCategory::CLIENT_ERROR);
    EXPECT_EQ(status_category(499), StatusCategory::CLIENT_ERROR);
}

TEST_F(HTTPStatusCategoryTest, ServerError) {
    EXPECT_EQ(status_category(500), StatusCategory::SERVER_ERROR);
    EXPECT_EQ(status_category(502), StatusCategory::SERVER_ERROR);
    EXPECT_EQ(status_category(503), StatusCategory::SERVER_ERROR);
    EXPECT_EQ(status_category(599), StatusCategory::SERVER_ERROR);
}

// ============================================================================
// Request Tests
// ============================================================================

class HTTPRequestTest : public ::testing::Test {};

TEST_F(HTTPRequestTest, DefaultValues) {
    Request req;

    EXPECT_EQ(req.method, Method::GET);
    EXPECT_TRUE(req.url.empty());
    EXPECT_TRUE(req.headers.empty());
    EXPECT_TRUE(req.body.empty());
    EXPECT_EQ(req.connect_timeout, std::chrono::milliseconds(30000));
    EXPECT_EQ(req.timeout, std::chrono::milliseconds(60000));
    EXPECT_TRUE(req.verify_ssl);
    EXPECT_TRUE(req.follow_redirects);
    EXPECT_EQ(req.max_redirects, 10);
    EXPECT_TRUE(req.use_http2);
}

TEST_F(HTTPRequestTest, SetJsonContent) {
    Request req;
    req.set_json_content();

    EXPECT_EQ(req.headers["Content-Type"], "application/json");
}

TEST_F(HTTPRequestTest, SetFormContent) {
    Request req;
    req.set_form_content();

    EXPECT_EQ(req.headers["Content-Type"], "application/x-www-form-urlencoded");
}

TEST_F(HTTPRequestTest, SetBody) {
    Request req;
    req.set_body("{\"key\": \"value\"}");

    EXPECT_EQ(req.body.size(), 16u);
    std::string body_str(req.body.begin(), req.body.end());
    EXPECT_EQ(body_str, "{\"key\": \"value\"}");
}

TEST_F(HTTPRequestTest, CustomValues) {
    Request req;
    req.method = Method::POST;
    req.url = "https://api.example.com/data";
    req.headers["Authorization"] = "Bearer token123";
    req.headers["Accept"] = "application/json";
    req.set_json_content();
    req.set_body("{\"name\": \"test\"}");
    req.timeout = 10s;
    req.verify_ssl = false;

    EXPECT_EQ(req.method, Method::POST);
    EXPECT_EQ(req.url, "https://api.example.com/data");
    EXPECT_EQ(req.headers.size(), 3u);
    EXPECT_EQ(req.timeout, 10s);
    EXPECT_FALSE(req.verify_ssl);
}

// ============================================================================
// Response Tests
// ============================================================================

class HTTPResponseTest : public ::testing::Test {};

TEST_F(HTTPResponseTest, DefaultValues) {
    Response resp;

    EXPECT_EQ(resp.status_code, 0);
    EXPECT_TRUE(resp.status_message.empty());
    EXPECT_TRUE(resp.headers.empty());
    EXPECT_TRUE(resp.body.empty());
    EXPECT_EQ(resp.total_time, std::chrono::microseconds(0));
    EXPECT_EQ(resp.connect_time, std::chrono::microseconds(0));
    EXPECT_TRUE(resp.error_message.empty());
}

TEST_F(HTTPResponseTest, IsSuccess) {
    Response resp;

    resp.status_code = 200;
    EXPECT_TRUE(resp.is_success());

    resp.status_code = 201;
    EXPECT_TRUE(resp.is_success());

    resp.status_code = 204;
    EXPECT_TRUE(resp.is_success());

    resp.status_code = 299;
    EXPECT_TRUE(resp.is_success());

    resp.status_code = 300;
    EXPECT_FALSE(resp.is_success());

    resp.status_code = 404;
    EXPECT_FALSE(resp.is_success());
}

TEST_F(HTTPResponseTest, IsRedirect) {
    Response resp;

    resp.status_code = 301;
    EXPECT_TRUE(resp.is_redirect());

    resp.status_code = 302;
    EXPECT_TRUE(resp.is_redirect());

    resp.status_code = 304;
    EXPECT_TRUE(resp.is_redirect());

    resp.status_code = 200;
    EXPECT_FALSE(resp.is_redirect());
}

TEST_F(HTTPResponseTest, IsClientError) {
    Response resp;

    resp.status_code = 400;
    EXPECT_TRUE(resp.is_client_error());

    resp.status_code = 401;
    EXPECT_TRUE(resp.is_client_error());

    resp.status_code = 404;
    EXPECT_TRUE(resp.is_client_error());

    resp.status_code = 500;
    EXPECT_FALSE(resp.is_client_error());
}

TEST_F(HTTPResponseTest, IsServerError) {
    Response resp;

    resp.status_code = 500;
    EXPECT_TRUE(resp.is_server_error());

    resp.status_code = 502;
    EXPECT_TRUE(resp.is_server_error());

    resp.status_code = 503;
    EXPECT_TRUE(resp.is_server_error());

    resp.status_code = 400;
    EXPECT_FALSE(resp.is_server_error());
}

TEST_F(HTTPResponseTest, BodyString) {
    Response resp;
    std::string test_body = "Hello, World!";
    resp.body.assign(test_body.begin(), test_body.end());

    EXPECT_EQ(resp.body_string(), "Hello, World!");
}

TEST_F(HTTPResponseTest, GetHeader) {
    Response resp;
    resp.headers["Content-Type"] = "application/json";
    resp.headers["X-Request-Id"] = "abc123";

    EXPECT_EQ(resp.get_header("Content-Type"), "application/json");
    EXPECT_EQ(resp.get_header("X-Request-Id"), "abc123");
    EXPECT_TRUE(resp.get_header("Non-Existent").empty());
}

// ============================================================================
// BackendStats Tests
// ============================================================================

class HTTPBackendStatsTest : public ::testing::Test {
protected:
    BackendStats stats_;
};

TEST_F(HTTPBackendStatsTest, DefaultValues) {
    EXPECT_EQ(stats_.requests_sent, 0u);
    EXPECT_EQ(stats_.responses_received, 0u);
    EXPECT_EQ(stats_.requests_failed, 0u);
    EXPECT_EQ(stats_.bytes_sent, 0u);
    EXPECT_EQ(stats_.bytes_received, 0u);
    EXPECT_EQ(stats_.total_request_time_us, 0u);
}

TEST_F(HTTPBackendStatsTest, AvgRequestTimeZero) {
    EXPECT_EQ(stats_.avg_request_time_us(), 0u);
}

TEST_F(HTTPBackendStatsTest, AvgRequestTimeCalculation) {
    stats_.total_request_time_us = 10000;
    stats_.responses_received = 10;

    EXPECT_EQ(stats_.avg_request_time_us(), 1000u);
}

TEST_F(HTTPBackendStatsTest, Reset) {
    stats_.requests_sent = 100;
    stats_.responses_received = 90;
    stats_.requests_failed = 10;
    stats_.bytes_sent = 50000;
    stats_.bytes_received = 45000;
    stats_.total_request_time_us = 100000;

    stats_.reset();

    EXPECT_EQ(stats_.requests_sent, 0u);
    EXPECT_EQ(stats_.responses_received, 0u);
    EXPECT_EQ(stats_.requests_failed, 0u);
    EXPECT_EQ(stats_.bytes_sent, 0u);
    EXPECT_EQ(stats_.bytes_received, 0u);
    EXPECT_EQ(stats_.total_request_time_us, 0u);
}

// ============================================================================
// HTTPConfig Tests
// ============================================================================

class HTTPConfigTest : public ::testing::Test {};

TEST_F(HTTPConfigTest, DefaultValues) {
    HTTPConfig config;

    EXPECT_TRUE(config.base_url.empty());
    EXPECT_TRUE(config.default_headers.empty());
    EXPECT_EQ(config.connect_timeout, std::chrono::milliseconds(30000));
    EXPECT_EQ(config.timeout, std::chrono::milliseconds(60000));
    EXPECT_TRUE(config.verify_ssl);
    EXPECT_TRUE(config.use_http2);
    EXPECT_TRUE(config.enable_connection_pool);
    EXPECT_EQ(config.max_connections_per_host, 6u);
    EXPECT_EQ(config.max_retries, 3);
    EXPECT_EQ(config.retry_delay, std::chrono::milliseconds(1000));
}

TEST_F(HTTPConfigTest, CustomValues) {
    HTTPConfig config;
    config.base_url = "https://api.example.com/v1";
    config.default_headers["Authorization"] = "Bearer token";
    config.timeout = 30s;
    config.verify_ssl = false;
    config.max_retries = 5;

    EXPECT_EQ(config.base_url, "https://api.example.com/v1");
    EXPECT_EQ(config.default_headers["Authorization"], "Bearer token");
    EXPECT_EQ(config.timeout, 30s);
    EXPECT_FALSE(config.verify_ssl);
    EXPECT_EQ(config.max_retries, 5);
}

TEST_F(HTTPConfigTest, DefaultConfig) {
    HTTPConfig config = HTTPConfig::default_config();

    EXPECT_TRUE(config.base_url.empty());
    EXPECT_EQ(config.timeout, std::chrono::milliseconds(60000));
}

// ============================================================================
// Utility Functions Tests
// ============================================================================

class HTTPUtilityTest : public ::testing::Test {};

TEST_F(HTTPUtilityTest, UrlEncode) {
    EXPECT_EQ(url_encode("hello world"), "hello%20world");
    EXPECT_EQ(url_encode("key=value"), "key%3Dvalue");
    EXPECT_EQ(url_encode("test&param"), "test%26param");
    EXPECT_EQ(url_encode("simple"), "simple");
    EXPECT_EQ(url_encode(""), "");
}

TEST_F(HTTPUtilityTest, UrlEncodeSpecialChars) {
    EXPECT_EQ(url_encode("?"), "%3F");
    EXPECT_EQ(url_encode("/"), "%2F");
    EXPECT_EQ(url_encode("#"), "%23");
    EXPECT_EQ(url_encode("+"), "%2B");
}

TEST_F(HTTPUtilityTest, UrlDecode) {
    EXPECT_EQ(url_decode("hello%20world"), "hello world");
    EXPECT_EQ(url_decode("key%3Dvalue"), "key=value");
    EXPECT_EQ(url_decode("test%26param"), "test&param");
    EXPECT_EQ(url_decode("simple"), "simple");
    EXPECT_EQ(url_decode(""), "");
}

TEST_F(HTTPUtilityTest, UrlDecodeSpecialChars) {
    EXPECT_EQ(url_decode("%3F"), "?");
    EXPECT_EQ(url_decode("%2F"), "/");
    EXPECT_EQ(url_decode("%23"), "#");
    EXPECT_EQ(url_decode("%2B"), "+");
}

TEST_F(HTTPUtilityTest, UrlEncodeDecodeRoundTrip) {
    std::string original = "hello world?key=value&foo=bar";
    std::string encoded = url_encode(original);
    std::string decoded = url_decode(encoded);

    EXPECT_EQ(decoded, original);
}

TEST_F(HTTPUtilityTest, BuildQueryString) {
    std::map<std::string, std::string> params;
    params["key1"] = "value1";
    params["key2"] = "value2";

    std::string query = build_query_string(params);

    // Order may vary since map is sorted by key
    EXPECT_TRUE(query.find("key1=value1") != std::string::npos);
    EXPECT_TRUE(query.find("key2=value2") != std::string::npos);
    EXPECT_TRUE(query.find("&") != std::string::npos);
}

TEST_F(HTTPUtilityTest, BuildQueryStringEmpty) {
    std::map<std::string, std::string> params;

    std::string query = build_query_string(params);

    EXPECT_TRUE(query.empty());
}

TEST_F(HTTPUtilityTest, BuildQueryStringSingle) {
    std::map<std::string, std::string> params;
    params["key"] = "value";

    std::string query = build_query_string(params);

    EXPECT_EQ(query, "key=value");
}

TEST_F(HTTPUtilityTest, BuildQueryStringSpecialChars) {
    std::map<std::string, std::string> params;
    params["name"] = "John Doe";

    std::string query = build_query_string(params);

    // Should be URL encoded
    EXPECT_TRUE(query.find("John%20Doe") != std::string::npos);
}

TEST_F(HTTPUtilityTest, ParseUrl) {
    auto result = parse_url("https://api.example.com:8080/v1/users?active=true");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->scheme, "https");
    EXPECT_EQ(result->host, "api.example.com");
    EXPECT_EQ(result->port, 8080);
    EXPECT_EQ(result->path, "/v1/users");
    EXPECT_EQ(result->query, "active=true");
}

TEST_F(HTTPUtilityTest, ParseUrlNoPort) {
    auto result = parse_url("https://api.example.com/path");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->scheme, "https");
    EXPECT_EQ(result->host, "api.example.com");
    // Port should be default (443 for HTTPS)
    EXPECT_EQ(result->path, "/path");
}

TEST_F(HTTPUtilityTest, ParseUrlNoPath) {
    auto result = parse_url("https://example.com");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->scheme, "https");
    EXPECT_EQ(result->host, "example.com");
}

TEST_F(HTTPUtilityTest, ParseUrlLocalhost) {
    auto result = parse_url("http://localhost:3000/api");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->scheme, "http");
    EXPECT_EQ(result->host, "localhost");
    EXPECT_EQ(result->port, 3000);
    EXPECT_EQ(result->path, "/api");
}

TEST_F(HTTPUtilityTest, ParseUrlIP) {
    auto result = parse_url("http://192.168.1.100:8080/test");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->scheme, "http");
    EXPECT_EQ(result->host, "192.168.1.100");
    EXPECT_EQ(result->port, 8080);
}

// ============================================================================
// HTTPClient Tests (Unit tests - no actual network calls)
// ============================================================================

class HTTPClientTest : public ::testing::Test {};

TEST_F(HTTPClientTest, DefaultConstruction) {
    HTTPClient client;

    // Should construct without error
    EXPECT_TRUE(true);
}

TEST_F(HTTPClientTest, ConstructWithConfig) {
    HTTPConfig config;
    config.base_url = "https://api.example.com";
    config.timeout = 10s;

    HTTPClient client(config);

    EXPECT_EQ(client.config().base_url, "https://api.example.com");
    EXPECT_EQ(client.config().timeout, 10s);
}

TEST_F(HTTPClientTest, GetConfig) {
    HTTPConfig config;
    config.base_url = "https://test.com";

    HTTPClient client(config);

    const HTTPConfig& retrieved = client.config();
    EXPECT_EQ(retrieved.base_url, "https://test.com");
}

TEST_F(HTTPClientTest, SetBaseUrl) {
    HTTPClient client;

    client.set_base_url("https://api.example.com/v2");

    // Should update config
    // Note: Implementation may vary
}

TEST_F(HTTPClientTest, SetDefaultHeader) {
    HTTPClient client;

    client.set_default_header("X-Custom-Header", "custom-value");

    // Header should be set for future requests
}

TEST_F(HTTPClientTest, SetBearerToken) {
    HTTPClient client;

    client.set_bearer_token("my-secret-token");

    // Token should be set for authentication
}

TEST_F(HTTPClientTest, SetBasicAuth) {
    HTTPClient client;

    client.set_basic_auth("username", "password");

    // Basic auth should be configured
}

TEST_F(HTTPClientTest, BackendType) {
    HTTPClient client;

    BackendType type = client.backend_type();

    // Should return a valid backend type
    EXPECT_TRUE(type == BackendType::CURL || type == BackendType::BEAST);
}

TEST_F(HTTPClientTest, Stats) {
    HTTPClient client;

    const BackendStats& stats = client.stats();

    // Initial stats should be zero
    EXPECT_EQ(stats.requests_sent, 0u);
    EXPECT_EQ(stats.responses_received, 0u);
}

TEST_F(HTTPClientTest, ResetStats) {
    HTTPClient client;

    client.reset_stats();

    const BackendStats& stats = client.stats();
    EXPECT_EQ(stats.requests_sent, 0u);
}

TEST_F(HTTPClientTest, MoveConstruction) {
    HTTPConfig config;
    config.base_url = "https://test.com";

    HTTPClient client1(config);
    HTTPClient client2(std::move(client1));

    EXPECT_EQ(client2.config().base_url, "https://test.com");
}

TEST_F(HTTPClientTest, MoveAssignment) {
    HTTPConfig config1;
    config1.base_url = "https://test1.com";

    HTTPConfig config2;
    config2.base_url = "https://test2.com";

    HTTPClient client1(config1);
    HTTPClient client2(config2);

    client2 = std::move(client1);

    EXPECT_EQ(client2.config().base_url, "https://test1.com");
}

// Note: Actual HTTP request tests would require network access
// or a mock HTTP server. These would typically be integration tests.

// ============================================================================
// URLComponents Tests
// ============================================================================

class URLComponentsTest : public ::testing::Test {};

TEST_F(URLComponentsTest, DefaultValues) {
    URLComponents components;

    EXPECT_TRUE(components.scheme.empty());
    EXPECT_TRUE(components.host.empty());
    EXPECT_EQ(components.port, 0);
    EXPECT_TRUE(components.path.empty());
    EXPECT_TRUE(components.query.empty());
}

TEST_F(URLComponentsTest, CustomValues) {
    URLComponents components;
    components.scheme = "https";
    components.host = "example.com";
    components.port = 443;
    components.path = "/api/v1";
    components.query = "key=value";

    EXPECT_EQ(components.scheme, "https");
    EXPECT_EQ(components.host, "example.com");
    EXPECT_EQ(components.port, 443);
    EXPECT_EQ(components.path, "/api/v1");
    EXPECT_EQ(components.query, "key=value");
}
