#pragma once

/**
 * @file config_loader_embedded.hpp
 * @brief Lightweight configuration loader for embedded systems
 *
 * Uses libyaml (C library) and cJSON for minimal memory footprint.
 * Designed for resource-constrained environments with configurable limits.
 *
 * Memory characteristics:
 * - libyaml: ~50KB binary, ~20KB heap during parsing
 * - cJSON: ~15KB binary, ~10KB heap during parsing
 *
 * Features:
 * - Configurable memory limits
 * - Resource cleanup after parsing
 * - Arena allocator support (optional)
 * - No exceptions (error codes only)
 */

#include <ipb/core/config/config_loader.hpp>

#include <cstddef>
#include <functional>

namespace ipb::core::config {

// ============================================================================
// EMBEDDED CONFIGURATION CONSTRAINTS
// ============================================================================

/**
 * @brief Memory constraints for embedded config loading
 */
struct EmbeddedConfigConstraints {
    /// Maximum total memory for config parsing (bytes)
    size_t max_memory_bytes = 64 * 1024;  // 64KB default

    /// Maximum configuration file size (bytes)
    size_t max_file_size = 32 * 1024;  // 32KB default

    /// Maximum string length for any single value
    size_t max_string_length = 1024;

    /// Maximum array/list elements
    size_t max_array_elements = 256;

    /// Maximum nesting depth
    size_t max_nesting_depth = 16;

    /// Maximum number of keys in a map
    size_t max_map_keys = 128;

    /// Release parser resources after loading (recommended for embedded)
    bool release_parser_after_load = true;

    /// Use static/preallocated buffers where possible
    bool use_static_buffers = false;

    /// Static buffer size (if use_static_buffers is true)
    size_t static_buffer_size = 16 * 1024;
};

/**
 * @brief Memory statistics for embedded config loading
 */
struct EmbeddedConfigStats {
    size_t peak_memory_usage    = 0;
    size_t current_memory_usage = 0;
    size_t parse_time_us        = 0;
    size_t file_size            = 0;
    bool constraints_exceeded   = false;
    std::string constraint_error;
};

/**
 * @brief Custom allocator interface for embedded systems
 *
 * Allows integration with custom memory pools or arena allocators.
 */
struct EmbeddedAllocator {
    using AllocFunc   = std::function<void*(size_t)>;
    using FreeFunc    = std::function<void(void*)>;
    using ReallocFunc = std::function<void*(void*, size_t)>;

    AllocFunc alloc     = nullptr;
    FreeFunc free       = nullptr;
    ReallocFunc realloc = nullptr;

    /// Check if custom allocator is configured
    bool is_configured() const { return alloc != nullptr && free != nullptr; }
};

// ============================================================================
// EMBEDDED CONFIG LOADER
// ============================================================================

/**
 * @brief Lightweight ConfigLoader for embedded systems
 *
 * Implementation uses:
 * - libyaml (C library) for YAML parsing
 * - cJSON for JSON parsing
 *
 * Both libraries have minimal footprint and are suitable for:
 * - Embedded Linux (Yocto, Buildroot)
 * - Edge gateways
 * - Resource-constrained environments
 *
 * @note For bare-metal or RTOS without filesystem, use parse_*() methods
 *       with configuration data loaded from flash/EEPROM.
 */
class EmbeddedConfigLoader : public ConfigLoader {
public:
    /**
     * @brief Construct with default constraints
     */
    EmbeddedConfigLoader();

    /**
     * @brief Construct with custom constraints
     * @param constraints Memory and parsing constraints
     */
    explicit EmbeddedConfigLoader(const EmbeddedConfigConstraints& constraints);

    /**
     * @brief Construct with custom allocator
     * @param constraints Memory constraints
     * @param allocator Custom memory allocator
     */
    EmbeddedConfigLoader(const EmbeddedConfigConstraints& constraints,
                         const EmbeddedAllocator& allocator);

    ~EmbeddedConfigLoader() override;

    // Non-copyable, movable
    EmbeddedConfigLoader(const EmbeddedConfigLoader&)            = delete;
    EmbeddedConfigLoader& operator=(const EmbeddedConfigLoader&) = delete;
    EmbeddedConfigLoader(EmbeddedConfigLoader&&) noexcept;
    EmbeddedConfigLoader& operator=(EmbeddedConfigLoader&&) noexcept;

    // ========================================================================
    // EMBEDDED-SPECIFIC CONFIGURATION
    // ========================================================================

    /**
     * @brief Set memory constraints
     * @param constraints New constraints to apply
     */
    void set_constraints(const EmbeddedConfigConstraints& constraints);

    /**
     * @brief Get current constraints
     */
    const EmbeddedConfigConstraints& constraints() const { return constraints_; }

    /**
     * @brief Set custom allocator
     * @param allocator Custom memory allocator
     */
    void set_allocator(const EmbeddedAllocator& allocator);

    /**
     * @brief Get statistics from last parse operation
     */
    const EmbeddedConfigStats& last_stats() const { return last_stats_; }

    /**
     * @brief Release all cached resources
     *
     * Call this after configuration is loaded to free parser memory.
     * Useful for single-load-at-startup scenarios.
     */
    void release_resources();

    /**
     * @brief Check if constraints would be exceeded
     * @param content Configuration content to check
     * @return true if content fits within constraints
     */
    bool check_constraints(std::string_view content) const;

    // ========================================================================
    // ConfigLoader INTERFACE IMPLEMENTATION
    // ========================================================================

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

    // Serialization (minimal support for embedded)
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
    EmbeddedConfigConstraints constraints_;
    EmbeddedAllocator allocator_;
    EmbeddedConfigStats last_stats_;

    // Static buffer for use_static_buffers mode
    std::unique_ptr<char[]> static_buffer_;

    // Internal helpers
    common::Result<std::string> read_file_constrained(const std::filesystem::path& path);
    void update_stats(size_t memory_used, size_t parse_time_us);
    bool validate_constraints(std::string_view content);
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

/**
 * @brief Create embedded ConfigLoader with default constraints
 */
std::unique_ptr<ConfigLoader> create_embedded_config_loader();

/**
 * @brief Create embedded ConfigLoader with custom constraints
 */
std::unique_ptr<ConfigLoader> create_embedded_config_loader(
    const EmbeddedConfigConstraints& constraints);

/**
 * @brief Create platform-appropriate ConfigLoader
 *
 * Automatically selects:
 * - EmbeddedConfigLoader for EMBEDDED_* platforms
 * - ConfigLoaderImpl for SERVER_* and EDGE_* platforms
 *
 * @param platform Target deployment platform
 * @param constraints Optional constraints for embedded platforms
 */
std::unique_ptr<ConfigLoader> create_config_loader_for_platform(
    common::DeploymentPlatform platform, const EmbeddedConfigConstraints& constraints = {});

// ============================================================================
// COMPILE-TIME PLATFORM SELECTION
// ============================================================================

#if defined(IPB_MODE_EMBEDDED)
// Use embedded loader for EMBEDDED build mode
inline std::unique_ptr<ConfigLoader> create_platform_config_loader() {
    return create_embedded_config_loader();
}
#elif defined(IPB_MODE_EDGE)
// Use standard loader for EDGE build mode
inline std::unique_ptr<ConfigLoader> create_platform_config_loader() {
    return create_config_loader();
}
#else
// Use standard loader for SERVER build mode (default)
inline std::unique_ptr<ConfigLoader> create_platform_config_loader() {
    return create_config_loader();
}
#endif

}  // namespace ipb::core::config
