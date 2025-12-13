/**
 * @file backend_factory.cpp
 * @brief HTTP backend factory implementation
 */

#include "ipb/transport/http/backends/curl_backend.hpp"
#include "ipb/transport/http/http_backend.hpp"

namespace ipb::transport::http {

std::unique_ptr<IHTTPBackend> create_backend(BackendType type) {
    switch (type) {
        case BackendType::CURL:
#ifdef IPB_HAS_CURL
            return std::make_unique<CurlBackend>();
#else
            return nullptr;
#endif

        case BackendType::BEAST:
            // Boost.Beast backend would go here
            return nullptr;

        case BackendType::NATIVE:
            // Native backend would go here
            return nullptr;

        default:
            return nullptr;
    }
}

BackendType default_backend_type() noexcept {
#ifdef IPB_HAS_CURL
    return BackendType::CURL;
#elif defined(IPB_HAS_BEAST)
    return BackendType::BEAST;
#else
    return BackendType::CURL;  // Will return nullptr from factory
#endif
}

bool is_backend_available(BackendType type) noexcept {
    switch (type) {
        case BackendType::CURL:
#ifdef IPB_HAS_CURL
            return true;
#else
            return false;
#endif

        case BackendType::BEAST:
#ifdef IPB_HAS_BEAST
            return true;
#else
            return false;
#endif

        case BackendType::NATIVE:
            return false;

        default:
            return false;
    }
}

}  // namespace ipb::transport::http
