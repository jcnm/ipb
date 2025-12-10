#include <ipb/router/routing_rule.hpp>
#include <regex>

namespace ipb {
namespace router {

RoutingRule::RoutingRule(const std::string& name)
    : name_(name)
    , enabled_(true)
{
}

bool RoutingRule::matches(const ipb::common::DataPoint& data_point) const {
    if (!enabled_) {
        return false;
    }
    
    // Check address pattern
    if (!address_pattern_.empty()) {
        try {
            std::regex pattern(address_pattern_);
            if (!std::regex_match(data_point.get_address(), pattern)) {
                return false;
            }
        } catch (const std::regex_error&) {
            return false;
        }
    }
    
    // Check protocol filter
    if (!protocol_filter_.empty()) {
        bool protocol_match = false;
        for (const auto& protocol : protocol_filter_) {
            if (data_point.get_protocol_id() == protocol) {
                protocol_match = true;
                break;
            }
        }
        if (!protocol_match) {
            return false;
        }
    }
    
    // Check quality filter
    if (!quality_filter_.empty()) {
        bool quality_match = false;
        for (const auto& quality : quality_filter_) {
            if (data_point.get_quality() == quality) {
                quality_match = true;
                break;
            }
        }
        if (!quality_match) {
            return false;
        }
    }
    
    return true;
}

void RoutingRule::set_address_pattern(const std::string& pattern) {
    address_pattern_ = pattern;
}

void RoutingRule::set_protocol_filter(const std::vector<std::string>& protocols) {
    protocol_filter_ = protocols;
}

void RoutingRule::set_quality_filter(const std::vector<ipb::common::DataQuality>& qualities) {
    quality_filter_ = qualities;
}

void RoutingRule::add_destination(const std::string& sink_id, RoutingPriority priority) {
    destinations_.emplace_back(sink_id, priority);
}

void RoutingRule::remove_destination(const std::string& sink_id) {
    destinations_.erase(
        std::remove_if(destinations_.begin(), destinations_.end(),
            [&sink_id](const RoutingDestination& dest) {
                return dest.sink_id == sink_id;
            }),
        destinations_.end()
    );
}

std::vector<RoutingDestination> RoutingRule::get_destinations() const {
    return destinations_;
}

void RoutingRule::set_enabled(bool enabled) {
    enabled_ = enabled;
}

bool RoutingRule::is_enabled() const {
    return enabled_;
}

std::string RoutingRule::get_name() const {
    return name_;
}

} // namespace router
} // namespace ipb

