#pragma once

/**
 * @file curl_backend.hpp
 * @brief libcurl HTTP backend implementation
 *
 * Default HTTP backend using libcurl. Features:
 * - HTTP/1.1 and HTTP/2 support
 * - TLS with system CA bundle
 * - Connection pooling
 * - Cookie handling
 * - Automatic decompression
 */

#include "../http_backend.hpp"

#include <memory>
#include <mutex>
#include <atomic>

namespace ipb::transport::http {

/**
 * @brief libcurl HTTP Backend
 */
class CurlBackend : public IHTTPBackend {
public:
    CurlBackend();
    ~CurlBackend() override;

    // Non-copyable, non-movable
    CurlBackend(const CurlBackend&) = delete;
    CurlBackend& operator=(const CurlBackend&) = delete;

    //=========================================================================
    // IHTTPBackend Implementation
    //=========================================================================

    BackendType type() const noexcept override { return BackendType::CURL; }
    std::string_view name() const noexcept override { return "libcurl"; }
    std::string_view version() const noexcept override;
    bool supports_http2() const noexcept override;

    Response execute(const Request& request) override;
    void execute_async(const Request& request, ResponseCallback callback) override;

    void close_all() override;
    void set_progress_callback(ProgressCallback callback) override;

    const BackendStats& stats() const noexcept override { return stats_; }
    void reset_stats() noexcept override { stats_.reset(); }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    mutable BackendStats stats_;
    ProgressCallback progress_cb_;
    std::mutex mutex_;
};

} // namespace ipb::transport::http
