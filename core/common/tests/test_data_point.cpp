#include <ipb/common/data_point.hpp>

#include <gtest/gtest.h>

using namespace ipb::common;

TEST(DataPointTest, BasicConstruction) {
    DataPoint dp("test_address", "test_protocol", 42.0);

    EXPECT_EQ(dp.get_address(), "test_address");
    EXPECT_EQ(dp.get_protocol_id(), "test_protocol");
    EXPECT_EQ(dp.get_value<double>(), 42.0);
    EXPECT_EQ(dp.get_quality(), DataQuality::GOOD);
}

TEST(DataPointTest, QualityHandling) {
    DataPoint dp("test", "proto", 1.0);

    dp.set_quality(DataQuality::BAD);
    EXPECT_EQ(dp.get_quality(), DataQuality::BAD);

    dp.set_quality(DataQuality::UNCERTAIN);
    EXPECT_EQ(dp.get_quality(), DataQuality::UNCERTAIN);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
