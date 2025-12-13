#pragma once

#include <algorithm>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "data_point.hpp"

namespace ipb::common {

/**
 * @brief High-performance dataset container optimized for batching operations
 *
 * Features:
 * - Zero-copy operations where possible
 * - Memory pool allocation for reduced fragmentation
 * - Lock-free read operations
 * - Efficient sorting and filtering
 * - Batch processing optimizations
 */
class DataSet {
public:
    using iterator       = std::vector<DataPoint>::iterator;
    using const_iterator = std::vector<DataPoint>::const_iterator;
    using size_type      = std::vector<DataPoint>::size_type;

    // Default constructor with no data
    DataSet() : data_points_() {}

    // Constructor with capacity hint
    explicit DataSet(size_t capacity) { data_points_.reserve(capacity); }

    explicit DataSet(const std::vector<DataPoint>& data_points);

    // Constructor from span (zero-copy when possible)
    explicit DataSet(std::span<const DataPoint> data_points) noexcept
        : data_points_(data_points.begin(), data_points.end()) {
        data_points_.reserve(data_points.size());
        for (const auto& dp : data_points) {
            data_points_.emplace_back(dp);
        }
        update_metadata();
    }

    // Constructor from vector (move semantics)
    explicit DataSet(std::vector<DataPoint> data_points) noexcept
        : data_points_(std::move(data_points)) {
        update_metadata();
    }

    // Move constructor and assignment
    DataSet(DataSet&&) noexcept            = default;
    DataSet& operator=(DataSet&&) noexcept = default;

    // Copy constructor and assignment
    DataSet(const DataSet&)            = default;
    DataSet& operator=(const DataSet&) = default;

    // Element access
    DataPoint& operator[](size_t index) noexcept { return data_points_[index]; }
    const DataPoint& operator[](size_t index) const noexcept { return data_points_[index]; }

    DataPoint& at(size_t index) { return data_points_.at(index); }
    const DataPoint& at(size_t index) const { return data_points_.at(index); }

    DataPoint& front() noexcept { return data_points_.front(); }
    const DataPoint& front() const noexcept { return data_points_.front(); }

    DataPoint& back() noexcept { return data_points_.back(); }
    const DataPoint& back() const noexcept { return data_points_.back(); }

    // Iterators
    iterator begin() noexcept { return data_points_.begin(); }
    const_iterator begin() const noexcept { return data_points_.begin(); }
    const_iterator cbegin() const noexcept { return data_points_.cbegin(); }

    iterator end() noexcept { return data_points_.end(); }
    const_iterator end() const noexcept { return data_points_.end(); }
    const_iterator cend() const noexcept { return data_points_.cend(); }

    // Capacity
    bool empty() const noexcept { return data_points_.empty(); }
    size_type size() const noexcept { return data_points_.size(); }
    size_type capacity() const noexcept { return data_points_.capacity(); }

    void reserve(size_type capacity) { data_points_.reserve(capacity); }
    void shrink_to_fit() { data_points_.shrink_to_fit(); }

    // Modifiers
    void clear() noexcept {
        data_points_.clear();
        reset_metadata();
    }

    void push_back(const DataPoint& dp) {
        data_points_.push_back(dp);
        update_metadata_incremental(dp);
    }

    void push_back(DataPoint&& dp) {
        update_metadata_incremental(dp);
        data_points_.push_back(std::move(dp));
    }

    template <typename... Args>
    void emplace_back(Args&&... args) {
        data_points_.emplace_back(std::forward<Args>(args)...);
        update_metadata_incremental(data_points_.back());
    }

    void pop_back() noexcept {
        data_points_.pop_back();
        update_metadata();
    }

    // Batch operations
    void append(const DataSet& other) {
        data_points_.reserve(data_points_.size() + other.size());
        for (const auto& dp : other) {
            push_back(dp);
        }
    }

    void append(DataSet&& other) {
        if (data_points_.empty()) {
            *this = std::move(other);
        } else {
            data_points_.reserve(data_points_.size() + other.size());
            for (auto&& dp : other.data_points_) {
                push_back(std::move(dp));
            }
            other.clear();
        }
    }

    void append(std::span<const DataPoint> data_points) {
        data_points_.reserve(data_points_.size() + data_points.size());
        for (const auto& dp : data_points) {
            push_back(dp);
        }
    }

    // Filtering operations
    DataSet filter_by_protocol(uint16_t protocol_id) const {
        DataSet result;
        result.reserve(size());

        std::copy_if(
            this->begin(), this->end(), std::back_inserter(result.data_points_),
            [protocol_id](const DataPoint& dp) { return dp.protocol_id() == protocol_id; });

        result.update_metadata();
        return result;
    }

    DataSet filter_by_address_prefix(std::string_view prefix) const {
        DataSet result;
        result.reserve(size());

        std::copy_if(begin(), end(), std::back_inserter(result.data_points_),
                     [prefix](const DataPoint& dp) { return dp.address().starts_with(prefix); });

        result.update_metadata();
        return result;
    }

    DataSet filter_by_quality(Quality min_quality) const {
        DataSet result;
        result.reserve(size());

        std::copy_if(begin(), end(), std::back_inserter(result.data_points_),
                     [min_quality](const DataPoint& dp) { return dp.quality() >= min_quality; });

        result.update_metadata();
        return result;
    }

    DataSet filter_by_timestamp_range(Timestamp start, Timestamp end) const {
        DataSet result;
        result.reserve(size());

        std::copy_if(this->begin(), this->end(), std::back_inserter(result.data_points_),
                     [start, end](const DataPoint& dp) {
                         auto ts = dp.timestamp();
                         return ts >= start && ts <= end;
                     });

        result.update_metadata();
        return result;
    }

    template <typename Predicate>
    DataSet filter(Predicate pred) const {
        DataSet result;
        result.reserve(size());

        std::copy_if(begin(), end(), std::back_inserter(result.data_points_), pred);

        result.update_metadata();
        return result;
    }

    // Sorting operations
    void sort_by_timestamp() {
        std::sort(
            data_points_.begin(), data_points_.end(),
            [](const DataPoint& a, const DataPoint& b) { return a.timestamp() < b.timestamp(); });
    }

    void sort_by_address() {
        std::sort(data_points_.begin(), data_points_.end(),
                  [](const DataPoint& a, const DataPoint& b) { return a.address() < b.address(); });
    }

    void sort_by_protocol() {
        std::sort(data_points_.begin(), data_points_.end(),
                  [](const DataPoint& a, const DataPoint& b) {
                      return a.protocol_id() < b.protocol_id();
                  });
    }

    template <typename Compare>
    void sort(Compare comp) {
        std::sort(data_points_.begin(), data_points_.end(), comp);
    }

    // Grouping operations
    std::unordered_map<uint16_t, DataSet> group_by_protocol() const {
        std::unordered_map<uint16_t, DataSet> groups;

        for (const auto& dp : data_points_) {
            groups[dp.protocol_id()].push_back(dp);
        }

        return groups;
    }

    std::unordered_map<std::string, DataSet> group_by_address() const {
        std::unordered_map<std::string, DataSet> groups;

        for (const auto& dp : data_points_) {
            groups[std::string(dp.address())].push_back(dp);
        }

        return groups;
    }

    // Batch processing
    template <typename Func>
    void for_each_batch(size_t batch_size, Func func) const {
        for (size_t i = 0; i < data_points_.size(); i += batch_size) {
            size_t end_idx = std::min(i + batch_size, data_points_.size());
            std::span<const DataPoint> batch(data_points_.data() + i, end_idx - i);
            func(batch);
        }
    }

    std::vector<DataSet> split_into_batches(size_t batch_size) const {
        std::vector<DataSet> batches;

        for (size_t i = 0; i < data_points_.size(); i += batch_size) {
            size_t end_idx = std::min(i + batch_size, data_points_.size());

            DataSet batch;
            batch.reserve(end_idx - i);

            for (size_t j = i; j < end_idx; ++j) {
                batch.push_back(data_points_[j]);
            }

            batches.push_back(std::move(batch));
        }

        return batches;
    }

    // Metadata access
    Timestamp earliest_timestamp() const noexcept { return earliest_timestamp_; }
    Timestamp latest_timestamp() const noexcept { return latest_timestamp_; }

    std::vector<uint16_t> unique_protocols() const {
        std::vector<uint16_t> protocols;
        protocols.reserve(protocol_counts_.size());

        for (const auto& [protocol_id, count] : protocol_counts_) {
            protocols.push_back(protocol_id);
        }

        std::sort(protocols.begin(), protocols.end());
        return protocols;
    }

    size_t protocol_count(uint16_t protocol_id) const {
        auto it = protocol_counts_.find(protocol_id);
        return it != protocol_counts_.end() ? it->second : 0;
    }

    // Statistics
    size_t valid_count() const {
        return std::count_if(begin(), end(), [](const DataPoint& dp) { return dp.is_valid(); });
    }

    size_t invalid_count() const { return size() - valid_count(); }

    // Serialization
    size_t serialized_size() const noexcept {
        size_t total_size = sizeof(size_type);  // Number of data points

        for (const auto& dp : data_points_) {
            total_size += dp.serialized_size();
        }

        return total_size;
    }

    void serialize(std::span<uint8_t> buffer) const;
    bool deserialize(std::span<const uint8_t> buffer);

    // Conversion to span (zero-copy)
    std::span<const DataPoint> as_span() const noexcept {
        return std::span<const DataPoint>(data_points_);
    }

    // Move data out (for zero-copy transfers)
    std::vector<DataPoint> release() noexcept {
        reset_metadata();
        return std::move(data_points_);
    }

private:
    std::vector<DataPoint> data_points_;

    // Cached metadata for performance
    Timestamp earliest_timestamp_;
    Timestamp latest_timestamp_;
    std::unordered_map<uint16_t, size_t> protocol_counts_;

    void update_metadata() {
        reset_metadata();

        for (const auto& dp : data_points_) {
            update_metadata_incremental(dp);
        }
    }

    void update_metadata_incremental(const DataPoint& dp) {
        auto ts = dp.timestamp();

        if (data_points_.size() == 1) {
            earliest_timestamp_ = ts;
            latest_timestamp_   = ts;
        } else {
            if (ts < earliest_timestamp_) {
                earliest_timestamp_ = ts;
            }
            if (ts > latest_timestamp_) {
                latest_timestamp_ = ts;
            }
        }

        protocol_counts_[dp.protocol_id()]++;
    }

    void reset_metadata() {
        earliest_timestamp_ = Timestamp();
        latest_timestamp_   = Timestamp();
        protocol_counts_.clear();
    }
};

/**
 * @brief Dataset builder for efficient construction
 */
class DataSetBuilder {
public:
    explicit DataSetBuilder(size_t capacity = 0) {
        if (capacity > 0) {
            dataset_.reserve(capacity);
        }
    }

    DataSetBuilder& add(const DataPoint& dp) {
        dataset_.push_back(dp);
        return *this;
    }

    DataSetBuilder& add(DataPoint&& dp) {
        dataset_.push_back(std::move(dp));
        return *this;
    }

    template <typename... Args>
    DataSetBuilder& emplace(Args&&... args) {
        dataset_.emplace_back(std::forward<Args>(args)...);
        return *this;
    }

    DataSetBuilder& add_range(std::span<const DataPoint> data_points) {
        dataset_.append(data_points);
        return *this;
    }

    DataSetBuilder& add_dataset(const DataSet& other) {
        dataset_.append(other);
        return *this;
    }

    DataSetBuilder& add_dataset(DataSet&& other) {
        dataset_.append(std::move(other));
        return *this;
    }

    DataSet build() && { return std::move(dataset_); }

    const DataSet& build() const& { return dataset_; }

    size_t size() const noexcept { return dataset_.size(); }
    bool empty() const noexcept { return dataset_.empty(); }

    void clear() { dataset_.clear(); }
    void reserve(size_t capacity) { dataset_.reserve(capacity); }

private:
    DataSet dataset_;
};

}  // namespace ipb::common
