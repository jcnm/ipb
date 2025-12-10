#include <ipb/gate/config_loader.hpp>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <iostream>

namespace ipb {
namespace gate {

ConfigLoader::ConfigLoader() = default;

ConfigLoader::~ConfigLoader() = default;

bool ConfigLoader::load_from_file(const std::string& config_file) {
    try {
        YAML::Node config = YAML::LoadFile(config_file);
        return parse_config(config);
    } catch (const YAML::Exception& e) {
        std::cerr << "YAML parsing error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Config loading error: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigLoader::load_from_string(const std::string& config_yaml) {
    try {
        YAML::Node config = YAML::Load(config_yaml);
        return parse_config(config);
    } catch (const YAML::Exception& e) {
        std::cerr << "YAML parsing error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Config parsing error: " << e.what() << std::endl;
        return false;
    }
}

GatewayConfig ConfigLoader::get_config() const {
    return config_;
}

bool ConfigLoader::parse_config(const YAML::Node& root) {
    try {
        // Parse gateway settings
        if (root["gateway"]) {
            auto gateway = root["gateway"];
            if (gateway["name"]) {
                config_.gateway.name = gateway["name"].as<std::string>();
            }
            if (gateway["log_level"]) {
                config_.gateway.log_level = gateway["log_level"].as<std::string>();
            }
            if (gateway["worker_threads"]) {
                config_.gateway.worker_threads = gateway["worker_threads"].as<int>();
            }
        }
        
        // Parse sinks
        if (root["sinks"]) {
            for (const auto& sink_node : root["sinks"]) {
                SinkConfig sink_config;
                
                if (sink_node["id"]) {
                    sink_config.id = sink_node["id"].as<std::string>();
                }
                if (sink_node["type"]) {
                    sink_config.type = sink_node["type"].as<std::string>();
                }
                if (sink_node["enabled"]) {
                    sink_config.enabled = sink_node["enabled"].as<bool>();
                }
                
                // Parse sink-specific configuration
                if (sink_config.type == "mqtt") {
                    parse_mqtt_config(sink_node, sink_config);
                } else if (sink_config.type == "console") {
                    parse_console_config(sink_node, sink_config);
                } else if (sink_config.type == "syslog") {
                    parse_syslog_config(sink_node, sink_config);
                }
                
                config_.sinks.push_back(sink_config);
            }
        }
        
        // Parse routing rules
        if (root["routing"] && root["routing"]["rules"]) {
            for (const auto& rule_node : root["routing"]["rules"]) {
                RoutingRuleConfig rule_config;
                
                if (rule_node["name"]) {
                    rule_config.name = rule_node["name"].as<std::string>();
                }
                if (rule_node["enabled"]) {
                    rule_config.enabled = rule_node["enabled"].as<bool>();
                }
                
                // Parse source filter
                if (rule_node["source_filter"]) {
                    auto filter = rule_node["source_filter"];
                    if (filter["address_pattern"]) {
                        rule_config.source_filter.address_pattern = filter["address_pattern"].as<std::string>();
                    }
                    if (filter["protocol_ids"]) {
                        for (const auto& protocol : filter["protocol_ids"]) {
                            rule_config.source_filter.protocol_ids.push_back(protocol.as<std::string>());
                        }
                    }
                }
                
                // Parse destinations
                if (rule_node["destinations"]) {
                    for (const auto& dest_node : rule_node["destinations"]) {
                        RoutingDestinationConfig dest_config;
                        if (dest_node["sink_id"]) {
                            dest_config.sink_id = dest_node["sink_id"].as<std::string>();
                        }
                        if (dest_node["priority"]) {
                            dest_config.priority = dest_node["priority"].as<std::string>();
                        }
                        rule_config.destinations.push_back(dest_config);
                    }
                }
                
                config_.routing_rules.push_back(rule_config);
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Config parsing error: " << e.what() << std::endl;
        return false;
    }
}

void ConfigLoader::parse_mqtt_config(const YAML::Node& node, SinkConfig& config) {
    if (node["broker_url"]) {
        config.mqtt_config.broker_url = node["broker_url"].as<std::string>();
    }
    if (node["client_id"]) {
        config.mqtt_config.client_id = node["client_id"].as<std::string>();
    }
    if (node["base_topic"]) {
        config.mqtt_config.base_topic = node["base_topic"].as<std::string>();
    }
    if (node["qos"]) {
        config.mqtt_config.qos = node["qos"].as<int>();
    }
    if (node["retain"]) {
        config.mqtt_config.retain = node["retain"].as<bool>();
    }
}

void ConfigLoader::parse_console_config(const YAML::Node& node, SinkConfig& config) {
    if (node["format"]) {
        config.console_config.format = node["format"].as<std::string>();
    }
    if (node["colored"]) {
        config.console_config.colored = node["colored"].as<bool>();
    }
}

void ConfigLoader::parse_syslog_config(const YAML::Node& node, SinkConfig& config) {
    if (node["facility"]) {
        config.syslog_config.facility = node["facility"].as<std::string>();
    }
    if (node["identity"]) {
        config.syslog_config.identity = node["identity"].as<std::string>();
    }
}

} // namespace gate
} // namespace ipb

