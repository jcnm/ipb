/**
 * @file sparkplug_topic.cpp
 * @brief Sparkplug B topic parsing and building implementation
 */

#include <algorithm>
#include <sstream>

#include "ipb/scoop/sparkplug/sparkplug_scoop.hpp"

namespace ipb::scoop::sparkplug {

//=============================================================================
// MessageType Conversion
//=============================================================================

std::string_view message_type_to_string(MessageType type) {
    switch (type) {
        case MessageType::NBIRTH:
            return "NBIRTH";
        case MessageType::NDEATH:
            return "NDEATH";
        case MessageType::NDATA:
            return "NDATA";
        case MessageType::NCMD:
            return "NCMD";
        case MessageType::DBIRTH:
            return "DBIRTH";
        case MessageType::DDEATH:
            return "DDEATH";
        case MessageType::DDATA:
            return "DDATA";
        case MessageType::DCMD:
            return "DCMD";
        case MessageType::STATE:
            return "STATE";
        default:
            return "UNKNOWN";
    }
}

MessageType string_to_message_type(std::string_view str) {
    if (str == "NBIRTH")
        return MessageType::NBIRTH;
    if (str == "NDEATH")
        return MessageType::NDEATH;
    if (str == "NDATA")
        return MessageType::NDATA;
    if (str == "NCMD")
        return MessageType::NCMD;
    if (str == "DBIRTH")
        return MessageType::DBIRTH;
    if (str == "DDEATH")
        return MessageType::DDEATH;
    if (str == "DDATA")
        return MessageType::DDATA;
    if (str == "DCMD")
        return MessageType::DCMD;
    if (str == "STATE")
        return MessageType::STATE;
    return MessageType::UNKNOWN;
}

//=============================================================================
// SparkplugTopic Implementation
//=============================================================================

std::optional<SparkplugTopic> SparkplugTopic::parse(const std::string& topic) {
    // Sparkplug B topic format:
    // spBv1.0/{group_id}/{message_type}/{edge_node_id}[/{device_id}]
    // or for STATE: STATE/{host_id}

    std::vector<std::string> parts;
    std::stringstream ss(topic);
    std::string part;

    while (std::getline(ss, part, '/')) {
        parts.push_back(part);
    }

    // Handle STATE messages (special format)
    if (parts.size() >= 2 && parts[0] == "STATE") {
        SparkplugTopic result;
        result.message_type = MessageType::STATE;
        result.edge_node_id = parts[1];  // Actually host_id for STATE
        return result;
    }

    // Standard Sparkplug topic: spBv1.0/{group}/{type}/{node}[/{device}]
    if (parts.size() < 4) {
        return std::nullopt;
    }

    // Validate namespace
    if (parts[0] != "spBv1.0") {
        return std::nullopt;
    }

    SparkplugTopic result;
    result.group_id     = parts[1];
    result.message_type = string_to_message_type(parts[2]);
    result.edge_node_id = parts[3];

    // Device ID is optional (only for device-level messages)
    if (parts.size() >= 5) {
        result.device_id = parts[4];
    }

    return result;
}

std::string SparkplugTopic::to_string() const {
    if (message_type == MessageType::STATE) {
        return "STATE/" + edge_node_id;
    }

    std::string topic = "spBv1.0/" + group_id + "/" +
                        std::string(message_type_to_string(message_type)) + "/" + edge_node_id;

    if (!device_id.empty()) {
        topic += "/" + device_id;
    }

    return topic;
}

bool SparkplugTopic::is_node_message() const {
    switch (message_type) {
        case MessageType::NBIRTH:
        case MessageType::NDEATH:
        case MessageType::NDATA:
        case MessageType::NCMD:
            return true;
        default:
            return false;
    }
}

bool SparkplugTopic::is_device_message() const {
    switch (message_type) {
        case MessageType::DBIRTH:
        case MessageType::DDEATH:
        case MessageType::DDATA:
        case MessageType::DCMD:
            return true;
        default:
            return false;
    }
}

bool SparkplugTopic::is_birth() const {
    return message_type == MessageType::NBIRTH || message_type == MessageType::DBIRTH;
}

bool SparkplugTopic::is_death() const {
    return message_type == MessageType::NDEATH || message_type == MessageType::DDEATH;
}

bool SparkplugTopic::is_data() const {
    return message_type == MessageType::NDATA || message_type == MessageType::DDATA;
}

bool SparkplugTopic::is_command() const {
    return message_type == MessageType::NCMD || message_type == MessageType::DCMD;
}

//=============================================================================
// SubscriptionFilter Implementation
//=============================================================================

std::vector<std::string> SubscriptionFilter::to_mqtt_topics() const {
    std::vector<std::string> topics;

    // Build topic patterns based on filters
    if (message_types.empty()) {
        // Subscribe to all message types
        std::string base = "spBv1.0/" + group_id_pattern + "/+/" + edge_node_pattern;

        if (device_pattern.empty() || device_pattern == "#") {
            // All levels (node and device)
            topics.push_back(base);
            topics.push_back(base + "/#");
        } else {
            // Specific device pattern
            topics.push_back(base + "/" + device_pattern);
        }
    } else {
        // Subscribe to specific message types
        for (MessageType type : message_types) {
            std::string topic = "spBv1.0/" + group_id_pattern + "/" +
                                std::string(message_type_to_string(type)) + "/" + edge_node_pattern;

            // Device-level messages need device ID
            if (type == MessageType::DBIRTH || type == MessageType::DDEATH ||
                type == MessageType::DDATA || type == MessageType::DCMD) {
                if (!device_pattern.empty()) {
                    topic += "/" + device_pattern;
                } else {
                    topic += "/+";  // Any device
                }
            }

            topics.push_back(topic);
        }
    }

    return topics;
}

}  // namespace ipb::scoop::sparkplug
