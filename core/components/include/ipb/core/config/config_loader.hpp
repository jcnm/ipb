#pragma once

/**
 * @file config_loader.hpp
 * @brief Configuration loader for IPB components
 *
 * Provides loading of Sink, Scoop, Router, and Application configurations
 * from YAML (default) or JSON files.
 */

#include <ipb/common/error.hpp>
#include <ipb/core/config/config_types.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ipb::core::config {

/**
 * @brief Configuration loader interface
 *
 * Loads IPB component configurations from files or strings.
 * Supports YAML (default) and JSON formats.
 */
class ConfigLoader {
public:
    virtual ~ConfigLoader() = default;

    // ========================================================================
    // FORMAT DETECTION
    // ========================================================================

    /**
     * @brief Detect format from file extension
     * @param path File path to check
     * @return Detected format (YAML for .yml/.yaml, JSON for .json)
     */
    static ConfigFormat detect_format(const std::filesystem::path& path);

    /**
     * @brief Detect format from content
     * @param content Configuration content
     * @return Detected format based on content analysis
     */
    static ConfigFormat detect_format_from_content(std::string_view content);

    // ========================================================================
    // FILE LOADING
    // ========================================================================

    /**
     * @brief Load application configuration from file
     * @param path Path to configuration file
     * @param format Format override (AUTO to detect from extension)
     * @return Application configuration or error
     */
    virtual common::Result<ApplicationConfig> load_application(
        const std::filesystem::path& path, ConfigFormat format = ConfigFormat::AUTO) = 0;

    /**
     * @brief Load Scoop configuration from file
     * @param path Path to configuration file
     * @param format Format override
     * @return Scoop configuration or error
     */
    virtual common::Result<ScoopConfig> load_scoop(const std::filesystem::path& path,
                                                   ConfigFormat format = ConfigFormat::AUTO) = 0;

    /**
     * @brief Load Sink configuration from file
     * @param path Path to configuration file
     * @param format Format override
     * @return Sink configuration or error
     */
    virtual common::Result<SinkConfig> load_sink(const std::filesystem::path& path,
                                                 ConfigFormat format = ConfigFormat::AUTO) = 0;

    /**
     * @brief Load Router configuration from file
     * @param path Path to configuration file
     * @param format Format override
     * @return Router configuration or error
     */
    virtual common::Result<RouterConfig> load_router(const std::filesystem::path& path,
                                                     ConfigFormat format = ConfigFormat::AUTO) = 0;

    /**
     * @brief Load multiple Scoop configurations from directory
     * @param dir_path Directory containing scoop config files
     * @param format Format filter (AUTO loads all supported formats)
     * @return Vector of Scoop configurations
     */
    virtual common::Result<std::vector<ScoopConfig>> load_scoops_from_directory(
        const std::filesystem::path& dir_path, ConfigFormat format = ConfigFormat::AUTO) = 0;

    /**
     * @brief Load multiple Sink configurations from directory
     * @param dir_path Directory containing sink config files
     * @param format Format filter (AUTO loads all supported formats)
     * @return Vector of Sink configurations
     */
    virtual common::Result<std::vector<SinkConfig>> load_sinks_from_directory(
        const std::filesystem::path& dir_path, ConfigFormat format = ConfigFormat::AUTO) = 0;

    // ========================================================================
    // STRING PARSING
    // ========================================================================

    /**
     * @brief Parse application configuration from string
     * @param content Configuration content
     * @param format Format of content (AUTO to detect)
     * @return Application configuration or error
     */
    virtual common::Result<ApplicationConfig> parse_application(
        std::string_view content, ConfigFormat format = ConfigFormat::AUTO) = 0;

    /**
     * @brief Parse Scoop configuration from string
     * @param content Configuration content
     * @param format Format of content
     * @return Scoop configuration or error
     */
    virtual common::Result<ScoopConfig> parse_scoop(std::string_view content,
                                                    ConfigFormat format = ConfigFormat::AUTO) = 0;

    /**
     * @brief Parse Sink configuration from string
     * @param content Configuration content
     * @param format Format of content
     * @return Sink configuration or error
     */
    virtual common::Result<SinkConfig> parse_sink(std::string_view content,
                                                  ConfigFormat format = ConfigFormat::AUTO) = 0;

    /**
     * @brief Parse Router configuration from string
     * @param content Configuration content
     * @param format Format of content
     * @return Router configuration or error
     */
    virtual common::Result<RouterConfig> parse_router(std::string_view content,
                                                      ConfigFormat format = ConfigFormat::AUTO) = 0;

    // ========================================================================
    // SERIALIZATION
    // ========================================================================

    /**
     * @brief Serialize application configuration to string
     * @param config Configuration to serialize
     * @param format Output format
     * @return Serialized configuration or error
     */
    virtual common::Result<std::string> serialize_application(
        const ApplicationConfig& config, ConfigFormat format = ConfigFormat::YAML) = 0;

    /**
     * @brief Serialize Scoop configuration to string
     * @param config Configuration to serialize
     * @param format Output format
     * @return Serialized configuration or error
     */
    virtual common::Result<std::string> serialize_scoop(
        const ScoopConfig& config, ConfigFormat format = ConfigFormat::YAML) = 0;

    /**
     * @brief Serialize Sink configuration to string
     * @param config Configuration to serialize
     * @param format Output format
     * @return Serialized configuration or error
     */
    virtual common::Result<std::string> serialize_sink(
        const SinkConfig& config, ConfigFormat format = ConfigFormat::YAML) = 0;

    /**
     * @brief Serialize Router configuration to string
     * @param config Configuration to serialize
     * @param format Output format
     * @return Serialized configuration or error
     */
    virtual common::Result<std::string> serialize_router(
        const RouterConfig& config, ConfigFormat format = ConfigFormat::YAML) = 0;

    // ========================================================================
    // FILE SAVING
    // ========================================================================

    /**
     * @brief Save application configuration to file
     * @param config Configuration to save
     * @param path Output file path
     * @param format Format (AUTO to detect from extension)
     * @return Success or error
     */
    virtual common::Result<void> save_application(const ApplicationConfig& config,
                                                  const std::filesystem::path& path,
                                                  ConfigFormat format = ConfigFormat::AUTO) = 0;

    /**
     * @brief Save Scoop configuration to file
     * @param config Configuration to save
     * @param path Output file path
     * @param format Format (AUTO to detect from extension)
     * @return Success or error
     */
    virtual common::Result<void> save_scoop(const ScoopConfig& config,
                                            const std::filesystem::path& path,
                                            ConfigFormat format = ConfigFormat::AUTO) = 0;

    /**
     * @brief Save Sink configuration to file
     * @param config Configuration to save
     * @param path Output file path
     * @param format Format (AUTO to detect from extension)
     * @return Success or error
     */
    virtual common::Result<void> save_sink(const SinkConfig& config,
                                           const std::filesystem::path& path,
                                           ConfigFormat format = ConfigFormat::AUTO) = 0;

    // ========================================================================
    // VALIDATION
    // ========================================================================

    /**
     * @brief Validate application configuration
     * @param config Configuration to validate
     * @return Success or validation error with details
     */
    virtual common::Result<void> validate(const ApplicationConfig& config) = 0;

    /**
     * @brief Validate Scoop configuration
     * @param config Configuration to validate
     * @return Success or validation error with details
     */
    virtual common::Result<void> validate(const ScoopConfig& config) = 0;

    /**
     * @brief Validate Sink configuration
     * @param config Configuration to validate
     * @return Success or validation error with details
     */
    virtual common::Result<void> validate(const SinkConfig& config) = 0;

    /**
     * @brief Validate Router configuration
     * @param config Configuration to validate
     * @return Success or validation error with details
     */
    virtual common::Result<void> validate(const RouterConfig& config) = 0;
};

/**
 * @brief Create default ConfigLoader instance
 *
 * Creates a ConfigLoader that supports both YAML and JSON formats.
 * YAML is the preferred format for human-readable configurations.
 *
 * @return Unique pointer to ConfigLoader instance
 */
std::unique_ptr<ConfigLoader> create_config_loader();

/**
 * @brief ConfigLoader implementation using yaml-cpp and jsoncpp
 */
class ConfigLoaderImpl : public ConfigLoader {
public:
    ConfigLoaderImpl()           = default;
    ~ConfigLoaderImpl() override = default;

    // File loading
    common::Result<ApplicationConfig> load_application(
        const std::filesystem::path& path, ConfigFormat format = ConfigFormat::AUTO) override;

    common::Result<ScoopConfig> load_scoop(const std::filesystem::path& path,
                                           ConfigFormat format = ConfigFormat::AUTO) override;

    common::Result<SinkConfig> load_sink(const std::filesystem::path& path,
                                         ConfigFormat format = ConfigFormat::AUTO) override;

    common::Result<RouterConfig> load_router(const std::filesystem::path& path,
                                             ConfigFormat format = ConfigFormat::AUTO) override;

    common::Result<std::vector<ScoopConfig>> load_scoops_from_directory(
        const std::filesystem::path& dir_path, ConfigFormat format = ConfigFormat::AUTO) override;

    common::Result<std::vector<SinkConfig>> load_sinks_from_directory(
        const std::filesystem::path& dir_path, ConfigFormat format = ConfigFormat::AUTO) override;

    // String parsing
    common::Result<ApplicationConfig> parse_application(
        std::string_view content, ConfigFormat format = ConfigFormat::AUTO) override;

    common::Result<ScoopConfig> parse_scoop(std::string_view content,
                                            ConfigFormat format = ConfigFormat::AUTO) override;

    common::Result<SinkConfig> parse_sink(std::string_view content,
                                          ConfigFormat format = ConfigFormat::AUTO) override;

    common::Result<RouterConfig> parse_router(std::string_view content,
                                              ConfigFormat format = ConfigFormat::AUTO) override;

    // Serialization
    common::Result<std::string> serialize_application(
        const ApplicationConfig& config, ConfigFormat format = ConfigFormat::YAML) override;

    common::Result<std::string> serialize_scoop(const ScoopConfig& config,
                                                ConfigFormat format = ConfigFormat::YAML) override;

    common::Result<std::string> serialize_sink(const SinkConfig& config,
                                               ConfigFormat format = ConfigFormat::YAML) override;

    common::Result<std::string> serialize_router(const RouterConfig& config,
                                                 ConfigFormat format = ConfigFormat::YAML) override;

    // File saving
    common::Result<void> save_application(const ApplicationConfig& config,
                                          const std::filesystem::path& path,
                                          ConfigFormat format = ConfigFormat::AUTO) override;

    common::Result<void> save_scoop(const ScoopConfig& config, const std::filesystem::path& path,
                                    ConfigFormat format = ConfigFormat::AUTO) override;

    common::Result<void> save_sink(const SinkConfig& config, const std::filesystem::path& path,
                                   ConfigFormat format = ConfigFormat::AUTO) override;

    // Validation
    common::Result<void> validate(const ApplicationConfig& config) override;
    common::Result<void> validate(const ScoopConfig& config) override;
    common::Result<void> validate(const SinkConfig& config) override;
    common::Result<void> validate(const RouterConfig& config) override;

private:
    // Internal helpers
    common::Result<std::string> read_file(const std::filesystem::path& path);
    common::Result<void> write_file(const std::filesystem::path& path, std::string_view content);
    ConfigFormat resolve_format(const std::filesystem::path& path, ConfigFormat format);
};

}  // namespace ipb::core::config
