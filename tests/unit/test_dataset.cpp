/**
 * @file test_dataset.cpp
 * @brief Comprehensive unit tests for ipb::common::DataSet
 *
 * Tests cover:
 * - DataSet construction and destruction
 * - Element access (operator[], at, front, back)
 * - Iterators (begin, end, cbegin, cend)
 * - Capacity (empty, size, capacity, reserve, shrink_to_fit)
 * - Modifiers (clear, push_back, emplace_back, pop_back, append)
 * - Filtering operations (by protocol, address, quality, timestamp, predicate)
 * - Sorting operations (by timestamp, address, protocol, custom)
 * - Grouping operations (by protocol, by address)
 * - Batch processing (for_each_batch, split_into_batches)
 * - Metadata (earliest/latest timestamp, unique protocols, protocol count)
 * - Statistics (valid_count, invalid_count)
 * - Serialization (serialized_size, as_span, release)
 * - DataSetBuilder
 */

#include <ipb/common/dataset.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace ipb::common;
using namespace std::chrono_literals;

// ============================================================================
// DataSet Construction Tests
// ============================================================================

class DataSetConstructionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create sample data points for testing
        for (int i = 0; i < 10; ++i) {
            DataPoint dp("sensor/temp" + std::to_string(i));
            dp.set_value(static_cast<double>(20.0 + i));
            dp.set_protocol_id(static_cast<uint16_t>(i % 3));
            dp.set_quality(Quality::GOOD);
            sample_data_.push_back(dp);
        }
    }

    std::vector<DataPoint> sample_data_;
};

TEST_F(DataSetConstructionTest, DefaultConstruction) {
    DataSet ds;
    EXPECT_TRUE(ds.empty());
    EXPECT_EQ(ds.size(), 0u);
}

TEST_F(DataSetConstructionTest, ConstructWithCapacity) {
    DataSet ds(100);
    EXPECT_TRUE(ds.empty());
    EXPECT_GE(ds.capacity(), 100u);
}

TEST_F(DataSetConstructionTest, ConstructFromVector) {
    DataSet ds(sample_data_);
    EXPECT_EQ(ds.size(), sample_data_.size());
    EXPECT_FALSE(ds.empty());
}

TEST_F(DataSetConstructionTest, ConstructFromSpan) {
    std::span<const DataPoint> span(sample_data_);
    DataSet ds(span);
    EXPECT_EQ(ds.size(), sample_data_.size());
}

TEST_F(DataSetConstructionTest, ConstructFromMoveVector) {
    auto copy = sample_data_;
    DataSet ds(std::move(copy));
    EXPECT_EQ(ds.size(), sample_data_.size());
}

TEST_F(DataSetConstructionTest, CopyConstruction) {
    DataSet ds1(sample_data_);
    DataSet ds2(ds1);

    EXPECT_EQ(ds1.size(), ds2.size());
    for (size_t i = 0; i < ds1.size(); ++i) {
        EXPECT_EQ(ds1[i].address(), ds2[i].address());
    }
}

TEST_F(DataSetConstructionTest, MoveConstruction) {
    DataSet ds1(sample_data_);
    size_t original_size = ds1.size();

    DataSet ds2(std::move(ds1));
    EXPECT_EQ(ds2.size(), original_size);
}

TEST_F(DataSetConstructionTest, CopyAssignment) {
    DataSet ds1(sample_data_);
    DataSet ds2;
    ds2 = ds1;

    EXPECT_EQ(ds1.size(), ds2.size());
}

TEST_F(DataSetConstructionTest, MoveAssignment) {
    DataSet ds1(sample_data_);
    size_t original_size = ds1.size();

    DataSet ds2;
    ds2 = std::move(ds1);
    EXPECT_EQ(ds2.size(), original_size);
}

// ============================================================================
// DataSet Element Access Tests
// ============================================================================

class DataSetAccessTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int i = 0; i < 5; ++i) {
            DataPoint dp("sensor/item" + std::to_string(i));
            dp.set_value(static_cast<double>(i));
            ds_.push_back(dp);
        }
    }

    DataSet ds_;
};

TEST_F(DataSetAccessTest, OperatorBracket) {
    EXPECT_EQ(ds_[0].address(), "sensor/item0");
    EXPECT_EQ(ds_[4].address(), "sensor/item4");
}

TEST_F(DataSetAccessTest, At) {
    EXPECT_EQ(ds_.at(0).address(), "sensor/item0");
    EXPECT_THROW(ds_.at(100), std::out_of_range);
}

TEST_F(DataSetAccessTest, Front) {
    EXPECT_EQ(ds_.front().address(), "sensor/item0");
}

TEST_F(DataSetAccessTest, Back) {
    EXPECT_EQ(ds_.back().address(), "sensor/item4");
}

TEST_F(DataSetAccessTest, Iterators) {
    int count = 0;
    for (auto it = ds_.begin(); it != ds_.end(); ++it) {
        ++count;
    }
    EXPECT_EQ(count, 5);
}

TEST_F(DataSetAccessTest, ConstIterators) {
    const DataSet& const_ds = ds_;
    int count = 0;
    for (auto it = const_ds.cbegin(); it != const_ds.cend(); ++it) {
        ++count;
    }
    EXPECT_EQ(count, 5);
}

TEST_F(DataSetAccessTest, RangeBasedFor) {
    int count = 0;
    for (const auto& dp : ds_) {
        (void)dp;
        ++count;
    }
    EXPECT_EQ(count, 5);
}

// ============================================================================
// DataSet Capacity Tests
// ============================================================================

class DataSetCapacityTest : public ::testing::Test {};

TEST_F(DataSetCapacityTest, Empty) {
    DataSet ds;
    EXPECT_TRUE(ds.empty());

    ds.push_back(DataPoint("test"));
    EXPECT_FALSE(ds.empty());
}

TEST_F(DataSetCapacityTest, Size) {
    DataSet ds;
    EXPECT_EQ(ds.size(), 0u);

    for (int i = 0; i < 10; ++i) {
        ds.push_back(DataPoint("test" + std::to_string(i)));
    }
    EXPECT_EQ(ds.size(), 10u);
}

TEST_F(DataSetCapacityTest, Reserve) {
    DataSet ds;
    ds.reserve(1000);
    EXPECT_GE(ds.capacity(), 1000u);
    EXPECT_TRUE(ds.empty());
}

TEST_F(DataSetCapacityTest, ShrinkToFit) {
    DataSet ds(1000);
    for (int i = 0; i < 10; ++i) {
        ds.push_back(DataPoint("test"));
    }

    ds.shrink_to_fit();
    // After shrink_to_fit, capacity should be close to size
    EXPECT_LE(ds.capacity(), ds.size() * 2);
}

// ============================================================================
// DataSet Modifier Tests
// ============================================================================

class DataSetModifierTest : public ::testing::Test {};

TEST_F(DataSetModifierTest, Clear) {
    DataSet ds;
    for (int i = 0; i < 10; ++i) {
        ds.push_back(DataPoint("test"));
    }

    EXPECT_EQ(ds.size(), 10u);
    ds.clear();
    EXPECT_TRUE(ds.empty());
}

TEST_F(DataSetModifierTest, PushBackCopy) {
    DataSet ds;
    DataPoint dp("test");
    dp.set_value(42.0);

    ds.push_back(dp);

    EXPECT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].address(), "test");
}

TEST_F(DataSetModifierTest, PushBackMove) {
    DataSet ds;
    DataPoint dp("test");
    dp.set_value(42.0);

    ds.push_back(std::move(dp));

    EXPECT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].address(), "test");
}

TEST_F(DataSetModifierTest, EmplaceBack) {
    DataSet ds;
    ds.emplace_back("test_address");

    EXPECT_EQ(ds.size(), 1u);
    EXPECT_EQ(ds[0].address(), "test_address");
}

TEST_F(DataSetModifierTest, PopBack) {
    DataSet ds;
    for (int i = 0; i < 5; ++i) {
        ds.push_back(DataPoint("test" + std::to_string(i)));
    }

    EXPECT_EQ(ds.size(), 5u);
    ds.pop_back();
    EXPECT_EQ(ds.size(), 4u);
    EXPECT_EQ(ds.back().address(), "test3");
}

TEST_F(DataSetModifierTest, AppendDataSet) {
    DataSet ds1;
    DataSet ds2;

    for (int i = 0; i < 5; ++i) {
        ds1.push_back(DataPoint("ds1_" + std::to_string(i)));
        ds2.push_back(DataPoint("ds2_" + std::to_string(i)));
    }

    ds1.append(ds2);
    EXPECT_EQ(ds1.size(), 10u);
}

TEST_F(DataSetModifierTest, AppendMoveDataSet) {
    DataSet ds1;
    DataSet ds2;

    for (int i = 0; i < 5; ++i) {
        ds1.push_back(DataPoint("ds1_" + std::to_string(i)));
        ds2.push_back(DataPoint("ds2_" + std::to_string(i)));
    }

    ds1.append(std::move(ds2));
    EXPECT_EQ(ds1.size(), 10u);
}

TEST_F(DataSetModifierTest, AppendSpan) {
    DataSet ds;
    std::vector<DataPoint> data;
    for (int i = 0; i < 5; ++i) {
        data.emplace_back("span_" + std::to_string(i));
    }

    ds.append(std::span<const DataPoint>(data));
    EXPECT_EQ(ds.size(), 5u);
}

TEST_F(DataSetModifierTest, AppendToEmptyWithMove) {
    DataSet ds1;
    DataSet ds2;

    for (int i = 0; i < 5; ++i) {
        ds2.push_back(DataPoint("ds2_" + std::to_string(i)));
    }

    ds1.append(std::move(ds2));
    EXPECT_EQ(ds1.size(), 5u);
}

// ============================================================================
// DataSet Filtering Tests
// ============================================================================

class DataSetFilterTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int i = 0; i < 20; ++i) {
            DataPoint dp("sensor/temp" + std::to_string(i));
            dp.set_value(static_cast<double>(i));
            dp.set_protocol_id(static_cast<uint16_t>(i % 4));
            dp.set_quality(i < 15 ? Quality::GOOD : Quality::BAD);
            ds_.push_back(dp);
        }
    }

    DataSet ds_;
};

TEST_F(DataSetFilterTest, FilterByProtocol) {
    auto filtered = ds_.filter_by_protocol(0);

    EXPECT_EQ(filtered.size(), 5u);
    for (const auto& dp : filtered) {
        EXPECT_EQ(dp.protocol_id(), 0);
    }
}

TEST_F(DataSetFilterTest, FilterByAddressPrefix) {
    // All addresses start with "sensor/temp"
    auto filtered = ds_.filter_by_address_prefix("sensor/temp1");

    // Should match temp1, temp10-19
    EXPECT_GE(filtered.size(), 1u);
    for (const auto& dp : filtered) {
        EXPECT_TRUE(dp.address().starts_with("sensor/temp1"));
    }
}

TEST_F(DataSetFilterTest, FilterByQuality) {
    auto filtered = ds_.filter_by_quality(Quality::GOOD);

    EXPECT_EQ(filtered.size(), 15u);
    for (const auto& dp : filtered) {
        EXPECT_GE(dp.quality(), Quality::GOOD);
    }
}

TEST_F(DataSetFilterTest, FilterByTimestampRange) {
    // Set specific timestamps
    DataSet ds;
    for (int i = 0; i < 10; ++i) {
        DataPoint dp("test" + std::to_string(i));
        dp.set_timestamp(Timestamp(std::chrono::nanoseconds(i * 1000)));
        ds.push_back(dp);
    }

    Timestamp start(std::chrono::nanoseconds(2000));
    Timestamp end(std::chrono::nanoseconds(7000));

    auto filtered = ds.filter_by_timestamp_range(start, end);

    EXPECT_GE(filtered.size(), 5u);
}

TEST_F(DataSetFilterTest, FilterWithPredicate) {
    auto filtered = ds_.filter([](const DataPoint& dp) {
        return dp.protocol_id() == 1;
    });

    for (const auto& dp : filtered) {
        EXPECT_EQ(dp.protocol_id(), 1);
    }
}

// ============================================================================
// DataSet Sorting Tests
// ============================================================================

class DataSetSortTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Add data points in random order
        for (int i = 9; i >= 0; --i) {
            DataPoint dp("sensor" + std::to_string(i));
            dp.set_timestamp(Timestamp(std::chrono::nanoseconds(i * 1000)));
            dp.set_protocol_id(static_cast<uint16_t>((10 - i) % 5));
            ds_.push_back(dp);
        }
    }

    DataSet ds_;
};

TEST_F(DataSetSortTest, SortByTimestamp) {
    ds_.sort_by_timestamp();

    for (size_t i = 1; i < ds_.size(); ++i) {
        EXPECT_LE(ds_[i-1].timestamp(), ds_[i].timestamp());
    }
}

TEST_F(DataSetSortTest, SortByAddress) {
    ds_.sort_by_address();

    for (size_t i = 1; i < ds_.size(); ++i) {
        EXPECT_LE(ds_[i-1].address(), ds_[i].address());
    }
}

TEST_F(DataSetSortTest, SortByProtocol) {
    ds_.sort_by_protocol();

    for (size_t i = 1; i < ds_.size(); ++i) {
        EXPECT_LE(ds_[i-1].protocol_id(), ds_[i].protocol_id());
    }
}

TEST_F(DataSetSortTest, SortCustom) {
    // Sort by descending timestamp
    ds_.sort([](const DataPoint& a, const DataPoint& b) {
        return a.timestamp() > b.timestamp();
    });

    for (size_t i = 1; i < ds_.size(); ++i) {
        EXPECT_GE(ds_[i-1].timestamp(), ds_[i].timestamp());
    }
}

// ============================================================================
// DataSet Grouping Tests
// ============================================================================

class DataSetGroupTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int i = 0; i < 20; ++i) {
            DataPoint dp("sensor/type" + std::to_string(i % 4));
            dp.set_protocol_id(static_cast<uint16_t>(i % 3));
            ds_.push_back(dp);
        }
    }

    DataSet ds_;
};

TEST_F(DataSetGroupTest, GroupByProtocol) {
    auto groups = ds_.group_by_protocol();

    EXPECT_EQ(groups.size(), 3u);

    size_t total = 0;
    for (const auto& [protocol_id, group] : groups) {
        total += group.size();
        for (const auto& dp : group) {
            EXPECT_EQ(dp.protocol_id(), protocol_id);
        }
    }
    EXPECT_EQ(total, ds_.size());
}

TEST_F(DataSetGroupTest, GroupByAddress) {
    auto groups = ds_.group_by_address();

    EXPECT_EQ(groups.size(), 4u);

    size_t total = 0;
    for (const auto& [address, group] : groups) {
        total += group.size();
        for (const auto& dp : group) {
            EXPECT_EQ(std::string(dp.address()), address);
        }
    }
    EXPECT_EQ(total, ds_.size());
}

// ============================================================================
// DataSet Batch Processing Tests
// ============================================================================

class DataSetBatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int i = 0; i < 100; ++i) {
            ds_.push_back(DataPoint("test" + std::to_string(i)));
        }
    }

    DataSet ds_;
};

TEST_F(DataSetBatchTest, ForEachBatch) {
    std::vector<size_t> batch_sizes;

    ds_.for_each_batch(25, [&batch_sizes](std::span<const DataPoint> batch) {
        batch_sizes.push_back(batch.size());
    });

    EXPECT_EQ(batch_sizes.size(), 4u);
    for (auto size : batch_sizes) {
        EXPECT_EQ(size, 25u);
    }
}

TEST_F(DataSetBatchTest, ForEachBatchUnevenSize) {
    // 100 items with batch size 30 = 3 batches of 30 + 1 batch of 10
    std::vector<size_t> batch_sizes;

    ds_.for_each_batch(30, [&batch_sizes](std::span<const DataPoint> batch) {
        batch_sizes.push_back(batch.size());
    });

    EXPECT_EQ(batch_sizes.size(), 4u);
    EXPECT_EQ(batch_sizes[3], 10u);
}

TEST_F(DataSetBatchTest, SplitIntoBatches) {
    auto batches = ds_.split_into_batches(25);

    EXPECT_EQ(batches.size(), 4u);

    size_t total = 0;
    for (const auto& batch : batches) {
        total += batch.size();
    }
    EXPECT_EQ(total, 100u);
}

// ============================================================================
// DataSet Metadata Tests
// ============================================================================

class DataSetMetadataTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int i = 0; i < 10; ++i) {
            DataPoint dp("test" + std::to_string(i));
            dp.set_timestamp(Timestamp(std::chrono::nanoseconds((i + 1) * 1000)));
            dp.set_protocol_id(static_cast<uint16_t>(i % 3));
            ds_.push_back(dp);
        }
    }

    DataSet ds_;
};

TEST_F(DataSetMetadataTest, EarliestTimestamp) {
    EXPECT_EQ(ds_.earliest_timestamp().nanoseconds(), 1000);
}

TEST_F(DataSetMetadataTest, LatestTimestamp) {
    EXPECT_EQ(ds_.latest_timestamp().nanoseconds(), 10000);
}

TEST_F(DataSetMetadataTest, UniqueProtocols) {
    auto protocols = ds_.unique_protocols();

    EXPECT_EQ(protocols.size(), 3u);
    EXPECT_TRUE(std::find(protocols.begin(), protocols.end(), 0) != protocols.end());
    EXPECT_TRUE(std::find(protocols.begin(), protocols.end(), 1) != protocols.end());
    EXPECT_TRUE(std::find(protocols.begin(), protocols.end(), 2) != protocols.end());
}

TEST_F(DataSetMetadataTest, ProtocolCount) {
    // Protocol 0: indices 0, 3, 6, 9 = 4 items
    // Protocol 1: indices 1, 4, 7 = 3 items
    // Protocol 2: indices 2, 5, 8 = 3 items
    EXPECT_EQ(ds_.protocol_count(0), 4u);
    EXPECT_EQ(ds_.protocol_count(1), 3u);
    EXPECT_EQ(ds_.protocol_count(2), 3u);
    EXPECT_EQ(ds_.protocol_count(99), 0u);
}

// ============================================================================
// DataSet Statistics Tests
// ============================================================================

class DataSetStatisticsTest : public ::testing::Test {};

TEST_F(DataSetStatisticsTest, ValidCount) {
    DataSet ds;
    for (int i = 0; i < 10; ++i) {
        DataPoint dp("test" + std::to_string(i));
        dp.set_value(static_cast<double>(i));
        dp.set_valid(i < 7);
        ds.push_back(dp);
    }

    EXPECT_EQ(ds.valid_count(), 7u);
}

TEST_F(DataSetStatisticsTest, InvalidCount) {
    DataSet ds;
    for (int i = 0; i < 10; ++i) {
        DataPoint dp("test" + std::to_string(i));
        dp.set_value(static_cast<double>(i));
        dp.set_valid(i < 7);
        ds.push_back(dp);
    }

    EXPECT_EQ(ds.invalid_count(), 3u);
}

// ============================================================================
// DataSet Serialization Tests
// ============================================================================

class DataSetSerializationTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int i = 0; i < 5; ++i) {
            DataPoint dp("test" + std::to_string(i));
            dp.set_value(static_cast<double>(i));
            ds_.push_back(dp);
        }
    }

    DataSet ds_;
};

TEST_F(DataSetSerializationTest, SerializedSize) {
    size_t size = ds_.serialized_size();
    EXPECT_GT(size, 0u);
}

TEST_F(DataSetSerializationTest, AsSpan) {
    auto span = ds_.as_span();

    EXPECT_EQ(span.size(), ds_.size());
    for (size_t i = 0; i < span.size(); ++i) {
        EXPECT_EQ(span[i].address(), ds_[i].address());
    }
}

TEST_F(DataSetSerializationTest, Release) {
    size_t original_size = ds_.size();

    auto released = ds_.release();

    EXPECT_EQ(released.size(), original_size);
    EXPECT_TRUE(ds_.empty());
}

// ============================================================================
// DataSetBuilder Tests
// ============================================================================

class DataSetBuilderTest : public ::testing::Test {};

TEST_F(DataSetBuilderTest, DefaultConstruction) {
    DataSetBuilder builder;
    EXPECT_TRUE(builder.empty());
    EXPECT_EQ(builder.size(), 0u);
}

TEST_F(DataSetBuilderTest, ConstructWithCapacity) {
    DataSetBuilder builder(100);
    EXPECT_TRUE(builder.empty());
}

TEST_F(DataSetBuilderTest, AddCopy) {
    DataSetBuilder builder;
    DataPoint dp("test");

    builder.add(dp);

    EXPECT_EQ(builder.size(), 1u);
}

TEST_F(DataSetBuilderTest, AddMove) {
    DataSetBuilder builder;

    builder.add(DataPoint("test"));

    EXPECT_EQ(builder.size(), 1u);
}

TEST_F(DataSetBuilderTest, Emplace) {
    DataSetBuilder builder;

    builder.emplace("test_address");

    EXPECT_EQ(builder.size(), 1u);
}

TEST_F(DataSetBuilderTest, AddRange) {
    DataSetBuilder builder;
    std::vector<DataPoint> data;
    for (int i = 0; i < 5; ++i) {
        data.emplace_back("test" + std::to_string(i));
    }

    builder.add_range(std::span<const DataPoint>(data));

    EXPECT_EQ(builder.size(), 5u);
}

TEST_F(DataSetBuilderTest, AddDataSet) {
    DataSetBuilder builder;
    DataSet ds;
    for (int i = 0; i < 5; ++i) {
        ds.push_back(DataPoint("test" + std::to_string(i)));
    }

    builder.add_dataset(ds);

    EXPECT_EQ(builder.size(), 5u);
}

TEST_F(DataSetBuilderTest, BuildMove) {
    DataSetBuilder builder;
    for (int i = 0; i < 5; ++i) {
        builder.add(DataPoint("test" + std::to_string(i)));
    }

    DataSet ds = std::move(builder).build();

    EXPECT_EQ(ds.size(), 5u);
}

TEST_F(DataSetBuilderTest, BuildConstRef) {
    DataSetBuilder builder;
    for (int i = 0; i < 5; ++i) {
        builder.add(DataPoint("test" + std::to_string(i)));
    }

    const DataSet& ds = builder.build();

    EXPECT_EQ(ds.size(), 5u);
}

TEST_F(DataSetBuilderTest, Clear) {
    DataSetBuilder builder;
    for (int i = 0; i < 5; ++i) {
        builder.add(DataPoint("test" + std::to_string(i)));
    }

    builder.clear();

    EXPECT_TRUE(builder.empty());
}

TEST_F(DataSetBuilderTest, Reserve) {
    DataSetBuilder builder;
    builder.reserve(1000);

    // Should not throw when adding many items
    for (int i = 0; i < 500; ++i) {
        builder.add(DataPoint("test" + std::to_string(i)));
    }

    EXPECT_EQ(builder.size(), 500u);
}

TEST_F(DataSetBuilderTest, FluentAPI) {
    DataSet ds = DataSetBuilder(10)
        .add(DataPoint("test1"))
        .add(DataPoint("test2"))
        .emplace("test3")
        .build();

    EXPECT_EQ(ds.size(), 3u);
}
