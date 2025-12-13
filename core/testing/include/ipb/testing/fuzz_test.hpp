#pragma once

/**
 * @file fuzz_test.hpp
 * @brief Fuzz testing infrastructure for finding edge cases
 *
 * Features:
 * - Property-based testing
 * - Input generation (random, boundary, mutation)
 * - Shrinking failed cases
 * - Coverage-guided fuzzing integration
 * - Corpus management
 * - Crash detection
 *
 * Usage:
 * @code
 * FuzzTest<std::string> fuzz;
 * fuzz.generate([]() {
 *     return RandomGen::string(0, 1000);
 * });
 *
 * fuzz.test([](const std::string& input) {
 *     // Test function that should not crash
 *     Parser parser;
 *     parser.parse(input);
 * });
 *
 * auto result = fuzz.run(10000);
 * @endcode
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace ipb::testing {

//=============================================================================
// Random Generation
//=============================================================================

/**
 * @brief Random value generator
 */
class RandomGen {
public:
    explicit RandomGen(uint64_t seed = 0)
        : rng_(seed == 0 ? std::random_device{}() : seed) {}

    // Integers
    template<typename T>
    T integer(T min = std::numeric_limits<T>::min(),
              T max = std::numeric_limits<T>::max()) {
        std::uniform_int_distribution<T> dist(min, max);
        return dist(rng_);
    }

    // Floats
    template<typename T>
    T floating(T min = 0.0, T max = 1.0) {
        std::uniform_real_distribution<T> dist(min, max);
        return dist(rng_);
    }

    // Boolean
    bool boolean(double probability = 0.5) {
        std::bernoulli_distribution dist(probability);
        return dist(rng_);
    }

    // String
    std::string string(size_t min_len = 0, size_t max_len = 100) {
        size_t len = integer<size_t>(min_len, max_len);
        std::string result;
        result.reserve(len);

        std::uniform_int_distribution<int> char_dist(32, 126);  // Printable ASCII
        for (size_t i = 0; i < len; ++i) {
            result += static_cast<char>(char_dist(rng_));
        }
        return result;
    }

    // Binary data
    std::vector<uint8_t> bytes(size_t min_len = 0, size_t max_len = 100) {
        size_t len = integer<size_t>(min_len, max_len);
        std::vector<uint8_t> result(len);

        std::uniform_int_distribution<int> byte_dist(0, 255);
        for (auto& b : result) {
            b = static_cast<uint8_t>(byte_dist(rng_));
        }
        return result;
    }

    // Pick from list
    template<typename T>
    T pick(const std::vector<T>& choices) {
        if (choices.empty()) {
            throw std::runtime_error("Cannot pick from empty list");
        }
        return choices[integer<size_t>(0, choices.size() - 1)];
    }

    // Shuffle
    template<typename T>
    void shuffle(std::vector<T>& vec) {
        std::shuffle(vec.begin(), vec.end(), rng_);
    }

    // Get underlying generator
    std::mt19937_64& engine() { return rng_; }

private:
    std::mt19937_64 rng_;
};

//=============================================================================
// Boundary Values
//=============================================================================

/**
 * @brief Generates boundary/edge case values
 */
class BoundaryGen {
public:
    // Integer boundaries
    template<typename T>
    static std::vector<T> integers() {
        return {
            std::numeric_limits<T>::min(),
            std::numeric_limits<T>::min() + 1,
            static_cast<T>(-1),
            static_cast<T>(0),
            static_cast<T>(1),
            std::numeric_limits<T>::max() - 1,
            std::numeric_limits<T>::max()
        };
    }

    // Unsigned integer boundaries
    template<typename T>
    static std::vector<T> unsigned_integers() {
        return {
            static_cast<T>(0),
            static_cast<T>(1),
            std::numeric_limits<T>::max() / 2,
            std::numeric_limits<T>::max() - 1,
            std::numeric_limits<T>::max()
        };
    }

    // Float boundaries
    template<typename T>
    static std::vector<T> floats() {
        return {
            -std::numeric_limits<T>::infinity(),
            std::numeric_limits<T>::lowest(),
            static_cast<T>(-1.0),
            static_cast<T>(-0.0),
            static_cast<T>(0.0),
            std::numeric_limits<T>::min(),  // Smallest positive
            std::numeric_limits<T>::epsilon(),
            static_cast<T>(1.0),
            std::numeric_limits<T>::max(),
            std::numeric_limits<T>::infinity(),
            std::numeric_limits<T>::quiet_NaN()
        };
    }

    // String boundaries
    static std::vector<std::string> strings() {
        return {
            "",                          // Empty
            " ",                         // Single space
            "\t\n\r",                   // Whitespace
            std::string(1, '\0'),       // Null byte
            std::string(1000, 'a'),     // Long string
            std::string(10000, 'x'),    // Very long string
            "🎉🔥💻",                    // Unicode
            "\xff\xfe",                 // Invalid UTF-8
            "<script>alert(1)</script>", // XSS attempt
            "'; DROP TABLE users; --",  // SQL injection
            "../../../etc/passwd",      // Path traversal
            "A" + std::string(100, 'A') + "B"  // Buffer patterns
        };
    }

    // Size boundaries
    static std::vector<size_t> sizes() {
        return {0, 1, 2, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128,
                255, 256, 511, 512, 1023, 1024, 4095, 4096,
                65535, 65536};
    }
};

//=============================================================================
// Input Mutator
//=============================================================================

/**
 * @brief Mutates inputs to find edge cases
 */
class Mutator {
public:
    explicit Mutator(uint64_t seed = 0) : rng_(seed) {}

    // Mutate bytes
    std::vector<uint8_t> mutate_bytes(const std::vector<uint8_t>& input) {
        if (input.empty()) {
            return rng_.bytes(1, 10);
        }

        std::vector<uint8_t> result = input;
        int strategy = rng_.integer(0, 6);

        switch (strategy) {
            case 0:  // Bit flip
                if (!result.empty()) {
                    size_t pos = rng_.integer<size_t>(0, result.size() - 1);
                    result[pos] ^= (1 << rng_.integer(0, 7));
                }
                break;

            case 1:  // Byte flip
                if (!result.empty()) {
                    size_t pos = rng_.integer<size_t>(0, result.size() - 1);
                    result[pos] = rng_.integer<uint8_t>(0, 255);
                }
                break;

            case 2:  // Insert byte
                {
                    size_t pos = rng_.integer<size_t>(0, result.size());
                    result.insert(result.begin() + pos, rng_.integer<uint8_t>(0, 255));
                }
                break;

            case 3:  // Delete byte
                if (!result.empty()) {
                    size_t pos = rng_.integer<size_t>(0, result.size() - 1);
                    result.erase(result.begin() + pos);
                }
                break;

            case 4:  // Duplicate chunk
                if (result.size() >= 4) {
                    size_t start = rng_.integer<size_t>(0, result.size() - 4);
                    size_t len = rng_.integer<size_t>(1, std::min<size_t>(4, result.size() - start));
                    result.insert(result.begin() + start,
                                  result.begin() + start,
                                  result.begin() + start + len);
                }
                break;

            case 5:  // Set to boundary value
                if (!result.empty()) {
                    size_t pos = rng_.integer<size_t>(0, result.size() - 1);
                    result[pos] = rng_.pick<uint8_t>({0, 1, 127, 128, 254, 255});
                }
                break;

            case 6:  // Arithmetic mutation
                if (!result.empty()) {
                    size_t pos = rng_.integer<size_t>(0, result.size() - 1);
                    int delta = rng_.integer(-35, 35);
                    result[pos] = static_cast<uint8_t>(result[pos] + delta);
                }
                break;
        }

        return result;
    }

    // Mutate string
    std::string mutate_string(const std::string& input) {
        std::vector<uint8_t> bytes(input.begin(), input.end());
        auto mutated = mutate_bytes(bytes);
        return std::string(mutated.begin(), mutated.end());
    }

    // Mutate integer
    template<typename T>
    T mutate_integer(T value) {
        int strategy = rng_.integer(0, 4);

        switch (strategy) {
            case 0: return value + rng_.integer<T>(-10, 10);
            case 1: return value * 2;
            case 2: return value / 2;
            case 3: return -value;
            case 4: return rng_.pick(BoundaryGen::integers<T>());
        }
        return value;
    }

private:
    RandomGen rng_;
};

//=============================================================================
// Shrinking
//=============================================================================

/**
 * @brief Shrinks failed inputs to minimal reproduction case
 */
template<typename T>
class Shrinker {
public:
    using TestFunc = std::function<bool(const T&)>;

    /**
     * @brief Shrink input while maintaining failure
     */
    T shrink(const T& input, TestFunc test_fails) {
        T current = input;
        bool progress = true;

        while (progress) {
            progress = false;
            auto candidates = generate_shrink_candidates(current);

            for (const auto& candidate : candidates) {
                if (test_fails(candidate)) {
                    current = candidate;
                    progress = true;
                    break;
                }
            }
        }

        return current;
    }

private:
    std::vector<T> generate_shrink_candidates(const T& input);
};

// Specialization for string
template<>
inline std::vector<std::string> Shrinker<std::string>::generate_shrink_candidates(
        const std::string& input) {
    std::vector<std::string> candidates;

    if (input.empty()) return candidates;

    // Try removing characters
    for (size_t i = 0; i < input.size(); ++i) {
        std::string s = input;
        s.erase(i, 1);
        candidates.push_back(s);
    }

    // Try removing chunks
    for (size_t len = input.size() / 2; len >= 1; len /= 2) {
        for (size_t i = 0; i + len <= input.size(); ++i) {
            std::string s = input;
            s.erase(i, len);
            candidates.push_back(s);
        }
    }

    // Try simplifying characters
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != 'a') {
            std::string s = input;
            s[i] = 'a';
            candidates.push_back(s);
        }
        if (input[i] != '0') {
            std::string s = input;
            s[i] = '0';
            candidates.push_back(s);
        }
    }

    return candidates;
}

// Specialization for vector<uint8_t>
template<>
inline std::vector<std::vector<uint8_t>> Shrinker<std::vector<uint8_t>>::generate_shrink_candidates(
        const std::vector<uint8_t>& input) {
    std::vector<std::vector<uint8_t>> candidates;

    if (input.empty()) return candidates;

    // Try removing bytes
    for (size_t i = 0; i < input.size(); ++i) {
        auto v = input;
        v.erase(v.begin() + i);
        candidates.push_back(v);
    }

    // Try removing chunks
    for (size_t len = input.size() / 2; len >= 1; len /= 2) {
        for (size_t i = 0; i + len <= input.size(); ++i) {
            auto v = input;
            v.erase(v.begin() + i, v.begin() + i + len);
            candidates.push_back(v);
        }
    }

    // Try zeroing bytes
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != 0) {
            auto v = input;
            v[i] = 0;
            candidates.push_back(v);
        }
    }

    return candidates;
}

//=============================================================================
// Fuzz Test Result
//=============================================================================

/**
 * @brief Result of a fuzz test
 */
template<typename T>
struct FuzzResult {
    bool success{true};
    size_t iterations{0};
    std::chrono::milliseconds duration{0};

    bool has_failure{false};
    T failing_input;
    std::string failure_message;

    std::vector<std::string> warnings;
};

//=============================================================================
// Fuzz Test
//=============================================================================

/**
 * @brief Property-based fuzz testing framework
 */
template<typename T>
class FuzzTest {
public:
    using Generator = std::function<T()>;
    using TestFunc = std::function<void(const T&)>;
    using Property = std::function<bool(const T&)>;

    explicit FuzzTest(uint64_t seed = 0)
        : rng_(seed)
        , mutator_(seed) {}

    /**
     * @brief Set input generator
     */
    void generate(Generator gen) {
        generator_ = std::move(gen);
    }

    /**
     * @brief Set test function (should not throw on valid input)
     */
    void test(TestFunc func) {
        test_func_ = std::move(func);
    }

    /**
     * @brief Add property that must hold
     */
    void property(const std::string& name, Property prop) {
        properties_.emplace_back(name, std::move(prop));
    }

    /**
     * @brief Add to corpus
     */
    void add_corpus(const T& input) {
        corpus_.push_back(input);
    }

    /**
     * @brief Run fuzz test
     */
    FuzzResult<T> run(size_t iterations = 10000) {
        FuzzResult<T> result;
        auto start = std::chrono::steady_clock::now();

        for (size_t i = 0; i < iterations; ++i) {
            T input = generate_input(i);

            try {
                // Run test function
                if (test_func_) {
                    test_func_(input);
                }

                // Check properties
                for (const auto& [name, prop] : properties_) {
                    if (!prop(input)) {
                        result.success = false;
                        result.has_failure = true;
                        result.failing_input = input;
                        result.failure_message = "Property '" + name + "' violated";
                        break;
                    }
                }
            } catch (const std::exception& e) {
                result.success = false;
                result.has_failure = true;
                result.failing_input = input;
                result.failure_message = std::string("Exception: ") + e.what();
            } catch (...) {
                result.success = false;
                result.has_failure = true;
                result.failing_input = input;
                result.failure_message = "Unknown exception";
            }

            if (!result.success) {
                // Shrink the failing input
                if constexpr (std::is_same_v<T, std::string> ||
                              std::is_same_v<T, std::vector<uint8_t>>) {
                    Shrinker<T> shrinker;
                    auto test_fails = [this](const T& inp) {
                        try {
                            if (test_func_) test_func_(inp);
                            for (const auto& [_, prop] : properties_) {
                                if (!prop(inp)) return true;
                            }
                            return false;
                        } catch (...) {
                            return true;
                        }
                    };
                    result.failing_input = shrinker.shrink(result.failing_input, test_fails);
                }
                break;
            }

            ++result.iterations;
        }

        auto end = std::chrono::steady_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        return result;
    }

    /**
     * @brief Load corpus from directory
     */
    void load_corpus(const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir)) return;

        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::ifstream file(entry.path(), std::ios::binary);
                std::string content((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());

                if constexpr (std::is_same_v<T, std::string>) {
                    corpus_.push_back(content);
                } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                    corpus_.emplace_back(content.begin(), content.end());
                }
            }
        }
    }

    /**
     * @brief Save failing input to file
     */
    void save_failure(const T& input, const std::filesystem::path& path) {
        std::ofstream file(path, std::ios::binary);

        if constexpr (std::is_same_v<T, std::string>) {
            file << input;
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            file.write(reinterpret_cast<const char*>(input.data()), input.size());
        }
    }

private:
    T generate_input(size_t iteration) {
        // Use corpus for some iterations
        if (!corpus_.empty() && rng_.boolean(0.3)) {
            return mutator_.mutate_string(corpus_[rng_.integer<size_t>(0, corpus_.size() - 1)]);
        }

        // Use boundary values occasionally
        if constexpr (std::is_same_v<T, std::string>) {
            if (rng_.boolean(0.1)) {
                return rng_.pick(BoundaryGen::strings());
            }
        }

        // Generate using generator
        if (generator_) {
            return generator_();
        }

        // Default generation
        if constexpr (std::is_same_v<T, std::string>) {
            return rng_.string(0, 100);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            return rng_.bytes(0, 100);
        } else if constexpr (std::is_integral_v<T>) {
            return rng_.integer<T>();
        } else if constexpr (std::is_floating_point_v<T>) {
            return rng_.floating<T>();
        } else {
            static_assert(sizeof(T) == 0, "Unsupported type for default generation");
        }
    }

    RandomGen rng_;
    Mutator mutator_;
    Generator generator_;
    TestFunc test_func_;
    std::vector<std::pair<std::string, Property>> properties_;
    std::vector<T> corpus_;
};

//=============================================================================
// Quick Property Checks
//=============================================================================

/**
 * @brief Quick property-based checks
 */
class QuickCheck {
public:
    /**
     * @brief Check that a function is idempotent
     */
    template<typename T, typename Func>
    static bool is_idempotent(Func func, size_t iterations = 1000) {
        RandomGen rng;
        for (size_t i = 0; i < iterations; ++i) {
            T input = rng.integer<T>();
            T once = func(input);
            T twice = func(func(input));
            if (once != twice) return false;
        }
        return true;
    }

    /**
     * @brief Check roundtrip (encode/decode)
     */
    template<typename T, typename Encode, typename Decode>
    static bool roundtrip(Encode encode, Decode decode, size_t iterations = 1000) {
        RandomGen rng;
        for (size_t i = 0; i < iterations; ++i) {
            T input;
            if constexpr (std::is_same_v<T, std::string>) {
                input = rng.string(0, 100);
            } else if constexpr (std::is_integral_v<T>) {
                input = rng.integer<T>();
            }

            auto encoded = encode(input);
            auto decoded = decode(encoded);

            if (input != decoded) return false;
        }
        return true;
    }

    /**
     * @brief Check commutativity
     */
    template<typename T, typename Op>
    static bool is_commutative(Op op, size_t iterations = 1000) {
        RandomGen rng;
        for (size_t i = 0; i < iterations; ++i) {
            T a = rng.integer<T>();
            T b = rng.integer<T>();
            if (op(a, b) != op(b, a)) return false;
        }
        return true;
    }

    /**
     * @brief Check associativity
     */
    template<typename T, typename Op>
    static bool is_associative(Op op, size_t iterations = 1000) {
        RandomGen rng;
        for (size_t i = 0; i < iterations; ++i) {
            T a = rng.integer<T>();
            T b = rng.integer<T>();
            T c = rng.integer<T>();
            if (op(op(a, b), c) != op(a, op(b, c))) return false;
        }
        return true;
    }
};

} // namespace ipb::testing
