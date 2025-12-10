#include <ipb/common/endpoint.hpp>

namespace ipb {
namespace common {

EndPoint::EndPoint(const std::string& address, const std::string& protocol_id)
    : address_(address)
    , protocol_id_(protocol_id)
    , enabled_(true)
{
}

std::string EndPoint::get_address() const {
    return address_;
}

std::string EndPoint::get_protocol_id() const {
    return protocol_id_;
}

bool EndPoint::is_enabled() const {
    return enabled_;
}

void EndPoint::set_enabled(bool enabled) {
    enabled_ = enabled;
}

void EndPoint::set_metadata(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    metadata_[key] = value;
}

std::string EndPoint::get_metadata(const std::string& key) const {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    auto it = metadata_.find(key);
    return (it != metadata_.end()) ? it->second : "";
}

std::map<std::string, std::string> EndPoint::get_all_metadata() const {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    return metadata_;
}

bool EndPoint::has_metadata(const std::string& key) const {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    return metadata_.find(key) != metadata_.end();
}

void EndPoint::clear_metadata() {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    metadata_.clear();
}

} // namespace common
} // namespace ipb

