#include <ipb/common/interfaces.hpp>

namespace ipb {
namespace common {

// Base implementations for interfaces

SinkMetrics::SinkMetrics() 
    : messages_sent(0)
    , messages_failed(0)
    , total_bytes_sent(0)
    , average_latency_ms(0.0)
    , is_connected(false)
{
}

void SinkMetrics::reset() {
    messages_sent = 0;
    messages_failed = 0;
    total_bytes_sent = 0;
    average_latency_ms = 0.0;
    is_connected = false;
}

// IIPBSinkBase default implementations
IIPBSinkBase::~IIPBSinkBase() = default;

bool IIPBSinkBase::is_running() const {
    return false; // Default implementation
}

SinkMetrics IIPBSinkBase::get_statistics() const {
    return SinkMetrics{}; // Default empty metrics
}

// IIPBSink default implementations  
IIPBSink::~IIPBSink() = default;

bool IIPBSink::send_batch(const std::vector<DataPoint>& data_points) {
    // Default implementation: send individually
    bool all_success = true;
    for (const auto& dp : data_points) {
        if (!send(dp)) {
            all_success = false;
        }
    }
    return all_success;
}

// IProtocolSourceBase default implementations
IProtocolSourceBase::~IProtocolSourceBase() = default;

bool IProtocolSourceBase::is_running() const {
    return false; // Default implementation
}

// IProtocolSource default implementations
IProtocolSource::~IProtocolSource() = default;

std::vector<DataPoint> IProtocolSource::read_batch(size_t max_count) {
    // Default implementation: read individually
    std::vector<DataPoint> batch;
    batch.reserve(max_count);
    
    for (size_t i = 0; i < max_count; ++i) {
        auto dp = read();
        if (dp.get_address().empty()) {
            break; // No more data
        }
        batch.push_back(dp);
    }
    
    return batch;
}

} // namespace common
} // namespace ipb

