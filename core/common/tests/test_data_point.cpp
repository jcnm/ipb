#include <ipb/common/data_point.hpp>

#include <gtest/gtest.h>

using namespace ipb::common;

TEST(DataPointTest, BasicConstruction) {
    Value val;
    val.set(42.0);
    DataPoint dp("test_address", val, 1);

    EXPECT_EQ(dp.get_address(), "test_address");
    EXPECT_EQ(dp.get_protocol_id(), 1);
    EXPECT_DOUBLE_EQ(dp.value().get<double>(), 42.0);
    EXPECT_EQ(dp.get_quality(), Quality::GOOD);
}

TEST(DataPointTest, QualityHandling) {
    Value val;
    val.set(1.0);
    DataPoint dp("test", val, 2);

    dp.set_quality(Quality::BAD);
    EXPECT_EQ(dp.get_quality(), Quality::BAD);

    dp.set_quality(Quality::UNCERTAIN);
    EXPECT_EQ(dp.get_quality(), Quality::UNCERTAIN);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
