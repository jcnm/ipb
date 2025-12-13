/**
 * @file http_client.cpp
 * @brief HTTP client implementation
 */

#include "ipb/transport/http/http_client.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

#include "ipb/transport/http/backends/curl_backend.hpp"

namespace ipb::transport::http {

//=============================================================================
// HTTPClient Implementation
//=============================================================================

class HTTPClient::Impl {
public:
    explicit Impl(const HTTPConfig& config)
        : config_(config), backend_(create_backend(config.backend)) {
        if (!backend_) {
            // Fallback to default backend
            backend_ = create_backend(default_backend_type());
        }
    }

    Response execute(const Request& req) {
        Request full_req = prepare_request(req);
        return backend_ ? backend_->execute(full_req)
                        : Response{.status_code = 0, .error_message = "No backend"};
    }

    void execute_async(const Request& req, ResponseCallback callback) {
        Request full_req = prepare_request(req);
        if (backend_) {
            backend_->execute_async(full_req, std::move(callback));
        } else if (callback) {
            callback(Response{.status_code = 0, .error_message = "No backend"});
        }
    }

    Request prepare_request(const Request& req) {
        Request full_req = req;

        // Add base URL if path is relative
        if (!config_.base_url.empty() && !req.url.empty() && req.url[0] == '/') {
            full_req.url = config_.base_url + req.url;
        }

        // Add default headers
        for (const auto& [name, value] : config_.default_headers) {
            if (full_req.headers.find(name) == full_req.headers.end()) {
                full_req.headers[name] = value;
            }
        }

        // Add authentication
        if (!config_.bearer_token.empty() &&
            full_req.headers.find("Authorization") == full_req.headers.end()) {
            full_req.headers["Authorization"] = "Bearer " + config_.bearer_token;
        }

        // Set TLS options from config if not overridden
        if (full_req.ca_cert_path.empty()) {
            full_req.ca_cert_path = config_.ca_cert_path;
        }
        if (full_req.client_cert_path.empty()) {
            full_req.client_cert_path = config_.client_cert_path;
        }
        if (full_req.client_key_path.empty()) {
            full_req.client_key_path = config_.client_key_path;
        }

        // Apply timeouts
        if (full_req.connect_timeout.count() == 0) {
            full_req.connect_timeout = config_.connect_timeout;
        }
        if (full_req.timeout.count() == 0) {
            full_req.timeout = config_.timeout;
        }

        full_req.verify_ssl = config_.verify_ssl;
        full_req.use_http2  = config_.use_http2;

        return full_req;
    }

    HTTPConfig config_;
    std::unique_ptr<IHTTPBackend> backend_;
};

HTTPClient::HTTPClient() : HTTPClient(HTTPConfig::default_config()) {}

HTTPClient::HTTPClient(const HTTPConfig& config) : impl_(std::make_unique<Impl>(config)) {}

HTTPClient::~HTTPClient() = default;

HTTPClient::HTTPClient(HTTPClient&&) noexcept            = default;
HTTPClient& HTTPClient::operator=(HTTPClient&&) noexcept = default;

const HTTPConfig& HTTPClient::config() const noexcept {
    return impl_->config_;
}

void HTTPClient::set_base_url(const std::string& url) {
    impl_->config_.base_url = url;
}

void HTTPClient::set_default_header(const std::string& name, const std::string& value) {
    impl_->config_.default_headers[name] = value;
}

void HTTPClient::set_bearer_token(const std::string& token) {
    impl_->config_.bearer_token = token;
}

void HTTPClient::set_basic_auth(const std::string& username, const std::string& password) {
    impl_->config_.basic_auth_user     = username;
    impl_->config_.basic_auth_password = password;
    // Add Basic auth header
    std::string credentials = username + ":" + password;
    // Base64 encode would go here - simplified for now
    impl_->config_.default_headers["Authorization"] = "Basic " + credentials;
}

BackendType HTTPClient::backend_type() const noexcept {
    return impl_->backend_ ? impl_->backend_->type() : BackendType::CURL;
}

Response HTTPClient::get(const std::string& url) {
    return get(url, {});
}

Response HTTPClient::get(const std::string& url, const Headers& headers) {
    Request req;
    req.method  = Method::GET;
    req.url     = url;
    req.headers = headers;
    return execute(req);
}

Response HTTPClient::post(const std::string& url, std::string_view body) {
    Request req;
    req.method = Method::POST;
    req.url    = url;
    req.set_body(body);
    return execute(req);
}

Response HTTPClient::post_json(const std::string& url, std::string_view json) {
    Request req;
    req.method = Method::POST;
    req.url    = url;
    req.set_json_content();
    req.set_body(json);
    return execute(req);
}

Response HTTPClient::post_form(const std::string& url,
                               const std::map<std::string, std::string>& form_data) {
    Request req;
    req.method = Method::POST;
    req.url    = url;
    req.set_form_content();
    req.set_body(build_query_string(form_data));
    return execute(req);
}

Response HTTPClient::put(const std::string& url, std::string_view body) {
    Request req;
    req.method = Method::PUT;
    req.url    = url;
    req.set_body(body);
    return execute(req);
}

Response HTTPClient::put_json(const std::string& url, std::string_view json) {
    Request req;
    req.method = Method::PUT;
    req.url    = url;
    req.set_json_content();
    req.set_body(json);
    return execute(req);
}

Response HTTPClient::patch(const std::string& url, std::string_view body) {
    Request req;
    req.method = Method::PATCH;
    req.url    = url;
    req.set_body(body);
    return execute(req);
}

Response HTTPClient::delete_(const std::string& url) {
    Request req;
    req.method = Method::DELETE_;
    req.url    = url;
    return execute(req);
}

Response HTTPClient::head(const std::string& url) {
    Request req;
    req.method = Method::HEAD;
    req.url    = url;
    return execute(req);
}

Response HTTPClient::execute(const Request& request) {
    return impl_->execute(request);
}

void HTTPClient::execute_async(const Request& request, ResponseCallback callback) {
    impl_->execute_async(request, std::move(callback));
}

void HTTPClient::close_all() {
    if (impl_->backend_) {
        impl_->backend_->close_all();
    }
}

const BackendStats& HTTPClient::stats() const noexcept {
    static BackendStats empty_stats;
    return impl_->backend_ ? impl_->backend_->stats() : empty_stats;
}

void HTTPClient::reset_stats() {
    if (impl_->backend_) {
        impl_->backend_->reset_stats();
    }
}

//=============================================================================
// Utility Functions
//=============================================================================

std::string url_encode(std::string_view str) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;

    for (char c : str) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
        }
    }

    return encoded.str();
}

std::string url_decode(std::string_view str) {
    std::string decoded;
    decoded.reserve(str.size());

    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int value;
            std::istringstream iss(std::string(str.substr(i + 1, 2)));
            if (iss >> std::hex >> value) {
                decoded += static_cast<char>(value);
                i += 2;
            } else {
                decoded += str[i];
            }
        } else if (str[i] == '+') {
            decoded += ' ';
        } else {
            decoded += str[i];
        }
    }

    return decoded;
}

std::string build_query_string(const std::map<std::string, std::string>& params) {
    std::ostringstream query;
    bool first = true;

    for (const auto& [key, value] : params) {
        if (!first) {
            query << '&';
        }
        first = false;
        query << url_encode(key) << '=' << url_encode(value);
    }

    return query.str();
}

std::optional<URLComponents> parse_url(const std::string& url) {
    URLComponents components;

    // Find scheme
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        return std::nullopt;
    }

    components.scheme = url.substr(0, scheme_end);

    // Find host and port
    auto host_start = scheme_end + 3;
    auto path_start = url.find('/', host_start);
    auto host_end   = (path_start != std::string::npos) ? path_start : url.size();

    std::string host_port = url.substr(host_start, host_end - host_start);

    auto colon = host_port.rfind(':');
    if (colon != std::string::npos && host_port[0] != '[') {
        // Not IPv6, has port
        components.host = host_port.substr(0, colon);
        components.port = static_cast<uint16_t>(std::stoi(host_port.substr(colon + 1)));
    } else {
        components.host = host_port;
        // Default ports
        if (components.scheme == "http") {
            components.port = 80;
        } else if (components.scheme == "https") {
            components.port = 443;
        }
    }

    // Find path and query
    if (path_start != std::string::npos) {
        auto query_start = url.find('?', path_start);
        if (query_start != std::string::npos) {
            components.path  = url.substr(path_start, query_start - path_start);
            components.query = url.substr(query_start + 1);
        } else {
            components.path = url.substr(path_start);
        }
    } else {
        components.path = "/";
    }

    return components;
}

}  // namespace ipb::transport::http
