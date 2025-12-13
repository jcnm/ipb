#include "ipb/common/data_point.hpp"

#include <algorithm>
#include <cstring>
#include <functional>

namespace ipb::common {

// Value implementation
void Value::serialize(std::span<uint8_t> buffer) const noexcept {
    if (buffer.size() < serialized_size())
        return;

    size_t offset = 0;

    // Write type
    std::memcpy(buffer.data() + offset, &type_, sizeof(Type));
    offset += sizeof(Type);

    // Write size
    std::memcpy(buffer.data() + offset, &size_, sizeof(size_t));
    offset += sizeof(size_t);

    // Write data
    if (size_ > 0) {
        const uint8_t* data_ptr = size_ <= INLINE_SIZE ? inline_data_ : external_data_.get();
        std::memcpy(buffer.data() + offset, data_ptr, size_);
    }
}

bool Value::deserialize(std::span<const uint8_t> buffer) noexcept {
    if (buffer.size() < sizeof(Type) + sizeof(size_t))
        return false;

    // Clean up current external storage if any
    if (size_ > INLINE_SIZE) {
        external_data_.~unique_ptr();
    }

    size_t offset = 0;

    // Read type
    std::memcpy(&type_, buffer.data() + offset, sizeof(Type));
    offset += sizeof(Type);

    // Read size
    std::memcpy(&size_, buffer.data() + offset, sizeof(size_t));
    offset += sizeof(size_t);

    // Validate remaining buffer size
    if (buffer.size() < offset + size_)
        return false;

    // Read data
    if (size_ > 0) {
        if (size_ <= INLINE_SIZE) {
            std::memcpy(inline_data_, buffer.data() + offset, size_);
        } else {
            new (&external_data_) std::unique_ptr<uint8_t[]>(std::make_unique<uint8_t[]>(size_));
            std::memcpy(external_data_.get(), buffer.data() + offset, size_);
        }
    }

    return true;
}

template <>
void Value::set_impl<bool>(bool&& value) noexcept {
    type_ = Type::BOOL;
    size_ = sizeof(bool);
    std::memcpy(inline_data_, &value, size_);
}

template <>
void Value::set_impl<int8_t>(int8_t&& value) noexcept {
    type_ = Type::INT8;
    size_ = sizeof(int8_t);
    std::memcpy(inline_data_, &value, size_);
}

template <>
void Value::set_impl<int16_t>(int16_t&& value) noexcept {
    type_ = Type::INT16;
    size_ = sizeof(int16_t);
    std::memcpy(inline_data_, &value, size_);
}

template <>
void Value::set_impl<uint8_t>(uint8_t&& value) noexcept {
    type_ = Type::UINT8;
    size_ = sizeof(uint8_t);
    std::memcpy(inline_data_, &value, size_);
}

template <>
void Value::set_impl<uint16_t>(uint16_t&& value) noexcept {
    type_ = Type::UINT16;
    size_ = sizeof(uint16_t);
    std::memcpy(inline_data_, &value, size_);
}

template <>
void Value::set_impl<int32_t>(int32_t&& value) noexcept {
    type_ = Type::INT32;
    size_ = sizeof(int32_t);
    std::memcpy(inline_data_, &value, size_);
}

template <>
void Value::set_impl<uint32_t>(uint32_t&& value) noexcept {
    type_ = Type::UINT32;
    size_ = sizeof(uint32_t);
    std::memcpy(inline_data_, &value, size_);
}

template <>
void Value::set_impl<int64_t>(int64_t&& value) noexcept {
    type_ = Type::INT64;
    size_ = sizeof(int64_t);
    std::memcpy(inline_data_, &value, size_);
}

template <>
void Value::set_impl<uint64_t>(uint64_t&& value) noexcept {
    type_ = Type::UINT64;
    size_ = sizeof(uint64_t);
    std::memcpy(inline_data_, &value, size_);
}

template <>
void Value::set_impl<float>(float&& value) noexcept {
    type_ = Type::FLOAT32;
    size_ = sizeof(float);
    std::memcpy(inline_data_, &value, size_);
}

template <>
void Value::set_impl<double>(double&& value) noexcept {
    type_ = Type::FLOAT64;
    size_ = sizeof(double);
    std::memcpy(inline_data_, &value, size_);
}

template <>
bool Value::get_impl<bool>() const noexcept {
    if (type_ != Type::BOOL || size_ != sizeof(bool))
        return false;
    bool result;
    std::memcpy(&result, inline_data_, sizeof(bool));
    return result;
}

template <>
int8_t Value::get_impl<int8_t>() const noexcept {
    if (type_ != Type::INT8 || size_ != sizeof(int8_t))
        return 0;
    int8_t result;
    std::memcpy(&result, inline_data_, sizeof(int8_t));
    return result;
}

template <>
int16_t Value::get_impl<int16_t>() const noexcept {
    if (type_ != Type::INT16 || size_ != sizeof(int16_t))
        return 0;
    int16_t result;
    std::memcpy(&result, inline_data_, sizeof(int16_t));
    return result;
}

template <>
uint8_t Value::get_impl<uint8_t>() const noexcept {
    if (type_ != Type::UINT8 || size_ != sizeof(uint8_t))
        return 0;
    uint8_t result;
    std::memcpy(&result, inline_data_, sizeof(uint8_t));
    return result;
}

template <>
uint16_t Value::get_impl<uint16_t>() const noexcept {
    if (type_ != Type::UINT16 || size_ != sizeof(uint16_t))
        return 0;
    uint16_t result;
    std::memcpy(&result, inline_data_, sizeof(uint16_t));
    return result;
}

template <>
int32_t Value::get_impl<int32_t>() const noexcept {
    if (type_ != Type::INT32 || size_ != sizeof(int32_t))
        return 0;
    int32_t result;
    std::memcpy(&result, inline_data_, sizeof(int32_t));
    return result;
}

template <>
uint32_t Value::get_impl<uint32_t>() const noexcept {
    if (type_ != Type::UINT32 || size_ != sizeof(uint32_t))
        return 0;
    uint32_t result;
    std::memcpy(&result, inline_data_, sizeof(uint32_t));
    return result;
}

template <>
int64_t Value::get_impl<int64_t>() const noexcept {
    if (type_ != Type::INT64 || size_ != sizeof(int64_t))
        return 0;
    int64_t result;
    std::memcpy(&result, inline_data_, sizeof(int64_t));
    return result;
}

template <>
uint64_t Value::get_impl<uint64_t>() const noexcept {
    if (type_ != Type::UINT64 || size_ != sizeof(uint64_t))
        return 0;
    uint64_t result;
    std::memcpy(&result, inline_data_, sizeof(uint64_t));
    return result;
}

template <>
float Value::get_impl<float>() const noexcept {
    if (type_ != Type::FLOAT32 || size_ != sizeof(float))
        return 0.0f;
    float result;
    std::memcpy(&result, inline_data_, sizeof(float));
    return result;
}

template <>
double Value::get_impl<double>() const noexcept {
    if (type_ != Type::FLOAT64 || size_ != sizeof(double))
        return 0.0;
    double result;
    std::memcpy(&result, inline_data_, sizeof(double));
    return result;
}

void Value::copy_from(const Value& other) noexcept {
    // Note: cleanup must be called by the caller if this is an assignment operation
    // (constructors don't need cleanup since there's nothing to clean)
    type_ = other.type_;
    size_ = other.size_;

    if (size_ <= INLINE_SIZE) {
        std::memcpy(inline_data_, other.inline_data_, size_);
    } else {
        // Use placement new since external_data_ may not be constructed
        new (&external_data_) std::unique_ptr<uint8_t[]>(std::make_unique<uint8_t[]>(size_));
        std::memcpy(external_data_.get(), other.external_data_.get(), size_);
    }
}

void Value::move_from(Value&& other) noexcept {
    // Note: cleanup must be called by the caller if this is an assignment operation
    // (constructors don't need cleanup since there's nothing to clean)
    type_ = other.type_;
    size_ = other.size_;

    if (size_ <= INLINE_SIZE) {
        std::memcpy(inline_data_, other.inline_data_, size_);
    } else {
        // Use placement new since external_data_ may not be constructed
        new (&external_data_) std::unique_ptr<uint8_t[]>(std::move(other.external_data_));
    }

    other.type_ = Type::EMPTY;
    other.size_ = 0;
}

void Value::cleanup() noexcept {
    if (size_ > INLINE_SIZE) {
        external_data_.~unique_ptr();
    }
    type_ = Type::EMPTY;
    size_ = 0;
}

// DataPoint implementation
void DataPoint::serialize(std::span<uint8_t> buffer) const noexcept {
    if (buffer.size() < serialized_size())
        return;

    size_t offset = 0;

    // Write address size and data
    std::memcpy(buffer.data() + offset, &address_size_, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    const char* addr_data =
        address_size_ <= MAX_INLINE_ADDRESS ? inline_address_ : external_address_.get();
    std::memcpy(buffer.data() + offset, addr_data, address_size_);
    offset += address_size_;

    // Write value
    auto value_size = value_.serialized_size();
    value_.serialize(std::span<uint8_t>(buffer.data() + offset, value_size));
    offset += value_size;

    // Write timestamp
    auto ts_ns = timestamp_.nanoseconds();
    std::memcpy(buffer.data() + offset, &ts_ns, sizeof(int64_t));
    offset += sizeof(int64_t);

    // Write metadata
    std::memcpy(buffer.data() + offset, &protocol_id_, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    std::memcpy(buffer.data() + offset, &quality_, sizeof(Quality));
    offset += sizeof(Quality);

    std::memcpy(buffer.data() + offset, &sequence_number_, sizeof(uint32_t));
}

bool DataPoint::deserialize(std::span<const uint8_t> buffer) noexcept {
    if (buffer.size() < sizeof(uint16_t))
        return false;

    // Clean up current external storage if any
    if (address_size_ > MAX_INLINE_ADDRESS) {
        external_address_.~unique_ptr();
    }

    size_t offset = 0;

    // Read address size
    uint16_t new_address_size;
    std::memcpy(&new_address_size, buffer.data() + offset, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    if (buffer.size() < offset + new_address_size)
        return false;

    address_size_ = new_address_size;

    // Read address
    if (address_size_ <= MAX_INLINE_ADDRESS) {
        std::memcpy(inline_address_, buffer.data() + offset, address_size_);
    } else {
        new (&external_address_) std::unique_ptr<char[]>(std::make_unique<char[]>(address_size_));
        std::memcpy(external_address_.get(), buffer.data() + offset, address_size_);
    }
    offset += address_size_;

    // Read value
    if (!value_.deserialize(
            std::span<const uint8_t>(buffer.data() + offset, buffer.size() - offset))) {
        return false;
    }
    offset += value_.serialized_size();

    if (buffer.size() <
        offset + sizeof(int64_t) + sizeof(uint16_t) + sizeof(Quality) + sizeof(uint32_t)) {
        return false;
    }

    // Read timestamp
    int64_t ts_ns;
    std::memcpy(&ts_ns, buffer.data() + offset, sizeof(int64_t));
    timestamp_ = Timestamp(std::chrono::nanoseconds(ts_ns));
    offset += sizeof(int64_t);

    // Read metadata
    std::memcpy(&protocol_id_, buffer.data() + offset, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    std::memcpy(&quality_, buffer.data() + offset, sizeof(Quality));
    offset += sizeof(Quality);

    std::memcpy(&sequence_number_, buffer.data() + offset, sizeof(uint32_t));

    return true;
}

size_t DataPoint::hash() const noexcept {
    auto addr = address();
    size_t h1 = std::hash<std::string_view>{}(addr);
    size_t h2 = std::hash<uint16_t>{}(protocol_id_);
    return h1 ^ (h2 << 1);
}

void DataPoint::copy_from(const DataPoint& other) noexcept {
    // Clean up current external storage if any
    if (address_size_ > MAX_INLINE_ADDRESS) {
        external_address_.~unique_ptr();
    }

    value_        = other.value_;
    timestamp_    = other.timestamp_;
    address_size_ = other.address_size_;

    if (other.address_size_ <= MAX_INLINE_ADDRESS) {
        std::memcpy(inline_address_, other.inline_address_, address_size_);
    } else {
        new (&external_address_) std::unique_ptr<char[]>(std::make_unique<char[]>(address_size_));
        std::memcpy(external_address_.get(), other.external_address_.get(), address_size_);
    }

    protocol_id_     = other.protocol_id_;
    quality_         = other.quality_;
    sequence_number_ = other.sequence_number_;
}

void DataPoint::move_from(DataPoint&& other) noexcept {
    // Clean up current external storage if any
    if (address_size_ > MAX_INLINE_ADDRESS) {
        external_address_.~unique_ptr();
    }

    value_        = std::move(other.value_);
    timestamp_    = other.timestamp_;
    address_size_ = other.address_size_;

    if (other.address_size_ <= MAX_INLINE_ADDRESS) {
        std::memcpy(inline_address_, other.inline_address_, address_size_);
    } else {
        new (&external_address_) std::unique_ptr<char[]>(std::move(other.external_address_));
    }

    protocol_id_     = other.protocol_id_;
    quality_         = other.quality_;
    sequence_number_ = other.sequence_number_;

    other.address_size_    = 0;
    other.protocol_id_     = 0;
    other.quality_         = Quality::INITIAL;
    other.sequence_number_ = 0;
}

}  // namespace ipb::common
