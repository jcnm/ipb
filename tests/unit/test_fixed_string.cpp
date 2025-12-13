/**
 * @file test_fixed_string.cpp
 * @brief Unit tests for IPB fixed-size string types
 *
 * Tests coverage for:
 * - Construction and assignment
 * - String operations (append, find, compare)
 * - Conversions (to/from string_view, std::string)
 * - Type aliases (TopicString, IdentifierString, etc.)
 * - Edge cases and overflow handling
 */

#include <ipb/common/fixed_string.hpp>

#include <string>
#include <string_view>
#include <unordered_set>

#include <gtest/gtest.h>

using namespace ipb::common;

// ============================================================================
// Construction Tests
// ============================================================================

class FixedStringConstructionTest : public ::testing::Test {};

TEST_F(FixedStringConstructionTest, DefaultConstructorCreatesEmptyString) {
    FixedString<32> str;
    EXPECT_TRUE(str.empty());
    EXPECT_EQ(str.size(), 0u);
    EXPECT_STREQ(str.c_str(), "");
}

TEST_F(FixedStringConstructionTest, ConstructFromCString) {
    FixedString<32> str("hello");
    EXPECT_EQ(str.size(), 5u);
    EXPECT_STREQ(str.c_str(), "hello");
}

TEST_F(FixedStringConstructionTest, ConstructFromStringView) {
    std::string_view sv = "world";
    FixedString<32> str(sv);
    EXPECT_EQ(str.size(), 5u);
    EXPECT_EQ(str.view(), sv);
}

TEST_F(FixedStringConstructionTest, ConstructFromStdString) {
    std::string s = "test";
    FixedString<32> str(s);
    EXPECT_EQ(str.size(), 4u);
    EXPECT_EQ(str.to_string(), s);
}

TEST_F(FixedStringConstructionTest, ConstructFromNullptr) {
    FixedString<32> str(nullptr);
    EXPECT_TRUE(str.empty());
}

TEST_F(FixedStringConstructionTest, CopyConstruction) {
    FixedString<32> str1("original");
    FixedString<32> str2(str1);
    EXPECT_EQ(str1, str2);
    EXPECT_STREQ(str2.c_str(), "original");
}

TEST_F(FixedStringConstructionTest, MoveConstruction) {
    FixedString<32> str1("moved");
    FixedString<32> str2(std::move(str1));
    EXPECT_STREQ(str2.c_str(), "moved");
}

TEST_F(FixedStringConstructionTest, TruncatesLongString) {
    FixedString<8> str("this is a very long string");
    EXPECT_EQ(str.size(), 7u);  // MAX_LENGTH = 8 - 1 = 7
    EXPECT_STREQ(str.c_str(), "this is");
}

// ============================================================================
// Assignment Tests
// ============================================================================

class FixedStringAssignmentTest : public ::testing::Test {};

TEST_F(FixedStringAssignmentTest, AssignFromCString) {
    FixedString<32> str;
    str = "assigned";
    EXPECT_STREQ(str.c_str(), "assigned");
}

TEST_F(FixedStringAssignmentTest, AssignFromStringView) {
    FixedString<32> str;
    str = std::string_view("view");
    EXPECT_EQ(str.view(), "view");
}

TEST_F(FixedStringAssignmentTest, CopyAssignment) {
    FixedString<32> str1("source");
    FixedString<32> str2;
    str2 = str1;
    EXPECT_EQ(str1, str2);
}

TEST_F(FixedStringAssignmentTest, SelfAssignment) {
    FixedString<32> str("self");
    str = str;
    EXPECT_STREQ(str.c_str(), "self");
}

TEST_F(FixedStringAssignmentTest, AssignWithLength) {
    FixedString<32> str;
    str.assign("partial", 4);
    EXPECT_STREQ(str.c_str(), "part");
}

// ============================================================================
// Accessor Tests
// ============================================================================

class FixedStringAccessorTest : public ::testing::Test {};

TEST_F(FixedStringAccessorTest, DataReturnsPointer) {
    FixedString<32> str("data");
    EXPECT_NE(str.data(), nullptr);
    EXPECT_STREQ(str.data(), "data");
}

TEST_F(FixedStringAccessorTest, ViewReturnsStringView) {
    FixedString<32> str("view test");
    std::string_view sv = str.view();
    EXPECT_EQ(sv, "view test");
    EXPECT_EQ(sv.size(), 9u);
}

TEST_F(FixedStringAccessorTest, ImplicitConversionToStringView) {
    FixedString<32> str("implicit");
    std::string_view sv = str;
    EXPECT_EQ(sv, "implicit");
}

TEST_F(FixedStringAccessorTest, ToStringCreatesStdString) {
    FixedString<32> str("convert");
    std::string s = str.to_string();
    EXPECT_EQ(s, "convert");
}

TEST_F(FixedStringAccessorTest, ElementAccess) {
    FixedString<32> str("abc");
    EXPECT_EQ(str[0], 'a');
    EXPECT_EQ(str[1], 'b');
    EXPECT_EQ(str[2], 'c');
}

TEST_F(FixedStringAccessorTest, FrontAndBack) {
    FixedString<32> str("test");
    EXPECT_EQ(str.front(), 't');
    EXPECT_EQ(str.back(), 't');
}

TEST_F(FixedStringAccessorTest, MaxSizeReturnsCapacity) {
    FixedString<64> str;
    EXPECT_EQ(str.max_size(), 63u);  // N - 1 for null terminator
    EXPECT_EQ(str.capacity(), 64u);
}

// ============================================================================
// Modifier Tests
// ============================================================================

class FixedStringModifierTest : public ::testing::Test {};

TEST_F(FixedStringModifierTest, Clear) {
    FixedString<32> str("clear me");
    str.clear();
    EXPECT_TRUE(str.empty());
    EXPECT_EQ(str.size(), 0u);
}

TEST_F(FixedStringModifierTest, PushBack) {
    FixedString<32> str;
    EXPECT_TRUE(str.push_back('a'));
    EXPECT_TRUE(str.push_back('b'));
    EXPECT_TRUE(str.push_back('c'));
    EXPECT_STREQ(str.c_str(), "abc");
}

TEST_F(FixedStringModifierTest, PushBackAtCapacity) {
    FixedString<4> str("abc");  // 3 chars, max is 3
    EXPECT_FALSE(str.push_back('d'));  // Should fail
    EXPECT_STREQ(str.c_str(), "abc");
}

TEST_F(FixedStringModifierTest, PopBack) {
    FixedString<32> str("test");
    str.pop_back();
    EXPECT_STREQ(str.c_str(), "tes");
    EXPECT_EQ(str.size(), 3u);
}

TEST_F(FixedStringModifierTest, PopBackEmpty) {
    FixedString<32> str;
    str.pop_back();  // Should be safe
    EXPECT_TRUE(str.empty());
}

TEST_F(FixedStringModifierTest, Append) {
    FixedString<32> str("hello");
    EXPECT_TRUE(str.append(" world"));
    EXPECT_STREQ(str.c_str(), "hello world");
}

TEST_F(FixedStringModifierTest, AppendTruncates) {
    FixedString<10> str("hello");
    str.append(" world!");  // Would exceed capacity
    EXPECT_EQ(str.size(), 9u);  // Max length is 9
}

TEST_F(FixedStringModifierTest, OperatorPlusEquals) {
    FixedString<32> str("a");
    str += "b";
    str += 'c';
    EXPECT_STREQ(str.c_str(), "abc");
}

// ============================================================================
// Comparison Tests
// ============================================================================

class FixedStringComparisonTest : public ::testing::Test {};

TEST_F(FixedStringComparisonTest, EqualStrings) {
    FixedString<32> str1("equal");
    FixedString<32> str2("equal");
    EXPECT_TRUE(str1 == str2);
    EXPECT_FALSE(str1 != str2);
}

TEST_F(FixedStringComparisonTest, UnequalStrings) {
    FixedString<32> str1("one");
    FixedString<32> str2("two");
    EXPECT_FALSE(str1 == str2);
    EXPECT_TRUE(str1 != str2);
}

TEST_F(FixedStringComparisonTest, CompareWithStringView) {
    FixedString<32> str("compare");
    EXPECT_TRUE(str == std::string_view("compare"));
    EXPECT_FALSE(str == std::string_view("other"));
    EXPECT_TRUE(str != std::string_view("other"));
}

TEST_F(FixedStringComparisonTest, LexicographicOrdering) {
    FixedString<32> a("apple");
    FixedString<32> b("banana");
    FixedString<32> c("cherry");

    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b < c);
    EXPECT_FALSE(c < a);
    EXPECT_TRUE(a <= a);
    EXPECT_TRUE(c >= b);
    EXPECT_TRUE(c > a);
}

// ============================================================================
// Search Tests
// ============================================================================

class FixedStringSearchTest : public ::testing::Test {};

TEST_F(FixedStringSearchTest, FindChar) {
    FixedString<32> str("hello world");
    EXPECT_EQ(str.find('o'), 4u);
    EXPECT_EQ(str.find('x'), std::string_view::npos);
}

TEST_F(FixedStringSearchTest, FindCharFromPosition) {
    FixedString<32> str("hello world");
    EXPECT_EQ(str.find('o', 5), 7u);  // Second 'o'
}

TEST_F(FixedStringSearchTest, FindSubstring) {
    FixedString<32> str("hello world");
    EXPECT_EQ(str.find("world"), 6u);
    EXPECT_EQ(str.find("xyz"), std::string_view::npos);
}

TEST_F(FixedStringSearchTest, Contains) {
    FixedString<32> str("hello world");
    EXPECT_TRUE(str.contains('o'));
    EXPECT_FALSE(str.contains('x'));
    EXPECT_TRUE(str.contains("world"));
    EXPECT_FALSE(str.contains("xyz"));
}

TEST_F(FixedStringSearchTest, StartsWith) {
    FixedString<32> str("hello world");
    EXPECT_TRUE(str.starts_with("hello"));
    EXPECT_TRUE(str.starts_with("h"));
    EXPECT_FALSE(str.starts_with("world"));
}

TEST_F(FixedStringSearchTest, EndsWith) {
    FixedString<32> str("hello world");
    EXPECT_TRUE(str.ends_with("world"));
    EXPECT_TRUE(str.ends_with("d"));
    EXPECT_FALSE(str.ends_with("hello"));
}

// ============================================================================
// Iterator Tests
// ============================================================================

class FixedStringIteratorTest : public ::testing::Test {};

TEST_F(FixedStringIteratorTest, RangeBasedFor) {
    FixedString<32> str("abc");
    std::string result;
    for (char c : str) {
        result += c;
    }
    EXPECT_EQ(result, "abc");
}

TEST_F(FixedStringIteratorTest, BeginEnd) {
    FixedString<32> str("test");
    EXPECT_EQ(str.end() - str.begin(), 4);
}

// ============================================================================
// Hash Tests
// ============================================================================

class FixedStringHashTest : public ::testing::Test {};

TEST_F(FixedStringHashTest, HashIsConsistent) {
    FixedString<32> str1("hash");
    FixedString<32> str2("hash");
    EXPECT_EQ(str1.hash(), str2.hash());
}

TEST_F(FixedStringHashTest, DifferentStringsHaveDifferentHash) {
    FixedString<32> str1("one");
    FixedString<32> str2("two");
    EXPECT_NE(str1.hash(), str2.hash());
}

TEST_F(FixedStringHashTest, WorksWithUnorderedSet) {
    std::unordered_set<FixedString<32>> set;
    set.insert(FixedString<32>("one"));
    set.insert(FixedString<32>("two"));
    set.insert(FixedString<32>("one"));  // Duplicate

    EXPECT_EQ(set.size(), 2u);
    EXPECT_TRUE(set.count(FixedString<32>("one")) > 0);
}

// ============================================================================
// Type Alias Tests
// ============================================================================

class TypeAliasTest : public ::testing::Test {};

TEST_F(TypeAliasTest, TopicStringHas64Capacity) {
    TopicString topic;
    EXPECT_EQ(topic.capacity(), 64u);
    EXPECT_EQ(topic.max_size(), 63u);
}

TEST_F(TypeAliasTest, IdentifierStringHas32Capacity) {
    IdentifierString id;
    EXPECT_EQ(id.capacity(), 32u);
    EXPECT_EQ(id.max_size(), 31u);
}

TEST_F(TypeAliasTest, ShortStringHas16Capacity) {
    ShortString short_str;
    EXPECT_EQ(short_str.capacity(), 16u);
}

TEST_F(TypeAliasTest, AddressStringHas128Capacity) {
    AddressString addr;
    EXPECT_EQ(addr.capacity(), 128u);
}

TEST_F(TypeAliasTest, LongStringHas256Capacity) {
    LongString long_str;
    EXPECT_EQ(long_str.capacity(), 256u);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

class FixedStringEdgeCaseTest : public ::testing::Test {};

TEST_F(FixedStringEdgeCaseTest, EmptyStringOperations) {
    FixedString<32> str;
    EXPECT_EQ(str.back(), '\0');
    EXPECT_EQ(str.find('x'), std::string_view::npos);
    EXPECT_FALSE(str.starts_with("x"));
    EXPECT_FALSE(str.ends_with("x"));
}

TEST_F(FixedStringEdgeCaseTest, SingleCharOperations) {
    FixedString<32> str("x");
    EXPECT_EQ(str.front(), 'x');
    EXPECT_EQ(str.back(), 'x');
    EXPECT_TRUE(str.starts_with("x"));
    EXPECT_TRUE(str.ends_with("x"));
}

TEST_F(FixedStringEdgeCaseTest, FullCapacityString) {
    FixedString<8> str("1234567");  // 7 chars, max is 7
    EXPECT_EQ(str.size(), 7u);
    EXPECT_FALSE(str.push_back('8'));
    EXPECT_EQ(str.size(), 7u);
}

TEST_F(FixedStringEdgeCaseTest, MutableElementAccess) {
    FixedString<32> str("abc");
    str[1] = 'X';
    EXPECT_STREQ(str.c_str(), "aXc");
}
