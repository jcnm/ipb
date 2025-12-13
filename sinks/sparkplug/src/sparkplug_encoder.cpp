/**
 * @file sparkplug_encoder.cpp
 * @brief Sparkplug B payload encoder implementation
 *
 * This file contains the encoder for Sparkplug B payloads.
 * When protobuf is available (IPB_HAS_PROTOBUF), it uses the generated
 * protobuf classes. Otherwise, it provides a stub implementation.
 */

#include <ipb/common/debug.hpp>
#include <ipb/common/error.hpp>

#include <chrono>
#include <cstring>
#include <vector>

#include "ipb/sink/sparkplug/sparkplug_sink.hpp"

namespace ipb::sink::sparkplug {

using namespace common::debug;

namespace {
constexpr std::string_view LOG_CAT = category::PROTOCOL;
}  // anonymous namespace

//=============================================================================
// Sparkplug B Encoder
//=============================================================================

namespace encoder {

/**
 * @brief Get current timestamp in milliseconds since epoch
 */
inline uint64_t get_timestamp_ms() {
    auto now = std::chrono::system_clock::now();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return static_cast<uint64_t>(ms.count());
}

/**
 * @brief Encode a Sparkplug B metric value to bytes
 *
 * This is a simplified encoder. The full implementation would use
 * Protocol Buffers for proper Sparkplug B encoding.
 */
class SparkplugEncoder {
public:
    /**
     * @brief Encode a birth certificate payload
     */
    static std::vector<uint8_t> encode_birth(uint64_t timestamp, uint64_t seq, uint64_t bdseq,
                                             const std::vector<MetricDefinition>& metrics) {
        std::vector<uint8_t> payload;

        // In a real implementation, this would use protobuf:
        // org.eclipse.tahu.protobuf.SparkplugBProto.Payload

#ifdef IPB_HAS_PROTOBUF
        // TODO: Use generated protobuf classes
        IPB_LOG_DEBUG(LOG_CAT, "Encoding BIRTH with protobuf");
#else
        // Stub implementation - encode as simple binary format
        // Format: [timestamp:8][seq:8][bdseq:8][metric_count:4][metrics...]

        payload.reserve(24 + metrics.size() * 64);

        // Timestamp (8 bytes, big-endian)
        append_uint64(payload, timestamp);

        // Sequence number (8 bytes)
        append_uint64(payload, seq);

        // Birth/death sequence (8 bytes)
        append_uint64(payload, bdseq);

        // Metric count (4 bytes)
        append_uint32(payload, static_cast<uint32_t>(metrics.size()));

        // Encode each metric definition
        for (const auto& metric : metrics) {
            encode_metric_definition(payload, metric);
        }

        IPB_LOG_TRACE(LOG_CAT, "Encoded BIRTH payload: " << payload.size() << " bytes");
#endif

        return payload;
    }

    /**
     * @brief Encode a death certificate payload
     */
    static std::vector<uint8_t> encode_death(uint64_t timestamp, uint64_t bdseq) {
        std::vector<uint8_t> payload;

#ifdef IPB_HAS_PROTOBUF
        // TODO: Use generated protobuf classes
#else
        payload.reserve(16);
        append_uint64(payload, timestamp);
        append_uint64(payload, bdseq);
#endif

        return payload;
    }

    /**
     * @brief Encode a data payload with metrics
     */
    static std::vector<uint8_t> encode_data(
        uint64_t timestamp, uint64_t seq,
        const std::vector<std::pair<MetricDefinition, common::Value>>& metrics,
        bool use_aliases = true) {
        std::vector<uint8_t> payload;

#ifdef IPB_HAS_PROTOBUF
        // TODO: Use generated protobuf classes
#else
        payload.reserve(16 + metrics.size() * 32);

        // Timestamp
        append_uint64(payload, timestamp);

        // Sequence
        append_uint64(payload, seq);

        // Metric count
        append_uint32(payload, static_cast<uint32_t>(metrics.size()));

        // Encode each metric value
        for (const auto& [def, value] : metrics) {
            encode_metric_value(payload, def, value, use_aliases);
        }
#endif

        return payload;
    }

    /**
     * @brief Encode a single data point as a data payload
     */
    static std::vector<uint8_t> encode_data_point(uint64_t timestamp, uint64_t seq,
                                                  const common::DataPoint& dp, uint64_t alias = 0) {
        std::vector<uint8_t> payload;

#ifdef IPB_HAS_PROTOBUF
        // TODO: Use generated protobuf classes
#else
        payload.reserve(64);

        // Timestamp
        append_uint64(payload, timestamp);

        // Sequence
        append_uint64(payload, seq);

        // Metric count = 1
        append_uint32(payload, 1);

        // Encode the metric
        auto def  = MetricDefinition::from_data_point(dp);
        def.alias = alias;
        encode_metric_value(payload, def, dp.value(), alias > 0);
#endif

        return payload;
    }

private:
    static void append_uint64(std::vector<uint8_t>& buf, uint64_t value) {
        for (int i = 7; i >= 0; --i) {
            buf.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
        }
    }

    static void append_uint32(std::vector<uint8_t>& buf, uint32_t value) {
        for (int i = 3; i >= 0; --i) {
            buf.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
        }
    }

    static void append_string(std::vector<uint8_t>& buf, const std::string& str) {
        append_uint32(buf, static_cast<uint32_t>(str.size()));
        buf.insert(buf.end(), str.begin(), str.end());
    }

    static void encode_metric_definition(std::vector<uint8_t>& buf,
                                         const MetricDefinition& metric) {
        // Name
        append_string(buf, metric.name);

        // Alias
        append_uint64(buf, metric.alias);

        // Datatype
        append_uint32(buf, static_cast<uint32_t>(metric.datatype));

        // Flags
        uint8_t flags = 0;
        if (metric.is_transient)
            flags |= 0x01;
        if (metric.is_historical)
            flags |= 0x02;
        buf.push_back(flags);

        // Optional metadata (simplified)
        bool has_description = metric.description.has_value();
        bool has_unit        = metric.unit.has_value();
        buf.push_back((has_description ? 0x01 : 0x00) | (has_unit ? 0x02 : 0x00));

        if (has_description) {
            append_string(buf, *metric.description);
        }
        if (has_unit) {
            append_string(buf, *metric.unit);
        }
    }

    static void encode_metric_value(std::vector<uint8_t>& buf, const MetricDefinition& def,
                                    const common::Value& value, bool use_alias) {
        // Use alias or name
        if (use_alias && def.alias > 0) {
            buf.push_back(0x01);  // Flag: using alias
            append_uint64(buf, def.alias);
        } else {
            buf.push_back(0x00);  // Flag: using name
            append_string(buf, def.name);
        }

        // Datatype
        append_uint32(buf, static_cast<uint32_t>(def.datatype));

        // Value based on type
        switch (value.type()) {
            case common::Value::Type::BOOL:
                buf.push_back(value.get<bool>() ? 0x01 : 0x00);
                break;

            case common::Value::Type::INT8:
                buf.push_back(static_cast<uint8_t>(value.get<int8_t>()));
                break;

            case common::Value::Type::INT16: {
                int16_t v = value.get<int16_t>();
                buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
                buf.push_back(static_cast<uint8_t>(v & 0xFF));
                break;
            }

            case common::Value::Type::INT32: {
                int32_t v = value.get<int32_t>();
                append_uint32(buf, static_cast<uint32_t>(v));
                break;
            }

            case common::Value::Type::INT64: {
                int64_t v = value.get<int64_t>();
                append_uint64(buf, static_cast<uint64_t>(v));
                break;
            }

            case common::Value::Type::UINT8:
                buf.push_back(value.get<uint8_t>());
                break;

            case common::Value::Type::UINT16: {
                uint16_t v = value.get<uint16_t>();
                buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
                buf.push_back(static_cast<uint8_t>(v & 0xFF));
                break;
            }

            case common::Value::Type::UINT32:
                append_uint32(buf, value.get<uint32_t>());
                break;

            case common::Value::Type::UINT64:
                append_uint64(buf, value.get<uint64_t>());
                break;

            case common::Value::Type::FLOAT32: {
                float v = value.get<float>();
                uint32_t bits;
                std::memcpy(&bits, &v, sizeof(bits));
                append_uint32(buf, bits);
                break;
            }

            case common::Value::Type::FLOAT64: {
                double v = value.get<double>();
                uint64_t bits;
                std::memcpy(&bits, &v, sizeof(bits));
                append_uint64(buf, bits);
                break;
            }

            case common::Value::Type::STRING: {
                auto sv = value.as_string_view();
                append_string(buf, std::string(sv));
                break;
            }

            default:
                // Unknown type - encode as null
                buf.push_back(0x00);
                break;
        }
    }
};

}  // namespace encoder

}  // namespace ipb::sink::sparkplug
