/**
 * @file sparkplug_payload.cpp
 * @brief Sparkplug B payload decoding implementation
 */

#include "ipb/scoop/sparkplug/sparkplug_scoop.hpp"

#include <ipb/common/error.hpp>
#include <ipb/common/debug.hpp>

#include <cstring>
#include <chrono>

namespace ipb::scoop::sparkplug {

using namespace common::debug;

namespace {
    constexpr std::string_view LOG_CAT = category::PROTOCOL;
} // anonymous namespace

//=============================================================================
// SparkplugMetric Implementation
//=============================================================================

common::DataPoint SparkplugMetric::to_data_point(
    const std::string& edge_node_id,
    const std::string& device_id) const
{
    common::DataPoint dp;

    // Build address: sparkplug/{group}/{node}[/{device}]/{metric_name}
    std::string address = "sparkplug/" + edge_node_id;
    if (!device_id.empty()) {
        address += "/" + device_id;
    }
    address += "/" + name;

    dp.set_address(address);

    // Set timestamp
    if (timestamp > 0) {
        auto time_point = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(timestamp)
        );
        dp.set_timestamp(common::Timestamp(time_point));
    } else {
        dp.set_timestamp(common::Timestamp::now());
    }

    // Set protocol ID
    dp.set_protocol_id(SparkplugScoop::PROTOCOL_ID);

    // Set quality
    if (is_null) {
        dp.set_quality(common::Quality::BAD);
    } else {
        dp.set_quality(common::Quality::GOOD);
    }

    // Set value based on variant type
    std::visit([&dp](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, bool>) {
            dp.set_value(arg);
        } else if constexpr (std::is_same_v<T, int8_t>) {
            dp.set_value(static_cast<int32_t>(arg));
        } else if constexpr (std::is_same_v<T, int16_t>) {
            dp.set_value(static_cast<int32_t>(arg));
        } else if constexpr (std::is_same_v<T, int32_t>) {
            dp.set_value(arg);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            dp.set_value(arg);
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            dp.set_value(static_cast<uint32_t>(arg));
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            dp.set_value(static_cast<uint32_t>(arg));
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            dp.set_value(arg);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            dp.set_value(arg);
        } else if constexpr (std::is_same_v<T, float>) {
            dp.set_value(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            dp.set_value(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            dp.set_value(arg);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            // For binary data, encode as hex string for now
            std::string hex;
            hex.reserve(arg.size() * 2);
            for (uint8_t b : arg) {
                static const char hex_chars[] = "0123456789ABCDEF";
                hex.push_back(hex_chars[b >> 4]);
                hex.push_back(hex_chars[b & 0x0F]);
            }
            dp.set_value(hex);
        }
    }, value);

    return dp;
}

//=============================================================================
// SparkplugPayload Implementation
//=============================================================================

namespace {

/**
 * @brief Read a big-endian uint64 from buffer
 */
inline uint64_t read_uint64_be(const uint8_t* data) {
    return (static_cast<uint64_t>(data[0]) << 56) |
           (static_cast<uint64_t>(data[1]) << 48) |
           (static_cast<uint64_t>(data[2]) << 40) |
           (static_cast<uint64_t>(data[3]) << 32) |
           (static_cast<uint64_t>(data[4]) << 24) |
           (static_cast<uint64_t>(data[5]) << 16) |
           (static_cast<uint64_t>(data[6]) << 8) |
           static_cast<uint64_t>(data[7]);
}

/**
 * @brief Read a big-endian uint32 from buffer
 */
inline uint32_t read_uint32_be(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

/**
 * @brief Read a string from buffer (length-prefixed)
 */
inline std::string read_string(const uint8_t* data, size_t& offset, size_t max_size) {
    if (offset + 4 > max_size) return "";

    uint32_t len = read_uint32_be(data + offset);
    offset += 4;

    if (offset + len > max_size) return "";

    std::string result(reinterpret_cast<const char*>(data + offset), len);
    offset += len;

    return result;
}

} // anonymous namespace

std::optional<SparkplugPayload> SparkplugPayload::decode(const std::vector<uint8_t>& data) {
    if (data.size() < 24) {  // Minimum: timestamp(8) + seq(8) + metric_count(4) + at least something
        IPB_LOG_WARN(LOG_CAT, "Payload too small: " << data.size() << " bytes");
        return std::nullopt;
    }

#ifdef IPB_HAS_PROTOBUF
    // TODO: Use generated protobuf classes for proper decoding
    IPB_LOG_DEBUG(LOG_CAT, "Decoding Sparkplug payload with protobuf");
#else
    // Simplified binary decoding (matches our encoder format)
    SparkplugPayload payload;

    size_t offset = 0;

    // Read timestamp
    payload.timestamp = read_uint64_be(data.data() + offset);
    offset += 8;

    // Read sequence number
    payload.seq = read_uint64_be(data.data() + offset);
    offset += 8;

    // Read metric count
    if (offset + 4 > data.size()) {
        return std::nullopt;
    }
    uint32_t metric_count = read_uint32_be(data.data() + offset);
    offset += 4;

    // Sanity check
    if (metric_count > 10000) {
        IPB_LOG_WARN(LOG_CAT, "Too many metrics: " << metric_count);
        return std::nullopt;
    }

    payload.metrics.reserve(metric_count);

    // Read each metric
    for (uint32_t i = 0; i < metric_count && offset < data.size(); ++i) {
        SparkplugMetric metric;

        // Read name/alias flag
        if (offset >= data.size()) break;
        bool use_alias = (data[offset++] != 0);

        if (use_alias) {
            // Read alias
            if (offset + 8 > data.size()) break;
            metric.alias = read_uint64_be(data.data() + offset);
            offset += 8;
        } else {
            // Read name
            metric.name = read_string(data.data(), offset, data.size());
        }

        // Read datatype
        if (offset + 4 > data.size()) break;
        metric.datatype = static_cast<SparkplugDataType>(read_uint32_be(data.data() + offset));
        offset += 4;

        // Read value based on datatype
        switch (metric.datatype) {
            case SparkplugDataType::Boolean:
                if (offset >= data.size()) break;
                metric.value = (data[offset++] != 0);
                break;

            case SparkplugDataType::Int8:
                if (offset >= data.size()) break;
                metric.value = static_cast<int8_t>(data[offset++]);
                break;

            case SparkplugDataType::Int16:
                if (offset + 2 > data.size()) break;
                metric.value = static_cast<int16_t>(
                    (static_cast<int16_t>(data[offset]) << 8) |
                    static_cast<int16_t>(data[offset + 1])
                );
                offset += 2;
                break;

            case SparkplugDataType::Int32:
                if (offset + 4 > data.size()) break;
                metric.value = static_cast<int32_t>(read_uint32_be(data.data() + offset));
                offset += 4;
                break;

            case SparkplugDataType::Int64:
                if (offset + 8 > data.size()) break;
                metric.value = static_cast<int64_t>(read_uint64_be(data.data() + offset));
                offset += 8;
                break;

            case SparkplugDataType::UInt8:
                if (offset >= data.size()) break;
                metric.value = data[offset++];
                break;

            case SparkplugDataType::UInt16:
                if (offset + 2 > data.size()) break;
                metric.value = static_cast<uint16_t>(
                    (static_cast<uint16_t>(data[offset]) << 8) |
                    static_cast<uint16_t>(data[offset + 1])
                );
                offset += 2;
                break;

            case SparkplugDataType::UInt32:
                if (offset + 4 > data.size()) break;
                metric.value = read_uint32_be(data.data() + offset);
                offset += 4;
                break;

            case SparkplugDataType::UInt64:
                if (offset + 8 > data.size()) break;
                metric.value = read_uint64_be(data.data() + offset);
                offset += 8;
                break;

            case SparkplugDataType::Float:
                if (offset + 4 > data.size()) break;
                {
                    uint32_t bits = read_uint32_be(data.data() + offset);
                    float f;
                    std::memcpy(&f, &bits, sizeof(f));
                    metric.value = f;
                }
                offset += 4;
                break;

            case SparkplugDataType::Double:
                if (offset + 8 > data.size()) break;
                {
                    uint64_t bits = read_uint64_be(data.data() + offset);
                    double d;
                    std::memcpy(&d, &bits, sizeof(d));
                    metric.value = d;
                }
                offset += 8;
                break;

            case SparkplugDataType::String:
            case SparkplugDataType::Text:
                metric.value = read_string(data.data(), offset, data.size());
                break;

            case SparkplugDataType::Bytes:
                {
                    if (offset + 4 > data.size()) break;
                    uint32_t len = read_uint32_be(data.data() + offset);
                    offset += 4;
                    if (offset + len > data.size()) break;
                    std::vector<uint8_t> bytes(data.begin() + offset,
                                               data.begin() + offset + len);
                    metric.value = std::move(bytes);
                    offset += len;
                }
                break;

            default:
                // Skip unknown types
                metric.is_null = true;
                break;
        }

        payload.metrics.push_back(std::move(metric));
    }

    IPB_LOG_TRACE(LOG_CAT, "Decoded " << payload.metrics.size() << " metrics");
    return payload;
#endif

    return std::nullopt;
}

std::vector<uint8_t> SparkplugPayload::encode() const {
    std::vector<uint8_t> data;

    // This is primarily used by the sink encoder
    // For the scoop, we mainly decode

    // Reserve approximate space
    data.reserve(16 + metrics.size() * 32);

    // Write timestamp (big-endian)
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((timestamp >> (i * 8)) & 0xFF));
    }

    // Write sequence
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((seq >> (i * 8)) & 0xFF));
    }

    // Write metric count
    uint32_t count = static_cast<uint32_t>(metrics.size());
    for (int i = 3; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((count >> (i * 8)) & 0xFF));
    }

    // Metrics would be encoded here (similar to sparkplug_encoder.cpp)
    // For brevity, this is left as a stub

    return data;
}

} // namespace ipb::scoop::sparkplug
