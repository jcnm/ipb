#pragma once

/**
 * @file fixed_string.hpp
 * @brief Fixed-size string types for zero-allocation hot paths
 *
 * Provides stack-allocated string alternatives to std::string for:
 * - Topic names
 * - Source IDs
 * - Channel names
 * - Task names
 *
 * Benefits:
 * - Zero heap allocation
 * - Cache-friendly (fits in cache line)
 * - Deterministic performance
 * - Safe for real-time operations
 */

#include <ipb/common/platform.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>

namespace ipb::common {

/**
 * @brief Fixed-size string with no heap allocation
 *
 * @tparam N Maximum string length (including null terminator)
 */
template <size_t N>
class FixedString {
    static_assert(N > 0, "FixedString must have positive capacity");

public:
    static constexpr size_t MAX_LENGTH = N - 1;
    static constexpr size_t CAPACITY = N;

    /// Default constructor - empty string
    constexpr FixedString() noexcept : size_(0) {
        data_[0] = '\0';
    }

    /// Construct from C string
    constexpr FixedString(const char* str) noexcept : size_(0) {
        if (str) {
            assign(str);
        } else {
            data_[0] = '\0';
        }
    }

    /// Construct from string_view
    constexpr FixedString(std::string_view sv) noexcept : size_(0) {
        assign(sv);
    }

    /// Construct from std::string
    FixedString(const std::string& str) noexcept : size_(0) {
        assign(str);
    }

    /// Copy constructor
    constexpr FixedString(const FixedString& other) noexcept
        : size_(other.size_) {
        std::copy_n(other.data_.data(), size_ + 1, data_.data());
    }

    /// Move constructor (same as copy for fixed-size)
    constexpr FixedString(FixedString&& other) noexcept
        : size_(other.size_) {
        std::copy_n(other.data_.data(), size_ + 1, data_.data());
    }

    /// Copy assignment
    constexpr FixedString& operator=(const FixedString& other) noexcept {
        if (this != &other) {
            size_ = other.size_;
            std::copy_n(other.data_.data(), size_ + 1, data_.data());
        }
        return *this;
    }

    /// Move assignment
    constexpr FixedString& operator=(FixedString&& other) noexcept {
        return *this = other;
    }

    /// Assign from C string
    constexpr FixedString& operator=(const char* str) noexcept {
        assign(str);
        return *this;
    }

    /// Assign from string_view
    constexpr FixedString& operator=(std::string_view sv) noexcept {
        assign(sv);
        return *this;
    }

    // =========================================================================
    // Assignment
    // =========================================================================

    constexpr void assign(const char* str) noexcept {
        if (!str) {
            clear();
            return;
        }
        size_t len = 0;
        while (str[len] && len < MAX_LENGTH) {
            data_[len] = str[len];
            ++len;
        }
        size_ = len;
        data_[size_] = '\0';
    }

    constexpr void assign(std::string_view sv) noexcept {
        size_ = std::min(sv.size(), MAX_LENGTH);
        std::copy_n(sv.data(), size_, data_.data());
        data_[size_] = '\0';
    }

    constexpr void assign(const char* str, size_t len) noexcept {
        size_ = std::min(len, MAX_LENGTH);
        std::copy_n(str, size_, data_.data());
        data_[size_] = '\0';
    }

    // =========================================================================
    // Accessors
    // =========================================================================

    /// Get as C string
    constexpr const char* c_str() const noexcept { return data_.data(); }

    /// Get as char pointer
    constexpr const char* data() const noexcept { return data_.data(); }

    /// Get mutable data pointer
    constexpr char* data() noexcept { return data_.data(); }

    /// Get as string_view
    constexpr std::string_view view() const noexcept {
        return std::string_view(data_.data(), size_);
    }

    /// Implicit conversion to string_view
    constexpr operator std::string_view() const noexcept {
        return view();
    }

    /// Convert to std::string
    std::string to_string() const {
        return std::string(data_.data(), size_);
    }

    /// Get length
    constexpr size_t size() const noexcept { return size_; }
    constexpr size_t length() const noexcept { return size_; }

    /// Check if empty
    constexpr bool empty() const noexcept { return size_ == 0; }

    /// Get maximum capacity
    static constexpr size_t max_size() noexcept { return MAX_LENGTH; }
    static constexpr size_t capacity() noexcept { return CAPACITY; }

    /// Element access
    constexpr char operator[](size_t pos) const noexcept {
        return data_[pos];
    }

    constexpr char& operator[](size_t pos) noexcept {
        return data_[pos];
    }

    /// Front and back
    constexpr char front() const noexcept { return data_[0]; }
    constexpr char back() const noexcept { return size_ > 0 ? data_[size_ - 1] : '\0'; }

    // =========================================================================
    // Modifiers
    // =========================================================================

    /// Clear the string
    constexpr void clear() noexcept {
        size_ = 0;
        data_[0] = '\0';
    }

    /// Append character
    constexpr bool push_back(char c) noexcept {
        if (size_ < MAX_LENGTH) {
            data_[size_++] = c;
            data_[size_] = '\0';
            return true;
        }
        return false;
    }

    /// Remove last character
    constexpr void pop_back() noexcept {
        if (size_ > 0) {
            data_[--size_] = '\0';
        }
    }

    /// Append string
    constexpr bool append(std::string_view sv) noexcept {
        size_t to_copy = std::min(sv.size(), MAX_LENGTH - size_);
        std::copy_n(sv.data(), to_copy, data_.data() + size_);
        size_ += to_copy;
        data_[size_] = '\0';
        return to_copy == sv.size();
    }

    /// Append operator
    constexpr FixedString& operator+=(std::string_view sv) noexcept {
        append(sv);
        return *this;
    }

    constexpr FixedString& operator+=(char c) noexcept {
        push_back(c);
        return *this;
    }

    // =========================================================================
    // Comparison
    // =========================================================================

    constexpr bool operator==(const FixedString& other) const noexcept {
        if (size_ != other.size_) return false;
        return std::equal(data_.data(), data_.data() + size_, other.data_.data());
    }

    constexpr bool operator!=(const FixedString& other) const noexcept {
        return !(*this == other);
    }

    constexpr bool operator==(std::string_view sv) const noexcept {
        return view() == sv;
    }

    constexpr bool operator!=(std::string_view sv) const noexcept {
        return view() != sv;
    }

    constexpr bool operator<(const FixedString& other) const noexcept {
        return view() < other.view();
    }

    constexpr bool operator<=(const FixedString& other) const noexcept {
        return view() <= other.view();
    }

    constexpr bool operator>(const FixedString& other) const noexcept {
        return view() > other.view();
    }

    constexpr bool operator>=(const FixedString& other) const noexcept {
        return view() >= other.view();
    }

    // =========================================================================
    // Search
    // =========================================================================

    constexpr size_t find(char c, size_t pos = 0) const noexcept {
        for (size_t i = pos; i < size_; ++i) {
            if (data_[i] == c) return i;
        }
        return std::string_view::npos;
    }

    constexpr size_t find(std::string_view sv, size_t pos = 0) const noexcept {
        return view().find(sv, pos);
    }

    constexpr bool contains(char c) const noexcept {
        return find(c) != std::string_view::npos;
    }

    constexpr bool contains(std::string_view sv) const noexcept {
        return find(sv) != std::string_view::npos;
    }

    constexpr bool starts_with(std::string_view sv) const noexcept {
        return view().substr(0, sv.size()) == sv;
    }

    constexpr bool ends_with(std::string_view sv) const noexcept {
        if (sv.size() > size_) return false;
        return view().substr(size_ - sv.size()) == sv;
    }

    // =========================================================================
    // Iterators
    // =========================================================================

    constexpr const char* begin() const noexcept { return data_.data(); }
    constexpr const char* end() const noexcept { return data_.data() + size_; }
    constexpr char* begin() noexcept { return data_.data(); }
    constexpr char* end() noexcept { return data_.data() + size_; }

    // =========================================================================
    // Hash support
    // =========================================================================

    size_t hash() const noexcept {
        return std::hash<std::string_view>{}(view());
    }

private:
    std::array<char, N> data_;
    size_t size_;
};

// ============================================================================
// COMMON FIXED STRING TYPES
// ============================================================================

/// Topic name (max 64 chars - fits in cache line)
using TopicString = FixedString<64>;

/// Source/sink ID (max 32 chars)
using IdentifierString = FixedString<32>;

/// Short name (max 16 chars)
using ShortString = FixedString<16>;

/// Address string (max 128 chars)
using AddressString = FixedString<128>;

/// Long string (max 256 chars)
using LongString = FixedString<256>;

// ============================================================================
// HASH SPECIALIZATIONS
// ============================================================================

}  // namespace ipb::common

namespace std {

template <size_t N>
struct hash<ipb::common::FixedString<N>> {
    size_t operator()(const ipb::common::FixedString<N>& s) const noexcept {
        return s.hash();
    }
};

}  // namespace std
