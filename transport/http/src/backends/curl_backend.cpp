/**
 * @file curl_backend.cpp
 * @brief libcurl HTTP backend implementation
 */

#include "ipb/transport/http/backends/curl_backend.hpp"

#ifdef IPB_HAS_CURL

#include <chrono>
#include <future>
#include <thread>

#include <curl/curl.h>

namespace ipb::transport::http {

//=============================================================================
// CURL Callbacks
//=============================================================================

namespace {

struct WriteCallbackData {
    std::vector<uint8_t>* buffer;
};

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* data        = static_cast<WriteCallbackData*>(userdata);
    size_t total_size = size * nmemb;
    data->buffer->insert(data->buffer->end(), ptr, ptr + total_size);
    return total_size;
}

struct HeaderCallbackData {
    Headers* headers;
};

size_t header_callback(char* buffer, size_t size, size_t nmemb, void* userdata) {
    auto* data        = static_cast<HeaderCallbackData*>(userdata);
    size_t total_size = size * nmemb;

    std::string line(buffer, total_size);

    // Remove trailing CRLF
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }

    // Parse header line
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string name  = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // Trim whitespace
        while (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }

        (*data->headers)[name] = value;
    }

    return total_size;
}

}  // anonymous namespace

//=============================================================================
// CurlBackend Implementation
//=============================================================================

class CurlBackend::Impl {
public:
    Impl() {
        // Global curl initialization
        static bool curl_initialized = false;
        if (!curl_initialized) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curl_initialized = true;
        }
    }

    ~Impl() {
        // Note: curl_global_cleanup() should only be called once
        // at program exit, not here
    }

    Response execute(const Request& request, ProgressCallback& progress_cb, BackendStats& stats) {
        (void)progress_cb;  // TODO: Implement progress callback support
        Response response;

        CURL* curl = curl_easy_init();
        if (!curl) {
            response.error_message = "Failed to initialize CURL";
            return response;
        }

        // Setup URL
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());

        // Setup method
        switch (request.method) {
            case Method::GET:
                curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
                break;
            case Method::POST:
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                break;
            case Method::PUT:
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
                break;
            case Method::PATCH:
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
                break;
            case Method::DELETE_:
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
                break;
            case Method::HEAD:
                curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
                break;
            case Method::OPTIONS:
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
                break;
        }

        // Setup body
        if (!request.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.size());
        }

        // Setup headers
        struct curl_slist* headers = nullptr;
        for (const auto& [name, value] : request.headers) {
            std::string header = name + ": " + value;
            headers            = curl_slist_append(headers, header.c_str());
        }
        if (headers) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        // Setup timeouts
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                         static_cast<long>(request.connect_timeout.count()));
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout.count()));

        // Setup TLS
        if (request.verify_ssl) {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        } else {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        }

        if (!request.ca_cert_path.empty()) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, request.ca_cert_path.c_str());
        }

        if (!request.client_cert_path.empty()) {
            curl_easy_setopt(curl, CURLOPT_SSLCERT, request.client_cert_path.c_str());
        }

        if (!request.client_key_path.empty()) {
            curl_easy_setopt(curl, CURLOPT_SSLKEY, request.client_key_path.c_str());
        }

        // Setup redirects
        if (request.follow_redirects) {
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, static_cast<long>(request.max_redirects));
        }

        // Setup HTTP/2
        if (request.use_http2) {
            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
        }

        // Setup response body callback
        WriteCallbackData body_data{&response.body};
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body_data);

        // Setup header callback
        HeaderCallbackData header_data{&response.headers};
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_data);

        // Execute request
        auto start = std::chrono::steady_clock::now();
        stats.requests_sent++;
        stats.bytes_sent += request.body.size();

        CURLcode res = curl_easy_perform(curl);

        auto end            = std::chrono::steady_clock::now();
        auto duration       = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        response.total_time = duration;

        if (res == CURLE_OK) {
            // Get response info
            long status_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
            response.status_code = static_cast<int>(status_code);

            double connect_time = 0;
            curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_time);
            response.connect_time =
                std::chrono::microseconds(static_cast<long long>(connect_time * 1000000));

            stats.responses_received++;
            stats.bytes_received += response.body.size();
            stats.total_request_time_us += duration.count();
        } else {
            response.error_message = curl_easy_strerror(res);
            stats.requests_failed++;
        }

        // Cleanup
        if (headers) {
            curl_slist_free_all(headers);
        }
        curl_easy_cleanup(curl);

        return response;
    }
};

CurlBackend::CurlBackend() : impl_(std::make_unique<Impl>()) {}

CurlBackend::~CurlBackend() = default;

std::string_view CurlBackend::version() const noexcept {
    static std::string version = curl_version();
    return version;
}

bool CurlBackend::supports_http2() const noexcept {
    curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
    return (info->features & CURL_VERSION_HTTP2) != 0;
}

Response CurlBackend::execute(const Request& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    return impl_->execute(request, progress_cb_, stats_);
}

void CurlBackend::execute_async(const Request& request, ResponseCallback callback) {
    std::thread([this, request, callback = std::move(callback)]() {
        auto response = execute(request);
        if (callback) {
            callback(std::move(response));
        }
    }).detach();
}

void CurlBackend::close_all() {
    // libcurl handles connection pooling internally
}

void CurlBackend::set_progress_callback(ProgressCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_cb_ = std::move(callback);
}

}  // namespace ipb::transport::http

#else  // !IPB_HAS_CURL

// Stub implementation when curl is not available
namespace ipb::transport::http {

class CurlBackend::Impl {};

CurlBackend::CurlBackend() : impl_(nullptr) {}
CurlBackend::~CurlBackend() = default;

std::string_view CurlBackend::version() const noexcept {
    return "not available";
}

bool CurlBackend::supports_http2() const noexcept {
    return false;
}

Response CurlBackend::execute(const Request&) {
    Response response;
    response.error_message = "CURL backend not available (compile with -DIPB_HAS_CURL=1)";
    return response;
}

void CurlBackend::execute_async(const Request&, ResponseCallback callback) {
    if (callback) {
        Response response;
        response.error_message = "CURL backend not available";
        callback(std::move(response));
    }
}

void CurlBackend::close_all() {}

void CurlBackend::set_progress_callback(ProgressCallback) {}

}  // namespace ipb::transport::http

#endif  // IPB_HAS_CURL
