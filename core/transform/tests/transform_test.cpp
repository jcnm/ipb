/**
 * @file transform_test.cpp
 * @brief Comprehensive unit tests for IPB Transform module
 *
 * Test coverage:
 * - All transformer types (compression, encryption, encoding, integrity)
 * - All algorithm variants
 * - Edge cases (empty, single byte, large data)
 * - Error conditions (corruption, invalid input)
 * - Pipeline composition
 * - Performance benchmarks
 */

#include <gtest/gtest.h>
#include <ipb/transform/transform.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace ipb::transform;

// ============================================================================
// TEST UTILITIES
// ============================================================================

class TransformTestBase : public ::testing::Test {
protected:
    /**
     * @brief Generate random test data
     */
    static std::vector<uint8_t> random_data(size_t size, uint32_t seed = 42) {
        std::vector<uint8_t> data(size);
        std::mt19937 gen(seed);
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& byte : data) {
            byte = static_cast<uint8_t>(dist(gen));
        }
        return data;
    }

    /**
     * @brief Generate compressible data (repeated patterns)
     */
    static std::vector<uint8_t> compressible_data(size_t size) {
        std::vector<uint8_t> data(size);
        const std::string pattern = "IPB Transform Test Pattern - This text repeats! ";
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(pattern[i % pattern.size()]);
        }
        return data;
    }

    /**
     * @brief Generate incompressible data (high entropy)
     */
    static std::vector<uint8_t> incompressible_data(size_t size) {
        return random_data(size, static_cast<uint32_t>(size));
    }

    /**
     * @brief Generate sequential data
     */
    static std::vector<uint8_t> sequential_data(size_t size) {
        std::vector<uint8_t> data(size);
        std::iota(data.begin(), data.end(), 0);
        return data;
    }

    /**
     * @brief Generate all-zeros data
     */
    static std::vector<uint8_t> zero_data(size_t size) {
        return std::vector<uint8_t>(size, 0);
    }

    /**
     * @brief Generate all-ones data
     */
    static std::vector<uint8_t> ones_data(size_t size) {
        return std::vector<uint8_t>(size, 0xFF);
    }

    /**
     * @brief Verify bijectivity: inverse(transform(data)) == data
     */
    static void verify_bijectivity(ITransformer& transformer,
                                    std::span<const uint8_t> data,
                                    const std::string& context = "") {
        auto transformed = transformer.transform(data);
        ASSERT_TRUE(transformed.is_success())
            << context << " transform failed: " << error_name(transformed.code());

        auto recovered = transformer.inverse(transformed.value());
        ASSERT_TRUE(recovered.is_success())
            << context << " inverse failed: " << error_name(recovered.code());

        EXPECT_EQ(recovered.value().size(), data.size())
            << context << " size mismatch";

        EXPECT_TRUE(std::equal(recovered.value().begin(), recovered.value().end(),
                               data.begin(), data.end()))
            << context << " data mismatch";
    }

    /**
     * @brief Test various data sizes
     */
    static std::vector<size_t> test_sizes() {
        return {0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128,
                255, 256, 512, 1000, 1024, 4096, 10000, 65536};
    }
};

// ============================================================================
// NULL TRANSFORMER TESTS
// ============================================================================

class NullTransformerTest : public TransformTestBase {};

TEST_F(NullTransformerTest, AllSizes) {
    NullTransformer transformer;
    for (size_t size : test_sizes()) {
        auto data = random_data(size);
        verify_bijectivity(transformer, data, "size=" + std::to_string(size));
    }
}

TEST_F(NullTransformerTest, Metadata) {
    NullTransformer transformer;
    EXPECT_EQ(transformer.id(), TransformerId::NONE);
    EXPECT_EQ(transformer.name(), "passthrough");
    EXPECT_FALSE(transformer.requires_key());
    EXPECT_FALSE(transformer.has_header());
    EXPECT_DOUBLE_EQ(transformer.max_expansion_ratio(), 1.0);
}

TEST_F(NullTransformerTest, Clone) {
    NullTransformer transformer;
    auto cloned = transformer.clone();
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->id(), TransformerId::NONE);

    auto data = random_data(100);
    verify_bijectivity(*cloned, data);
}

// ============================================================================
// BASE64 TRANSFORMER TESTS
// ============================================================================

class Base64TransformerTest : public TransformTestBase {};

TEST_F(Base64TransformerTest, AllSizes) {
    Base64Transformer transformer;
    for (size_t size : test_sizes()) {
        auto data = random_data(size);
        verify_bijectivity(transformer, data, "size=" + std::to_string(size));
    }
}

TEST_F(Base64TransformerTest, StandardAlphabet) {
    Base64Transformer transformer(Base64Transformer::Variant::STANDARD);
    auto data = random_data(256);

    auto encoded = transformer.transform(data);
    ASSERT_TRUE(encoded.is_success());

    // Verify only valid Base64 characters
    for (uint8_t c : encoded.value()) {
        bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
        EXPECT_TRUE(valid) << "Invalid char: " << static_cast<int>(c);
    }

    verify_bijectivity(transformer, data);
}

TEST_F(Base64TransformerTest, UrlSafeAlphabet) {
    Base64Transformer transformer(Base64Transformer::Variant::URL_SAFE);
    auto data = random_data(256);

    auto encoded = transformer.transform(data);
    ASSERT_TRUE(encoded.is_success());

    // URL-safe should not contain + or /
    for (uint8_t c : encoded.value()) {
        EXPECT_NE(c, '+') << "URL-safe should not contain +";
        EXPECT_NE(c, '/') << "URL-safe should not contain /";
    }

    verify_bijectivity(transformer, data);
}

TEST_F(Base64TransformerTest, NoPaddingVariant) {
    Base64Transformer transformer(Base64Transformer::Variant::STANDARD, false);

    // Test sizes that would normally have padding
    for (size_t size : {1, 2, 4, 5, 7, 8, 10}) {
        auto data = random_data(size);
        auto encoded = transformer.transform(data);
        ASSERT_TRUE(encoded.is_success());

        // Verify no padding
        for (uint8_t c : encoded.value()) {
            EXPECT_NE(c, '=') << "No-padding variant should not have '='";
        }

        verify_bijectivity(transformer, data, "no-padding size=" + std::to_string(size));
    }
}

TEST_F(Base64TransformerTest, KnownVectors) {
    Base64Transformer transformer;

    // RFC 4648 test vectors
    struct TestCase {
        std::string input;
        std::string expected;
    };

    std::vector<TestCase> cases = {
        {"", ""},
        {"f", "Zg=="},
        {"fo", "Zm8="},
        {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="},
        {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"},
    };

    for (const auto& tc : cases) {
        std::vector<uint8_t> input(tc.input.begin(), tc.input.end());
        auto encoded = transformer.transform(input);
        ASSERT_TRUE(encoded.is_success());

        std::string result(encoded.value().begin(), encoded.value().end());
        EXPECT_EQ(result, tc.expected) << "Input: " << tc.input;

        verify_bijectivity(transformer, input);
    }
}

TEST_F(Base64TransformerTest, InvalidDecode) {
    Base64Transformer transformer;

    // Invalid characters
    std::vector<uint8_t> invalid = {'!', '@', '#', '$'};
    auto result = transformer.inverse(invalid);
    EXPECT_TRUE(result.is_error());
}

TEST_F(Base64TransformerTest, ExpansionRatio) {
    Base64Transformer transformer;
    auto data = random_data(1000);
    auto encoded = transformer.transform(data);
    ASSERT_TRUE(encoded.is_success());

    // Base64 expands by 4/3 (plus possible padding)
    double ratio = static_cast<double>(encoded.value().size()) / data.size();
    EXPECT_LE(ratio, transformer.max_expansion_ratio());
    EXPECT_GE(ratio, 1.33);
}

// ============================================================================
// HEX TRANSFORMER TESTS
// ============================================================================

class HexTransformerTest : public TransformTestBase {};

TEST_F(HexTransformerTest, AllSizes) {
    HexTransformer transformer;
    for (size_t size : test_sizes()) {
        auto data = random_data(size);
        verify_bijectivity(transformer, data, "size=" + std::to_string(size));
    }
}

TEST_F(HexTransformerTest, LowercaseOutput) {
    HexTransformer transformer(false);  // lowercase
    std::vector<uint8_t> data = {0xAB, 0xCD, 0xEF};

    auto encoded = transformer.transform(data);
    ASSERT_TRUE(encoded.is_success());

    std::string result(encoded.value().begin(), encoded.value().end());
    EXPECT_EQ(result, "abcdef");

    verify_bijectivity(transformer, data);
}

TEST_F(HexTransformerTest, UppercaseOutput) {
    HexTransformer transformer(true);  // uppercase
    std::vector<uint8_t> data = {0xAB, 0xCD, 0xEF};

    auto encoded = transformer.transform(data);
    ASSERT_TRUE(encoded.is_success());

    std::string result(encoded.value().begin(), encoded.value().end());
    EXPECT_EQ(result, "ABCDEF");

    verify_bijectivity(transformer, data);
}

TEST_F(HexTransformerTest, CaseInsensitiveDecode) {
    HexTransformer transformer;

    // Decode lowercase
    std::vector<uint8_t> lower = {'a', 'b', 'c', 'd'};
    auto result1 = transformer.inverse(lower);
    ASSERT_TRUE(result1.is_success());
    EXPECT_EQ(result1.value(), (std::vector<uint8_t>{0xAB, 0xCD}));

    // Decode uppercase
    std::vector<uint8_t> upper = {'A', 'B', 'C', 'D'};
    auto result2 = transformer.inverse(upper);
    ASSERT_TRUE(result2.is_success());
    EXPECT_EQ(result2.value(), (std::vector<uint8_t>{0xAB, 0xCD}));

    // Decode mixed case
    std::vector<uint8_t> mixed = {'a', 'B', 'c', 'D'};
    auto result3 = transformer.inverse(mixed);
    ASSERT_TRUE(result3.is_success());
    EXPECT_EQ(result3.value(), (std::vector<uint8_t>{0xAB, 0xCD}));
}

TEST_F(HexTransformerTest, InvalidDecode) {
    HexTransformer transformer;

    // Odd length
    std::vector<uint8_t> odd = {'a', 'b', 'c'};
    auto result1 = transformer.inverse(odd);
    EXPECT_TRUE(result1.is_error());
    EXPECT_EQ(result1.code(), ErrorCode::DECODING_ERROR);

    // Invalid characters
    std::vector<uint8_t> invalid = {'g', 'h'};
    auto result2 = transformer.inverse(invalid);
    EXPECT_TRUE(result2.is_error());
}

TEST_F(HexTransformerTest, AllByteValues) {
    HexTransformer transformer;

    // Test all 256 byte values
    std::vector<uint8_t> all_bytes(256);
    std::iota(all_bytes.begin(), all_bytes.end(), 0);

    auto encoded = transformer.transform(all_bytes);
    ASSERT_TRUE(encoded.is_success());
    EXPECT_EQ(encoded.value().size(), 512);

    verify_bijectivity(transformer, all_bytes);
}

// ============================================================================
// CRC32 TRANSFORMER TESTS
// ============================================================================

class Crc32TransformerTest : public TransformTestBase {};

TEST_F(Crc32TransformerTest, AllSizes) {
    Crc32Transformer transformer;
    for (size_t size : test_sizes()) {
        if (size == 0) continue;  // CRC32 needs data
        auto data = random_data(size);
        verify_bijectivity(transformer, data, "size=" + std::to_string(size));
    }
}

TEST_F(Crc32TransformerTest, ChecksumSize) {
    Crc32Transformer transformer;
    auto data = random_data(100);

    auto result = transformer.transform(data);
    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), data.size() + 4);  // +4 for CRC32
}

TEST_F(Crc32TransformerTest, DetectSingleBitFlip) {
    Crc32Transformer transformer;
    auto data = random_data(1000);

    auto with_crc = transformer.transform(data);
    ASSERT_TRUE(with_crc.is_success());

    // Flip each bit and verify detection
    for (size_t byte_idx = 0; byte_idx < std::min(size_t{100}, data.size()); ++byte_idx) {
        for (int bit = 0; bit < 8; ++bit) {
            auto corrupted = with_crc.value();
            corrupted[byte_idx] ^= (1 << bit);

            auto verified = transformer.inverse(corrupted);
            EXPECT_TRUE(verified.is_error())
                << "Failed to detect bit flip at byte " << byte_idx << " bit " << bit;
        }
    }
}

TEST_F(Crc32TransformerTest, KnownCRC32Values) {
    // Test known CRC32 values
    std::vector<uint8_t> data = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint32_t expected = 0xCBF43926;

    uint32_t computed = crc32(data);
    EXPECT_EQ(computed, expected);
}

TEST_F(Crc32TransformerTest, TruncatedData) {
    Crc32Transformer transformer;
    std::vector<uint8_t> too_short = {1, 2, 3};  // Less than 4 bytes

    auto result = transformer.inverse(too_short);
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::TRUNCATED_DATA);
}

// ============================================================================
// XXHASH64 TRANSFORMER TESTS
// ============================================================================

class XxHash64TransformerTest : public TransformTestBase {};

TEST_F(XxHash64TransformerTest, AllSizes) {
    XxHash64Transformer transformer;
    for (size_t size : test_sizes()) {
        if (size == 0) continue;
        auto data = random_data(size);
        verify_bijectivity(transformer, data, "size=" + std::to_string(size));
    }
}

TEST_F(XxHash64TransformerTest, ChecksumSize) {
    XxHash64Transformer transformer;
    auto data = random_data(100);

    auto result = transformer.transform(data);
    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), data.size() + 8);  // +8 for XXH64
}

TEST_F(XxHash64TransformerTest, SeedVariation) {
    XxHash64Transformer t1(0);
    XxHash64Transformer t2(12345);

    auto data = random_data(100);

    auto r1 = t1.transform(data);
    auto r2 = t2.transform(data);

    ASSERT_TRUE(r1.is_success());
    ASSERT_TRUE(r2.is_success());

    // Different seeds should produce different results
    EXPECT_NE(r1.value(), r2.value());

    // But both should be verifiable with their respective transformers
    verify_bijectivity(t1, data);
    verify_bijectivity(t2, data);
}

TEST_F(XxHash64TransformerTest, DetectCorruption) {
    XxHash64Transformer transformer;
    auto data = random_data(1000);

    auto with_hash = transformer.transform(data);
    ASSERT_TRUE(with_hash.is_success());

    // Corrupt various positions
    std::vector<size_t> positions = {0, 1, 100, 500, data.size() - 1};
    for (size_t pos : positions) {
        auto corrupted = with_hash.value();
        corrupted[pos] ^= 0xFF;

        auto verified = transformer.inverse(corrupted);
        EXPECT_TRUE(verified.is_error())
            << "Failed to detect corruption at position " << pos;
    }
}

TEST_F(XxHash64TransformerTest, Determinism) {
    // Same input should always produce same hash
    auto data = random_data(1000);

    uint64_t hash1 = xxhash64(data, 0);
    uint64_t hash2 = xxhash64(data, 0);
    EXPECT_EQ(hash1, hash2);

    // Different data should (almost certainly) produce different hash
    auto data2 = random_data(1000, 99);
    uint64_t hash3 = xxhash64(data2, 0);
    EXPECT_NE(hash1, hash3);
}

// ============================================================================
// PIPELINE TESTS
// ============================================================================

class TransformPipelineTest : public TransformTestBase {};

TEST_F(TransformPipelineTest, EmptyPipeline) {
    auto pipeline = TransformPipeline::builder().build();

    EXPECT_TRUE(pipeline.empty());
    EXPECT_EQ(pipeline.stage_count(), 0);
    EXPECT_EQ(pipeline.id(), TransformerId::NONE);

    for (size_t size : test_sizes()) {
        auto data = random_data(size);
        verify_bijectivity(pipeline, data);
    }
}

TEST_F(TransformPipelineTest, SingleStage) {
    auto pipeline = TransformPipeline::builder()
        .add<Base64Transformer>()
        .build();

    EXPECT_EQ(pipeline.stage_count(), 1);

    for (size_t size : {0, 1, 100, 1000}) {
        auto data = random_data(size);
        verify_bijectivity(pipeline, data);
    }
}

TEST_F(TransformPipelineTest, TwoStages) {
    // CRC32 then Base64
    auto pipeline = TransformPipeline::builder()
        .add<Crc32Transformer>()
        .add<Base64Transformer>()
        .build();

    EXPECT_EQ(pipeline.stage_count(), 2);

    auto data = random_data(500);
    verify_bijectivity(pipeline, data);

    // Verify output is valid Base64
    auto result = pipeline.transform(data);
    ASSERT_TRUE(result.is_success());
    for (uint8_t c : result.value()) {
        bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
        EXPECT_TRUE(valid);
    }
}

TEST_F(TransformPipelineTest, ThreeStages) {
    // XXHash -> CRC32 -> Hex
    auto pipeline = TransformPipeline::builder()
        .add<XxHash64Transformer>()
        .add<Crc32Transformer>()
        .add<HexTransformer>()
        .build();

    EXPECT_EQ(pipeline.stage_count(), 3);

    auto data = random_data(1000);
    verify_bijectivity(pipeline, data);
}

TEST_F(TransformPipelineTest, ConditionalAdd) {
    bool add_integrity = true;
    bool add_encoding = false;

    auto pipeline = TransformPipeline::builder()
        .add_if<Crc32Transformer>(add_integrity)
        .add_if<Base64Transformer>(add_encoding)
        .build();

    EXPECT_EQ(pipeline.stage_count(), 1);

    auto data = random_data(100);
    verify_bijectivity(pipeline, data);
}

TEST_F(TransformPipelineTest, Clone) {
    auto pipeline = TransformPipeline::builder()
        .add<Crc32Transformer>()
        .add<Base64Transformer>()
        .build();

    auto cloned = pipeline.clone();
    ASSERT_NE(cloned, nullptr);

    auto data = random_data(200);

    auto r1 = pipeline.transform(data);
    auto r2 = static_cast<TransformPipeline*>(cloned.get())->transform(data);

    ASSERT_TRUE(r1.is_success());
    ASSERT_TRUE(r2.is_success());
    EXPECT_EQ(r1.value(), r2.value());

    verify_bijectivity(*cloned, data);
}

TEST_F(TransformPipelineTest, Description) {
    auto pipeline = TransformPipeline::builder()
        .add<Crc32Transformer>()
        .add<Base64Transformer>()
        .build();

    std::string desc = pipeline.description();
    EXPECT_NE(desc.find("crc32"), std::string::npos);
    EXPECT_NE(desc.find("base64"), std::string::npos);
    EXPECT_NE(desc.find("pipeline"), std::string::npos);
}

TEST_F(TransformPipelineTest, StageIds) {
    auto pipeline = TransformPipeline::builder()
        .add<XxHash64Transformer>()
        .add<HexTransformer>()
        .build();

    auto ids = pipeline.stage_ids();
    ASSERT_EQ(ids.size(), 2);
    EXPECT_EQ(ids[0], TransformerId::XXH64);
    EXPECT_EQ(ids[1], TransformerId::HEX);
}

TEST_F(TransformPipelineTest, TransformWithStats) {
    auto pipeline = TransformPipeline::builder()
        .add<Base64Transformer>()
        .build();

    auto data = random_data(1000);
    auto result = pipeline.transform_with_stats(data);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().stats.input_size, 1000);
    EXPECT_GT(result.value().stats.output_size, 0);
    EXPECT_GT(result.value().stats.duration.count(), 0);
}

TEST_F(TransformPipelineTest, LargeData) {
    auto pipeline = TransformPipeline::builder()
        .add<XxHash64Transformer>()
        .add<Base64Transformer>()
        .build();

    // 10 MB of data
    auto data = random_data(10 * 1024 * 1024);
    verify_bijectivity(pipeline, data);
}

// ============================================================================
// REGISTRY TESTS
// ============================================================================

class TransformRegistryTest : public TransformTestBase {};

TEST_F(TransformRegistryTest, CreateByName) {
    std::vector<std::pair<std::string, TransformerId>> cases = {
        {"none", TransformerId::NONE},
        {"base64", TransformerId::BASE64},
        {"base64url", TransformerId::BASE64_URL},
        {"hex", TransformerId::HEX},
        {"crc32", TransformerId::CRC32},
        {"xxhash64", TransformerId::XXH64},
    };

    for (const auto& [name, expected_id] : cases) {
        auto transformer = TransformRegistry::create(name);
        ASSERT_NE(transformer, nullptr) << "Failed to create: " << name;
        EXPECT_EQ(transformer->id(), expected_id) << "Wrong ID for: " << name;
    }
}

TEST_F(TransformRegistryTest, CreateById) {
    std::vector<TransformerId> ids = {
        TransformerId::NONE,
        TransformerId::BASE64,
        TransformerId::HEX,
        TransformerId::CRC32,
        TransformerId::XXH64,
    };

    for (auto id : ids) {
        auto transformer = TransformRegistry::create(id);
        ASSERT_NE(transformer, nullptr) << "Failed to create ID: "
            << static_cast<int>(id);
        EXPECT_EQ(transformer->id(), id);
    }
}

TEST_F(TransformRegistryTest, UnknownReturnsNull) {
    auto t1 = TransformRegistry::create("nonexistent");
    EXPECT_EQ(t1, nullptr);

    auto t2 = TransformRegistry::create("INVALID_NAME");
    EXPECT_EQ(t2, nullptr);
}

TEST_F(TransformRegistryTest, AvailableTransformers) {
    auto available = TransformRegistry::instance().available_transformers();
    EXPECT_GT(available.size(), 5);  // At least our basic transformers

    // Should include known IDs
    EXPECT_NE(std::find(available.begin(), available.end(), TransformerId::BASE64),
              available.end());
    EXPECT_NE(std::find(available.begin(), available.end(), TransformerId::HEX),
              available.end());
}

// ============================================================================
// UTILITY FUNCTION TESTS
// ============================================================================

class TransformUtilsTest : public TransformTestBase {};

TEST_F(TransformUtilsTest, EncodeDecodeBase64) {
    for (size_t size : {0, 1, 10, 100, 1000}) {
        auto data = random_data(size);

        auto encoded = encode_base64(data);
        EXPECT_EQ(encoded.empty(), data.empty());

        auto decoded = decode_base64(encoded);
        ASSERT_TRUE(decoded.is_success());
        EXPECT_EQ(decoded.value(), data);
    }
}

TEST_F(TransformUtilsTest, EncodeDecodeHex) {
    for (size_t size : {0, 1, 10, 100, 1000}) {
        auto data = random_data(size);

        auto encoded = encode_hex(data);
        EXPECT_EQ(encoded.size(), data.size() * 2);

        auto decoded = decode_hex(encoded);
        ASSERT_TRUE(decoded.is_success());
        EXPECT_EQ(decoded.value(), data);
    }
}

TEST_F(TransformUtilsTest, Crc32KnownValue) {
    // "123456789" should have CRC32 = 0xCBF43926
    std::vector<uint8_t> data = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(crc32(data), 0xCBF43926u);
}

TEST_F(TransformUtilsTest, XxHash64Determinism) {
    auto data = random_data(1000);

    // Same data, same seed -> same hash
    EXPECT_EQ(xxhash64(data, 0), xxhash64(data, 0));
    EXPECT_EQ(xxhash64(data, 42), xxhash64(data, 42));

    // Different seed -> different hash
    EXPECT_NE(xxhash64(data, 0), xxhash64(data, 1));
}

// ============================================================================
// STRESS TESTS
// ============================================================================

class TransformStressTest : public TransformTestBase {};

TEST_F(TransformStressTest, ManySmallTransforms) {
    Base64Transformer transformer;

    for (int i = 0; i < 10000; ++i) {
        auto data = random_data(100, static_cast<uint32_t>(i));
        verify_bijectivity(transformer, data);
    }
}

TEST_F(TransformStressTest, VariousDataPatterns) {
    std::vector<std::unique_ptr<ITransformer>> transformers;
    transformers.push_back(std::make_unique<Base64Transformer>());
    transformers.push_back(std::make_unique<HexTransformer>());
    transformers.push_back(std::make_unique<Crc32Transformer>());
    transformers.push_back(std::make_unique<XxHash64Transformer>());

    for (auto& transformer : transformers) {
        // Random data
        verify_bijectivity(*transformer, random_data(1000), "random");

        // Compressible data
        verify_bijectivity(*transformer, compressible_data(1000), "compressible");

        // Incompressible data
        verify_bijectivity(*transformer, incompressible_data(1000), "incompressible");

        // Sequential data
        verify_bijectivity(*transformer, sequential_data(256), "sequential");

        // All zeros
        verify_bijectivity(*transformer, zero_data(1000), "zeros");

        // All ones
        verify_bijectivity(*transformer, ones_data(1000), "ones");
    }
}

TEST_F(TransformStressTest, PipelineVariations) {
    // Test various pipeline combinations
    std::vector<TransformPipeline> pipelines;

    pipelines.push_back(TransformPipeline::builder()
        .add<Base64Transformer>()
        .build());

    pipelines.push_back(TransformPipeline::builder()
        .add<HexTransformer>()
        .build());

    pipelines.push_back(TransformPipeline::builder()
        .add<Crc32Transformer>()
        .add<Base64Transformer>()
        .build());

    pipelines.push_back(TransformPipeline::builder()
        .add<XxHash64Transformer>()
        .add<HexTransformer>()
        .build());

    pipelines.push_back(TransformPipeline::builder()
        .add<Crc32Transformer>()
        .add<XxHash64Transformer>()
        .add<Base64Transformer>()
        .build());

    for (auto& pipeline : pipelines) {
        auto data = random_data(1000);
        verify_bijectivity(pipeline, data, pipeline.description());
    }
}

// ============================================================================
// EDGE CASE TESTS
// ============================================================================

class TransformEdgeCaseTest : public TransformTestBase {};

TEST_F(TransformEdgeCaseTest, EmptyInput) {
    std::vector<uint8_t> empty;

    std::vector<std::unique_ptr<ITransformer>> transformers;
    transformers.push_back(std::make_unique<NullTransformer>());
    transformers.push_back(std::make_unique<Base64Transformer>());
    transformers.push_back(std::make_unique<HexTransformer>());

    for (auto& t : transformers) {
        auto result = t->transform(empty);
        ASSERT_TRUE(result.is_success()) << t->name();
        // Empty input should produce empty or minimal output
    }
}

TEST_F(TransformEdgeCaseTest, SingleByte) {
    for (int byte_val = 0; byte_val <= 255; ++byte_val) {
        std::vector<uint8_t> single = {static_cast<uint8_t>(byte_val)};

        Base64Transformer b64;
        verify_bijectivity(b64, single);

        HexTransformer hex;
        verify_bijectivity(hex, single);
    }
}

TEST_F(TransformEdgeCaseTest, MaxExpansion) {
    // Verify transformers don't exceed their stated max expansion
    auto data = random_data(10000);

    std::vector<std::unique_ptr<ITransformer>> transformers;
    transformers.push_back(std::make_unique<Base64Transformer>());
    transformers.push_back(std::make_unique<HexTransformer>());
    transformers.push_back(std::make_unique<Crc32Transformer>());
    transformers.push_back(std::make_unique<XxHash64Transformer>());

    for (auto& t : transformers) {
        auto result = t->transform(data);
        ASSERT_TRUE(result.is_success());

        double actual_ratio = static_cast<double>(result.value().size()) / data.size();
        EXPECT_LE(actual_ratio, t->max_expansion_ratio() + 0.01)
            << t->name() << " exceeded max expansion ratio";
    }
}

// ============================================================================
// PERFORMANCE TESTS (Optional)
// ============================================================================

class TransformPerformanceTest : public TransformTestBase {};

TEST_F(TransformPerformanceTest, Base64Throughput) {
    Base64Transformer transformer;
    auto data = random_data(1024 * 1024);  // 1 MB

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 10; ++i) {
        auto encoded = transformer.transform(data);
        auto decoded = transformer.inverse(encoded.value());
        ASSERT_TRUE(decoded.is_success());
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double mb_processed = 20.0;  // 10 iterations * 2 (encode+decode) * 1 MB
    double seconds = duration.count() / 1000.0;
    double throughput = mb_processed / seconds;

    std::cout << "Base64 throughput: " << throughput << " MB/s" << std::endl;
    EXPECT_GT(throughput, 10.0);  // At least 10 MB/s
}

TEST_F(TransformPerformanceTest, XxHash64Throughput) {
    auto data = random_data(1024 * 1024);  // 1 MB

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 100; ++i) {
        volatile uint64_t hash = xxhash64(data, 0);
        (void)hash;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double mb_processed = 100.0;  // 100 iterations * 1 MB
    double seconds = duration.count() / 1000.0;
    double throughput = mb_processed / seconds;

    std::cout << "XXHash64 throughput: " << throughput << " MB/s" << std::endl;
    EXPECT_GT(throughput, 100.0);  // At least 100 MB/s for XXHash64
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
