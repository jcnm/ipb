#include "ipb/transport/mqtt/backends/mqtt_backend.hpp"
#include "ipb/transport/mqtt/backends/paho_backend.hpp"
#include "ipb/transport/mqtt/backends/coremqtt_backend.hpp"

namespace ipb::transport::mqtt {

std::unique_ptr<IMQTTBackend> create_backend(BackendType type) {
    switch (type) {
        case BackendType::PAHO:
#ifdef IPB_HAS_PAHO
            return std::make_unique<PahoBackend>();
#else
            return nullptr;
#endif

        case BackendType::COREMQTT:
#ifdef IPB_HAS_COREMQTT
            return std::make_unique<CoreMQTTBackend>();
#else
            // Return stub that reports unavailable
            return std::make_unique<CoreMQTTBackend>();
#endif

        case BackendType::NATIVE:
            // Not implemented yet
            return nullptr;

        default:
            return nullptr;
    }
}

BackendType default_backend_type() noexcept {
#if defined(IPB_DEFAULT_MQTT_BACKEND_COREMQTT) && defined(IPB_HAS_COREMQTT)
    return BackendType::COREMQTT;
#elif defined(IPB_HAS_PAHO)
    return BackendType::PAHO;
#elif defined(IPB_HAS_COREMQTT)
    return BackendType::COREMQTT;
#else
    return BackendType::PAHO;  // Will fail at runtime if not available
#endif
}

bool is_backend_available(BackendType type) noexcept {
    switch (type) {
        case BackendType::PAHO:
#ifdef IPB_HAS_PAHO
            return true;
#else
            return false;
#endif

        case BackendType::COREMQTT:
#ifdef IPB_HAS_COREMQTT
            return true;
#else
            return false;
#endif

        case BackendType::NATIVE:
            return false;  // Not implemented yet

        default:
            return false;
    }
}

} // namespace ipb::transport::mqtt
