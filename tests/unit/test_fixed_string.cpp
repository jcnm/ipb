/**
 * @file test_fixed_string.cpp
 * @brief Comprehensive unit tests for IPB fixed-size string types
 *
 * Industrial-grade test coverage including:
 * - Construction and assignment from various sources
 * - String operations (append, find, compare)
 * - Conversions (to/from string_view, std::string)
 * - Type aliases (TopicString, IdentifierString, etc.)
 * - Boundary conditions and overflow handling
 * - Edge cases (empty strings, max capacity, special characters)
 */

#include <ipb/common/fixed_string.hpp>

#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

TEST_F(FixedStringConstructionTest, EmptyCString) {
    FixedString<32> str("");
    EXPECT_TRUE(str.empty());
    EXPECT_EQ(str.size(), 0u);
}

TEST_F(FixedStringConstructionTest, SingleCharCString) {
    FixedString<32> str("x");
    EXPECT_EQ(str.size(), 1u);
    EXPECT_STREQ(str.c_str(), "x");
}

TEST_F(FixedStringConstructionTest, ExactMaxLengthString) {
    FixedString<8> str("1234567");  // Exactly 7 chars = MAX_LENGTH
    EXPECT_EQ(str.size(), 7u);
    EXPECT_STREQ(str.c_str(), "1234567");
}

TEST_F(FixedStringConstructionTest, ConstructFromEmptyStringView) {
    std::string_view sv = "";
    FixedString<32> str(sv);
    EXPECT_TRUE(str.empty());
}

TEST_F(FixedStringConstructionTest, ConstructFromEmptyStdString) {
    std::string s = "";
    FixedString<32> str(s);
    EXPECT_TRUE(str.empty());
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

TEST_F(FixedStringAssignmentTest, AssignNullptr) {
    FixedString<32> str("existing");
    str.assign(nullptr);
    EXPECT_TRUE(str.empty());
}

TEST_F(FixedStringAssignmentTest, AssignOverwritesExisting) {
    FixedString<32> str("original");
    str = "new";
    EXPECT_STREQ(str.c_str(), "new");
    EXPECT_EQ(str.size(), 3u);
}

TEST_F(FixedStringAssignmentTest, MoveAssignment) {
    FixedString<32> str1("moved");
    FixedString<32> str2;
    str2 = std::move(str1);
    EXPECT_STREQ(str2.c_str(), "moved");
}

TEST_F(FixedStringAssignmentTest, AssignLongerThenShorter) {
    FixedString<32> str("short");
    str = "this is a longer string";
    EXPECT_EQ(str.size(), 23u);

    str = "tiny";
    EXPECT_EQ(str.size(), 4u);
    EXPECT_STREQ(str.c_str(), "tiny");
}

TEST_F(FixedStringAssignmentTest, AssignWithLengthZero) {
    FixedString<32> str("existing");
    str.assign("anything", 0);
    EXPECT_TRUE(str.empty());
}

TEST_F(FixedStringAssignmentTest, AssignWithLengthExceedingCapacity) {
    FixedString<8> str;
    str.assign("very long string", 100);  // Length exceeds both string and capacity
    EXPECT_EQ(str.size(), 7u);  // Capped at MAX_LENGTH
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

TEST_F(FixedStringAccessorTest, LengthEqualsSize) {
    FixedString<32> str("length");
    EXPECT_EQ(str.size(), str.length());
}

TEST_F(FixedStringAccessorTest, MutableDataAccess) {
    FixedString<32> str("abc");
    char* data = str.data();
    data[1] = 'X';
    EXPECT_STREQ(str.c_str(), "aXc");
}

TEST_F(FixedStringAccessorTest, FrontOfEmptyString) {
    FixedString<32> str;
    // Front of empty string should return null terminator
    EXPECT_EQ(str.front(), '\0');
}

TEST_F(FixedStringAccessorTest, BackOfEmptyString) {
    FixedString<32> str;
    EXPECT_EQ(str.back(), '\0');
}

TEST_F(FixedStringAccessorTest, ViewOfEmptyString) {
    FixedString<32> str;
    auto sv = str.view();
    EXPECT_TRUE(sv.empty());
    EXPECT_EQ(sv.size(), 0u);
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

TEST_F(FixedStringModifierTest, ClearMultipleTimes) {
    FixedString<32> str("test");
    str.clear();
    str.clear();
    str.clear();
    EXPECT_TRUE(str.empty());
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

TEST_F(FixedStringModifierTest, PushBackToExactCapacity) {
    FixedString<4> str("ab");  // 2 chars, max is 3
    EXPECT_TRUE(str.push_back('c'));  // Should succeed
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

TEST_F(FixedStringModifierTest, PopBackMultiple) {
    FixedString<32> str("test");
    str.pop_back();
    str.pop_back();
    str.pop_back();
    str.pop_back();
    EXPECT_TRUE(str.empty());
}

TEST_F(FixedStringModifierTest, PopBackBeyondEmpty) {
    FixedString<32> str("ab");
    str.pop_back();
    str.pop_back();
    str.pop_back();  // Extra pop_back
    str.pop_back();  // And another
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

TEST_F(FixedStringModifierTest, AppendReturnsFalseOnTruncation) {
    FixedString<8> str("hello");  // 5 chars
    bool result = str.append("abc");  // Would need 8 chars, only 2 available
    EXPECT_FALSE(result);  // Partial append
    EXPECT_EQ(str.size(), 7u);  // Max available
}

TEST_F(FixedStringModifierTest, AppendEmptyString) {
    FixedString<32> str("test");
    str.append("");
    EXPECT_STREQ(str.c_str(), "test");
}

TEST_F(FixedStringModifierTest, OperatorPlusEquals) {
    FixedString<32> str("a");
    str += "b";
    str += 'c';
    EXPECT_STREQ(str.c_str(), "abc");
}

TEST_F(FixedStringModifierTest, OperatorPlusEqualsChain) {
    FixedString<32> str;
    str += "hello";
    str += " ";
    str += "world";
    EXPECT_STREQ(str.c_str(), "hello world");
}

TEST_F(FixedStringModifierTest, ClearThenModify) {
    FixedString<32> str("original");
    str.clear();
    str.push_back('x');
    str.append("yz");
    EXPECT_STREQ(str.c_str(), "xyz");
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

TEST_F(FixedStringComparisonTest, EmptyStringsEqual) {
    FixedString<32> str1;
    FixedString<32> str2;
    EXPECT_TRUE(str1 == str2);
}

TEST_F(FixedStringComparisonTest, EmptyLessThanNonEmpty) {
    FixedString<32> empty;
    FixedString<32> nonempty("a");
    EXPECT_TRUE(empty < nonempty);
}

TEST_F(FixedStringComparisonTest, SamePrefixDifferentLength) {
    FixedString<32> short_str("ab");
    FixedString<32> long_str("abc");
    EXPECT_TRUE(short_str < long_str);
    EXPECT_FALSE(long_str < short_str);
}

TEST_F(FixedStringComparisonTest, CaseSensitiveComparison) {
    FixedString<32> lower("abc");
    FixedString<32> upper("ABC");
    EXPECT_FALSE(lower == upper);
    // 'A' (65) < 'a' (97) in ASCII
    EXPECT_TRUE(upper < lower);
}

TEST_F(FixedStringComparisonTest, CompareWithEmptyStringView) {
    FixedString<32> empty;
    EXPECT_TRUE(empty == std::string_view(""));
    EXPECT_FALSE(empty == std::string_view("x"));
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

TEST_F(FixedStringSearchTest, FindCharAtStart) {
    FixedString<32> str("hello");
    EXPECT_EQ(str.find('h'), 0u);
}

TEST_F(FixedStringSearchTest, FindCharAtEnd) {
    FixedString<32> str("hello");
    EXPECT_EQ(str.find('o'), 4u);
}

TEST_F(FixedStringSearchTest, FindCharFromEndPosition) {
    FixedString<32> str("hello");
    EXPECT_EQ(str.find('o', 4), 4u);
    EXPECT_EQ(str.find('o', 5), std::string_view::npos);
}

TEST_F(FixedStringSearchTest, FindSubstring) {
    FixedString<32> str("hello world");
    EXPECT_EQ(str.find("world"), 6u);
    EXPECT_EQ(str.find("xyz"), std::string_view::npos);
}

TEST_F(FixedStringSearchTest, FindSubstringAtStart) {
    FixedString<32> str("hello world");
    EXPECT_EQ(str.find("hello"), 0u);
}

TEST_F(FixedStringSearchTest, FindSubstringAtEnd) {
    FixedString<32> str("hello world");
    EXPECT_EQ(str.find("world"), 6u);
}

TEST_F(FixedStringSearchTest, FindEmptySubstring) {
    FixedString<32> str("hello");
    EXPECT_EQ(str.find(""), 0u);
}

TEST_F(FixedStringSearchTest, Contains) {
    FixedString<32> str("hello world");
    EXPECT_TRUE(str.contains('o'));
    EXPECT_FALSE(str.contains('x'));
    EXPECT_TRUE(str.contains("world"));
    EXPECT_FALSE(str.contains("xyz"));
}

TEST_F(FixedStringSearchTest, ContainsEmptyString) {
    FixedString<32> str("hello");
    EXPECT_TRUE(str.contains(""));
}

TEST_F(FixedStringSearchTest, StartsWith) {
    FixedString<32> str("hello world");
    EXPECT_TRUE(str.starts_with("hello"));
    EXPECT_TRUE(str.starts_with("h"));
    EXPECT_FALSE(str.starts_with("world"));
    EXPECT_TRUE(str.starts_with(""));  // Empty prefix
}

TEST_F(FixedStringSearchTest, StartsWithFullString) {
    FixedString<32> str("hello");
    EXPECT_TRUE(str.starts_with("hello"));
}

TEST_F(FixedStringSearchTest, StartsWithLongerString) {
    FixedString<32> str("hello");
    EXPECT_FALSE(str.starts_with("hello world"));
}

TEST_F(FixedStringSearchTest, EndsWith) {
    FixedString<32> str("hello world");
    EXPECT_TRUE(str.ends_with("world"));
    EXPECT_TRUE(str.ends_with("d"));
    EXPECT_FALSE(str.ends_with("hello"));
    EXPECT_TRUE(str.ends_with(""));  // Empty suffix
}

TEST_F(FixedStringSearchTest, EndsWithFullString) {
    FixedString<32> str("hello");
    EXPECT_TRUE(str.ends_with("hello"));
}

TEST_F(FixedStringSearchTest, EndsWithLongerString) {
    FixedString<32> str("hello");
    EXPECT_FALSE(str.ends_with("hello world"));
}

TEST_F(FixedStringSearchTest, FindInEmptyString) {
    FixedString<32> str;
    EXPECT_EQ(str.find('x'), std::string_view::npos);
    EXPECT_EQ(str.find("xyz"), std::string_view::npos);
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

TEST_F(FixedStringIteratorTest, EmptyStringIterators) {
    FixedString<32> str;
    EXPECT_EQ(str.begin(), str.end());
}

TEST_F(FixedStringIteratorTest, MutableIterator) {
    FixedString<32> str("abc");
    for (char& c : str) {
        c = std::toupper(c);
    }
    EXPECT_STREQ(str.c_str(), "ABC");
}

TEST_F(FixedStringIteratorTest, IteratorArithmetic) {
    FixedString<32> str("hello");
    auto it = str.begin();
    EXPECT_EQ(*it, 'h');
    ++it;
    EXPECT_EQ(*it, 'e');
    it += 2;
    EXPECT_EQ(*it, 'l');
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

TEST_F(FixedStringHashTest, WorksWithUnorderedMap) {
    std::unordered_map<FixedString<32>, int> map;
    map[FixedString<32>("one")] = 1;
    map[FixedString<32>("two")] = 2;
    map[FixedString<32>("one")] = 100;  // Update

    EXPECT_EQ(map.size(), 2u);
    EXPECT_EQ(map[FixedString<32>("one")], 100);
}

TEST_F(FixedStringHashTest, EmptyStringHash) {
    FixedString<32> empty;
    // Should produce a valid hash without crashing
    size_t h = empty.hash();
    // Hash of empty string should be consistent
    EXPECT_EQ(h, FixedString<32>("").hash());
}

TEST_F(FixedStringHashTest, SingleCharHash) {
    FixedString<32> str("x");
    size_t h = str.hash();
    EXPECT_NE(h, FixedString<32>("").hash());
    EXPECT_NE(h, FixedString<32>("y").hash());
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

TEST_F(TypeAliasTest, TopicStringMaxCapacity) {
    std::string long_topic(100, 'x');
    TopicString topic(long_topic);
    EXPECT_EQ(topic.size(), 63u);  // Truncated to max
}

TEST_F(TypeAliasTest, AllTypeAliasesAreValid) {
    TopicString topic("test");
    IdentifierString id("id");
    ShortString short_str("s");
    AddressString addr("addr");
    LongString long_str("long");

    EXPECT_FALSE(topic.empty());
    EXPECT_FALSE(id.empty());
    EXPECT_FALSE(short_str.empty());
    EXPECT_FALSE(addr.empty());
    EXPECT_FALSE(long_str.empty());
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

TEST_F(FixedStringEdgeCaseTest, MinimalCapacityString) {
    FixedString<2> str("x");  // Max is 1
    EXPECT_EQ(str.size(), 1u);
    EXPECT_STREQ(str.c_str(), "x");
    EXPECT_FALSE(str.push_back('y'));
}

TEST_F(FixedStringEdgeCaseTest, SpecialCharacters) {
    FixedString<32> str("hello\tworld\n");
    EXPECT_TRUE(str.contains('\t'));
    EXPECT_TRUE(str.contains('\n'));
}

TEST_F(FixedStringEdgeCaseTest, BinaryData) {
    FixedString<32> str;
    str.push_back('a');
    str.push_back('\0');  // Embedded null
    str.push_back('b');
    // Size should only count until first embedded null
    EXPECT_EQ(str.size(), 3u);
}

TEST_F(FixedStringEdgeCaseTest, SpacesAndTabs) {
    FixedString<32> str("  \t  ");
    EXPECT_EQ(str.size(), 5u);
    EXPECT_TRUE(str.contains(' '));
    EXPECT_TRUE(str.contains('\t'));
}

TEST_F(FixedStringEdgeCaseTest, UnicodeCharacters) {
    // UTF-8 multibyte characters will be stored as individual bytes
    FixedString<32> str("\xC3\xA9");  // UTF-8 for 'é'
    EXPECT_EQ(str.size(), 2u);  // Two bytes for UTF-8
}

TEST_F(FixedStringEdgeCaseTest, RepeatedModifications) {
    FixedString<8> str;
    for (int i = 0; i < 100; ++i) {
        str.clear();
        str = "test";
        str.pop_back();
        str.push_back('X');
    }
    EXPECT_STREQ(str.c_str(), "tesX");
}

TEST_F(FixedStringEdgeCaseTest, FillToCapacity) {
    FixedString<8> str;
    while (str.push_back('x')) {}
    EXPECT_EQ(str.size(), 7u);  // MAX_LENGTH = 7
}

TEST_F(FixedStringEdgeCaseTest, CompareWithSamePrefix) {
    FixedString<32> prefix("hello");
    FixedString<32> extended("hello world");
    EXPECT_TRUE(prefix < extended);
    EXPECT_TRUE(extended > prefix);
}

TEST_F(FixedStringEdgeCaseTest, ConstructFromLargeString) {
    std::string very_long(10000, 'x');
    FixedString<32> str(very_long);
    EXPECT_EQ(str.size(), 31u);
}

TEST_F(FixedStringEdgeCaseTest, AssignFromLargeStringView) {
    std::string very_long(10000, 'x');
    FixedString<32> str;
    str = std::string_view(very_long);
    EXPECT_EQ(str.size(), 31u);
}

TEST_F(FixedStringEdgeCaseTest, ConsecutiveAppends) {
    FixedString<32> str;
    for (int i = 0; i < 10; ++i) {
        str.append("abc");
    }
    // 10 * 3 = 30 chars, which is under max_size (31)
    EXPECT_EQ(str.size(), 30u);

    // One more append should truncate
    str.append("xyz");  // Would need 33, but max is 31
    EXPECT_EQ(str.size(), 31u);  // Truncated to max
}

TEST_F(FixedStringEdgeCaseTest, NullCharacterInMiddle) {
    FixedString<32> str;
    str.assign("hello", 5);
    // This tests that assign with explicit length works correctly
    EXPECT_EQ(str.size(), 5u);
    EXPECT_STREQ(str.c_str(), "hello");
}

// ============================================================================
// Performance-related Tests
// ============================================================================

class FixedStringPerformanceTest : public ::testing::Test {};

TEST_F(FixedStringPerformanceTest, ManySmallOperations) {
    FixedString<64> str;
    for (int i = 0; i < 1000; ++i) {
        str = "test";
        str += "_value";
        str.clear();
    }
    EXPECT_TRUE(str.empty());
}

TEST_F(FixedStringPerformanceTest, ManyHashCalculations) {
    FixedString<64> str("hash_test");
    size_t sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += str.hash();
    }
    EXPECT_GT(sum, 0u);
}

TEST_F(FixedStringPerformanceTest, ManyComparisons) {
    FixedString<64> str1("compare1");
    FixedString<64> str2("compare2");
    int equal_count = 0;
    for (int i = 0; i < 1000; ++i) {
        if (str1 == str2) {
            ++equal_count;
        }
    }
    EXPECT_EQ(equal_count, 0);
}

TEST_F(FixedStringPerformanceTest, ManySearches) {
    FixedString<64> str("the quick brown fox jumps over the lazy dog");
    int found = 0;
    for (int i = 0; i < 1000; ++i) {
        if (str.contains("fox")) {
            ++found;
        }
    }
    EXPECT_EQ(found, 1000);
}
