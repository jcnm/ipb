#pragma once

/**
 * @file console_scoop.hpp
 * @brief Console input protocol scoop (data collector)
 *
 * This scoop reads data from stdin/console input and converts incoming data
 * to IPB DataPoints for routing through the system.
 *
 * Supports multiple input formats:
 * - JSON: {"address": "sensor/temp", "value": 25.5, "quality": "good"}
 * - Key-Value: address=sensor/temp value=25.5 quality=good
 * - CSV: address,value,quality,timestamp
 *
 * Useful for:
 * - Testing and debugging the IPB pipeline
 * - Manual data injection
 * - Integration with shell scripts and external tools
 * - Interactive data entry
 */

#include "ipb/common/interfaces.hpp"
#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/error.hpp"
#include "ipb/common/debug.hpp"
#include "ipb/common/platform.hpp"

#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>
#include <optional>
#include <queue>
#include <thread>
#include <istream>

namespace ipb::scoop::console {

//=============================================================================
// Input Format
//=============================================================================

/**
 * @brief Supported input formats for console data
 */
enum class InputFormat {
    JSON,           ///< JSON format: {"address": "...", "value": ...}
    KEY_VALUE,      ///< Key-value: address=... value=... quality=...
    CSV,            ///< CSV: address,value,quality,timestamp
    AUTO            ///< Auto-detect based on input content
};

//=============================================================================
// Console Scoop Configuration
//=============================================================================

/**
 * @brief Console Scoop configuration
 */
struct ConsoleScoopConfig {
    // Input settings
    InputFormat format = InputFormat::AUTO;     ///< Input format to expect
    std::string prompt = "ipb> ";               ///< Prompt for interactive mode
    bool interactive = false;                   ///< Interactive mode with prompt
    bool echo_input = false;                    ///< Echo parsed data back

    // CSV format settings
    char csv_delimiter = ',';                   ///< CSV field delimiter
    bool csv_has_header = false;                ///< CSV has header row
    std::vector<std::string> csv_columns = {"address", "value", "quality", "timestamp"};

    // Data conversion
    common::Quality default_quality = common::Quality::Good;
    uint16_t default_protocol_id = 100;         ///< Protocol ID for console data
    std::string address_prefix = "console/";    ///< Prefix for addresses

    // Processing
    size_t buffer_size = 1000;                  ///< Max buffered DataPoints
    std::chrono::milliseconds read_timeout{100}; ///< Timeout for non-blocking reads
    bool skip_empty_lines = true;               ///< Skip empty input lines
    bool skip_comments = true;                  ///< Skip lines starting with #

    // Error handling
    bool skip_parse_errors = true;              ///< Skip lines that fail to parse
    size_t max_parse_errors = 100;              ///< Max errors before unhealthy

    // Monitoring
    bool enable_statistics = true;
    std::chrono::seconds statistics_interval{30};

    // Validation
    bool is_valid() const;
    std::string validation_error() const;

    // Presets
    static ConsoleScoopConfig create_default();
    static ConsoleScoopConfig create_interactive();
    static ConsoleScoopConfig create_json_pipe();
    static ConsoleScoopConfig create_csv_pipe();
};

//=============================================================================
// Console Scoop Statistics
//=============================================================================

/**
 * @brief Console Scoop statistics
 */
struct ConsoleScoopStatistics {
    std::atomic<uint64_t> lines_received{0};
    std::atomic<uint64_t> lines_processed{0};
    std::atomic<uint64_t> lines_skipped{0};
    std::atomic<uint64_t> parse_errors{0};
    std::atomic<uint64_t> data_points_produced{0};
    std::atomic<uint64_t> bytes_received{0};

    void reset() {
        lines_received = 0;
        lines_processed = 0;
        lines_skipped = 0;
        parse_errors = 0;
        data_points_produced = 0;
        bytes_received = 0;
    }
};

//=============================================================================
// Console Scoop
//=============================================================================

/**
 * @brief Console Input Protocol Scoop
 *
 * Reads data from stdin/console and converts incoming lines to IPB DataPoints.
 *
 * Features:
 * - Multiple input format support (JSON, key-value, CSV)
 * - Auto-format detection
 * - Interactive and pipe modes
 * - Buffered async delivery
 * - Error/debug/platform system integration
 */
class ConsoleScoop : public common::IProtocolSourceBase {
public:
    static constexpr uint16_t PROTOCOL_ID = 100;
    static constexpr std::string_view PROTOCOL_NAME = "Console";
    static constexpr std::string_view COMPONENT_NAME = "ConsoleScoop";
    static constexpr std::string_view COMPONENT_VERSION = "1.0.0";

    /**
     * @brief Construct ConsoleScoop with configuration
     */
    explicit ConsoleScoop(const ConsoleScoopConfig& config = ConsoleScoopConfig::create_default());

    /**
     * @brief Construct ConsoleScoop with custom input stream
     */
    ConsoleScoop(const ConsoleScoopConfig& config, std::istream& input_stream);

    ~ConsoleScoop() override;

    // Non-copyable
    ConsoleScoop(const ConsoleScoop&) = delete;
    ConsoleScoop& operator=(const ConsoleScoop&) = delete;

    //=========================================================================
    // IProtocolSourceBase Implementation
    //=========================================================================

    common::Result<common::DataSet> read() override;
    common::Result<common::DataSet> read_async() override;

    common::Result<> subscribe(DataCallback data_cb, ErrorCallback error_cb) override;
    common::Result<> unsubscribe() override;

    common::Result<> add_address(std::string_view address) override;
    common::Result<> remove_address(std::string_view address) override;
    std::vector<std::string> get_addresses() const override;

    common::Result<> connect() override;
    common::Result<> disconnect() override;
    bool is_connected() const noexcept override;

    uint16_t protocol_id() const noexcept override { return PROTOCOL_ID; }
    std::string_view protocol_name() const noexcept override { return PROTOCOL_NAME; }

    //=========================================================================
    // IIPBComponent Implementation
    //=========================================================================

    common::Result<> start() override;
    common::Result<> stop() override;
    bool is_running() const noexcept override;

    common::Result<> configure(const common::ConfigurationBase& config) override;
    std::unique_ptr<common::ConfigurationBase> get_configuration() const override;

    common::Statistics get_statistics() const noexcept override;
    void reset_statistics() noexcept override;

    bool is_healthy() const noexcept override;
    std::string get_health_status() const override;

    std::string_view component_name() const noexcept override { return COMPONENT_NAME; }
    std::string_view component_version() const noexcept override { return COMPONENT_VERSION; }

    //=========================================================================
    // Console-Specific Methods
    //=========================================================================

    /**
     * @brief Inject a data point directly (for testing)
     */
    common::Result<> inject_data_point(const common::DataPoint& dp);

    /**
     * @brief Inject a line of text to be parsed
     */
    common::Result<> inject_line(const std::string& line);

    /**
     * @brief Get console-specific statistics
     */
    ConsoleScoopStatistics get_console_statistics() const;

    /**
     * @brief Set custom line parser
     */
    using CustomParserCallback = std::function<std::optional<common::DataPoint>(const std::string& line)>;
    void set_custom_parser(CustomParserCallback parser);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

//=============================================================================
// Console Scoop Factory
//=============================================================================

/**
 * @brief Factory for creating ConsoleScoop instances
 */
class ConsoleScoopFactory {
public:
    /**
     * @brief Create default ConsoleScoop (stdin, auto-format)
     */
    static std::unique_ptr<ConsoleScoop> create();

    /**
     * @brief Create interactive ConsoleScoop with prompt
     */
    static std::unique_ptr<ConsoleScoop> create_interactive(const std::string& prompt = "ipb> ");

    /**
     * @brief Create JSON-mode ConsoleScoop for piped input
     */
    static std::unique_ptr<ConsoleScoop> create_json_pipe();

    /**
     * @brief Create CSV-mode ConsoleScoop for piped input
     */
    static std::unique_ptr<ConsoleScoop> create_csv_pipe(char delimiter = ',', bool has_header = false);

    /**
     * @brief Create ConsoleScoop with custom input stream
     */
    static std::unique_ptr<ConsoleScoop> create_from_stream(std::istream& input);

    /**
     * @brief Create ConsoleScoop with full configuration
     */
    static std::unique_ptr<ConsoleScoop> create(const ConsoleScoopConfig& config);
};

} // namespace ipb::scoop::console
