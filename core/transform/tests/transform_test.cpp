/**
 * @file transform_test.cpp
 * @brief Unit tests for IPB Transform module
 */

#include <gtest/gtest.h>
#include <ipb/transform/transform.hpp>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

using namespace ipb::transform;

// ============================================================================
// Test Utilities
// ============================================================================

/**
 * @brief Generate random test data
 */
std::vector<uint8_t> generate_random_data(size_t size, uint32_t seed = 42) {
    std::vector<uint8_t> data(size);
    std::mt19937 gen(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& byte : data) {
        byte = static_cast<uint8_t>(dist(gen));
    }
    return data;
}

/**
 * @brief Generate compressible test data (repeated patterns)
 */
std::vector<uint8_t> generate_compressible_data(size_t size) {
    std::vector<uint8_t> data(size);
    const std::string pattern = "Hello, IPB Transform! This is a test pattern that should compress well. ";
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(pattern[i % pattern.size()]);
    }
    return data;
}

// ============================================================================
// NullTransformer Tests
// ============================================================================

TEST(NullTransformerTest, PassthroughEmpty) {
    NullTransformer transformer;
    std::vector<uint8_t> empty;

    auto result = transformer.transform(empty);
    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(result.value().empty());

    auto inverse = transformer.inverse(empty);
    ASSERT_TRUE(inverse.is_success());
    EXPECT_TRUE(inverse.value().empty());
}

TEST(NullTransformerTest, PassthroughData) {
    NullTransformer transformer;
    auto data = generate_random_data(1024);

    auto result = transformer.transform(data);
    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), data);

    auto inverse = transformer.inverse(data);
    ASSERT_TRUE(inverse.is_success());
    EXPECT_EQ(inverse.value(), data);
}

TEST(NullTransformerTest, Bijectivity) {
    NullTransformer transformer;
    auto data = generate_random_data(4096);

    auto transformed = transformer.transform(data);
    ASSERT_TRUE(transformed.is_success());

    auto recovered = transformer.inverse(transformed.value());
    ASSERT_TRUE(recovered.is_success());
    EXPECT_EQ(recovered.value(), data);
}

// ============================================================================
// Base64 Transformer Tests
// ============================================================================

TEST(Base64TransformerTest, EncodeDecodeEmpty) {
    Base64Transformer transformer;
    std::vector<uint8_t> empty;

    auto encoded = transformer.transform(empty);
    ASSERT_TRUE(encoded.is_success());

    auto decoded = transformer.inverse(encoded.value());
    ASSERT_TRUE(decoded.is_success());
    EXPECT_TRUE(decoded.value().empty());
}

TEST(Base64TransformerTest, EncodeDecodeSimple) {
    Base64Transformer transformer;
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};

    auto encoded = transformer.transform(data);
    ASSERT_TRUE(encoded.is_success());

    // Check that output is Base64 (ASCII printable)
    for (uint8_t c : encoded.value()) {
        EXPECT_TRUE((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=');
    }

    auto decoded = transformer.inverse(encoded.value());
    ASSERT_TRUE(decoded.is_success());
    EXPECT_EQ(decoded.value(), data);
}

TEST(Base64TransformerTest, Bijectivity) {
    Base64Transformer transformer;
    auto data = generate_random_data(1000);

    auto encoded = transformer.transform(data);
    ASSERT_TRUE(encoded.is_success());

    auto decoded = transformer.inverse(encoded.value());
    ASSERT_TRUE(decoded.is_success());
    EXPECT_EQ(decoded.value(), data);
}

TEST(Base64TransformerTest, UrlSafeVariant) {
    Base64Transformer transformer(Base64Transformer::Variant::URL_SAFE);
    auto data = generate_random_data(256);

    auto encoded = transformer.transform(data);
    ASSERT_TRUE(encoded.is_success());

    // URL-safe should not contain + or /
    for (uint8_t c : encoded.value()) {
        EXPECT_NE(c, '+');
        EXPECT_NE(c, '/');
    }

    auto decoded = transformer.inverse(encoded.value());
    ASSERT_TRUE(decoded.is_success());
    EXPECT_EQ(decoded.value(), data);
}

// ============================================================================
// Hex Transformer Tests
// ============================================================================

TEST(HexTransformerTest, EncodeDecodeSimple) {
    HexTransformer transformer;
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};

    auto encoded = transformer.transform(data);
    ASSERT_TRUE(encoded.is_success());
    EXPECT_EQ(encoded.value().size(), 8);  // 4 bytes -> 8 hex chars

    std::string hex_str(encoded.value().begin(), encoded.value().end());
    EXPECT_EQ(hex_str, "deadbeef");

    auto decoded = transformer.inverse(encoded.value());
    ASSERT_TRUE(decoded.is_success());
    EXPECT_EQ(decoded.value(), data);
}

TEST(HexTransformerTest, Bijectivity) {
    HexTransformer transformer;
    auto data = generate_random_data(512);

    auto encoded = transformer.transform(data);
    ASSERT_TRUE(encoded.is_success());
    EXPECT_EQ(encoded.value().size(), data.size() * 2);

    auto decoded = transformer.inverse(encoded.value());
    ASSERT_TRUE(decoded.is_success());
    EXPECT_EQ(decoded.value(), data);
}

TEST(HexTransformerTest, UppercaseVariant) {
    HexTransformer transformer(true);  // uppercase
    std::vector<uint8_t> data = {0xAB, 0xCD};

    auto encoded = transformer.transform(data);
    ASSERT_TRUE(encoded.is_success());

    std::string hex_str(encoded.value().begin(), encoded.value().end());
    EXPECT_EQ(hex_str, "ABCD");
}

// ============================================================================
// CRC32 Transformer Tests
// ============================================================================

TEST(Crc32TransformerTest, AddChecksum) {
    Crc32Transformer transformer;
    std::vector<uint8_t> data = {'t', 'e', 's', 't'};

    auto result = transformer.transform(data);
    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), data.size() + 4);  // +4 bytes for CRC32
}

TEST(Crc32TransformerTest, VerifyChecksum) {
    Crc32Transformer transformer;
    auto data = generate_random_data(1024);

    auto with_crc = transformer.transform(data);
    ASSERT_TRUE(with_crc.is_success());

    auto verified = transformer.inverse(with_crc.value());
    ASSERT_TRUE(verified.is_success());
    EXPECT_EQ(verified.value(), data);
}

TEST(Crc32TransformerTest, DetectCorruption) {
    Crc32Transformer transformer;
    auto data = generate_random_data(1024);

    auto with_crc = transformer.transform(data);
    ASSERT_TRUE(with_crc.is_success());

    // Corrupt the data
    auto corrupted = with_crc.value();
    corrupted[100] ^= 0xFF;

    auto verified = transformer.inverse(corrupted);
    EXPECT_TRUE(verified.is_error());
    EXPECT_EQ(verified.code(), ErrorCode::INVALID_CHECKSUM);
}

// ============================================================================
// XXHash64 Transformer Tests
// ============================================================================

TEST(XxHash64TransformerTest, AddChecksum) {
    XxHash64Transformer transformer;
    std::vector<uint8_t> data = {'t', 'e', 's', 't'};

    auto result = transformer.transform(data);
    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), data.size() + 8);  // +8 bytes for XXH64
}

TEST(XxHash64TransformerTest, Bijectivity) {
    XxHash64Transformer transformer;
    auto data = generate_random_data(2048);

    auto with_hash = transformer.transform(data);
    ASSERT_TRUE(with_hash.is_success());

    auto verified = transformer.inverse(with_hash.value());
    ASSERT_TRUE(verified.is_success());
    EXPECT_EQ(verified.value(), data);
}

TEST(XxHash64TransformerTest, DetectCorruption) {
    XxHash64Transformer transformer;
    auto data = generate_random_data(1024);

    auto with_hash = transformer.transform(data);
    ASSERT_TRUE(with_hash.is_success());

    // Corrupt the data
    auto corrupted = with_hash.value();
    corrupted[500] ^= 0xFF;

    auto verified = transformer.inverse(corrupted);
    EXPECT_TRUE(verified.is_error());
    EXPECT_EQ(verified.code(), ErrorCode::INVALID_CHECKSUM);
}

// ============================================================================
// Pipeline Tests
// ============================================================================

TEST(TransformPipelineTest, EmptyPipeline) {
    auto pipeline = TransformPipeline::builder().build();

    EXPECT_TRUE(pipeline.empty());
    EXPECT_EQ(pipeline.stage_count(), 0);

    auto data = generate_random_data(1024);
    auto result = pipeline.transform(data);
    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), data);
}

TEST(TransformPipelineTest, SingleStage) {
    auto pipeline = TransformPipeline::builder()
        .add<Base64Transformer>()
        .build();

    EXPECT_EQ(pipeline.stage_count(), 1);

    auto data = generate_random_data(256);

    auto transformed = pipeline.transform(data);
    ASSERT_TRUE(transformed.is_success());

    auto recovered = pipeline.inverse(transformed.value());
    ASSERT_TRUE(recovered.is_success());
    EXPECT_EQ(recovered.value(), data);
}

TEST(TransformPipelineTest, MultipleStages) {
    // Pipeline: data -> checksum -> base64
    auto pipeline = TransformPipeline::builder()
        .add<Crc32Transformer>()
        .add<Base64Transformer>()
        .build();

    EXPECT_EQ(pipeline.stage_count(), 2);

    auto data = generate_random_data(512);

    auto transformed = pipeline.transform(data);
    ASSERT_TRUE(transformed.is_success());

    // Result should be base64 encoded
    for (uint8_t c : transformed.value()) {
        EXPECT_TRUE((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=');
    }

    auto recovered = pipeline.inverse(transformed.value());
    ASSERT_TRUE(recovered.is_success());
    EXPECT_EQ(recovered.value(), data);
}

TEST(TransformPipelineTest, Bijectivity) {
    // Complex pipeline: checksum -> hex
    auto pipeline = TransformPipeline::builder()
        .add<XxHash64Transformer>()
        .add<HexTransformer>()
        .build();

    auto data = generate_random_data(1000);

    auto transformed = pipeline.transform(data);
    ASSERT_TRUE(transformed.is_success());

    auto recovered = pipeline.inverse(transformed.value());
    ASSERT_TRUE(recovered.is_success());
    EXPECT_EQ(recovered.value(), data);
}

TEST(TransformPipelineTest, Description) {
    auto pipeline = TransformPipeline::builder()
        .add<Crc32Transformer>()
        .add<Base64Transformer>()
        .build();

    std::string desc = pipeline.description();
    EXPECT_NE(desc.find("crc32"), std::string::npos);
    EXPECT_NE(desc.find("base64"), std::string::npos);
}

TEST(TransformPipelineTest, Clone) {
    auto pipeline = TransformPipeline::builder()
        .add<Crc32Transformer>()
        .add<Base64Transformer>()
        .build();

    auto cloned = pipeline.clone();
    ASSERT_NE(cloned, nullptr);

    auto data = generate_random_data(256);

    auto result1 = pipeline.transform(data);
    auto result2 = static_cast<TransformPipeline*>(cloned.get())->transform(data);

    ASSERT_TRUE(result1.is_success());
    ASSERT_TRUE(result2.is_success());
    EXPECT_EQ(result1.value(), result2.value());
}

TEST(TransformPipelineTest, WithStats) {
    auto pipeline = TransformPipeline::builder()
        .add<Base64Transformer>()
        .build();

    auto data = generate_random_data(1000);

    auto result = pipeline.transform_with_stats(data);
    ASSERT_TRUE(result.is_success());

    const auto& stats = result.value().stats;
    EXPECT_EQ(stats.input_size, 1000);
    EXPECT_GT(stats.output_size, 0);
    EXPECT_GT(stats.duration.count(), 0);
}

// ============================================================================
// Registry Tests
// ============================================================================

TEST(TransformRegistryTest, CreateByName) {
    auto transformer = TransformRegistry::create("base64");
    ASSERT_NE(transformer, nullptr);
    EXPECT_EQ(transformer->id(), TransformerId::BASE64);
}

TEST(TransformRegistryTest, CreateById) {
    auto transformer = TransformRegistry::create(TransformerId::HEX);
    ASSERT_NE(transformer, nullptr);
    EXPECT_EQ(transformer->id(), TransformerId::HEX);
}

TEST(TransformRegistryTest, UnknownReturnsNull) {
    auto transformer = TransformRegistry::create("nonexistent");
    EXPECT_EQ(transformer, nullptr);
}

TEST(TransformRegistryTest, AvailableTransformers) {
    auto& registry = TransformRegistry::instance();
    auto available = registry.available_transformers();
    EXPECT_GT(available.size(), 0);
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST(TransformUtilsTest, EncodeDecodeBase64) {
    auto data = generate_random_data(100);

    auto encoded = encode_base64(data);
    EXPECT_GT(encoded.size(), 0);

    auto decoded = decode_base64(encoded);
    ASSERT_TRUE(decoded.is_success());
    EXPECT_EQ(decoded.value(), data);
}

TEST(TransformUtilsTest, EncodeDecodeHex) {
    auto data = generate_random_data(50);

    auto encoded = encode_hex(data);
    EXPECT_EQ(encoded.size(), data.size() * 2);

    auto decoded = decode_hex(encoded);
    ASSERT_TRUE(decoded.is_success());
    EXPECT_EQ(decoded.value(), data);
}

TEST(TransformUtilsTest, Crc32Function) {
    std::vector<uint8_t> data = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint32_t crc = crc32(data);

    // Known CRC32 for "123456789" is 0xCBF43926
    EXPECT_EQ(crc, 0xCBF43926);
}

TEST(TransformUtilsTest, XxHash64Function) {
    std::vector<uint8_t> data = {'t', 'e', 's', 't'};
    uint64_t hash1 = xxhash64(data, 0);
    uint64_t hash2 = xxhash64(data, 0);
    uint64_t hash3 = xxhash64(data, 1);  // Different seed

    EXPECT_EQ(hash1, hash2);  // Same data, same seed -> same hash
    EXPECT_NE(hash1, hash3);  // Different seed -> different hash
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST(TransformStressTest, LargeDataPipeline) {
    auto pipeline = TransformPipeline::builder()
        .add<XxHash64Transformer>()
        .add<Base64Transformer>()
        .build();

    // 1 MB of data
    auto data = generate_random_data(1024 * 1024);

    auto transformed = pipeline.transform(data);
    ASSERT_TRUE(transformed.is_success());

    auto recovered = pipeline.inverse(transformed.value());
    ASSERT_TRUE(recovered.is_success());
    EXPECT_EQ(recovered.value(), data);
}

TEST(TransformStressTest, ManySmallTransforms) {
    Base64Transformer transformer;

    for (int i = 0; i < 10000; ++i) {
        auto data = generate_random_data(100, i);

        auto encoded = transformer.transform(data);
        ASSERT_TRUE(encoded.is_success());

        auto decoded = transformer.inverse(encoded.value());
        ASSERT_TRUE(decoded.is_success());
        EXPECT_EQ(decoded.value(), data);
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
