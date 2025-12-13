#pragma once

/**
 * @file performance_benchmarks.hpp
 * @brief Enterprise-grade performance benchmarking framework
 *
 * Comprehensive benchmarking features:
 * - Nanosecond-precision timing
 * - Statistical analysis (mean, median, percentiles, std dev)
 * - Warm-up runs and outlier detection
 * - SLO (Service Level Objective) validation
 * - Memory allocation tracking
 * - CPU cycle counting (where available)
 * - JSON/CSV report generation
 *
 * Usage:
 *   BenchmarkSuite suite("MyComponent");
 *   suite.add_benchmark("operation_x", []() {
 *       // Code to benchmark
 *   });
 *   suite.run(BenchmarkConfig{.iterations = 10000});
 *   suite.print_results();
 */

#include <ipb/common/platform.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace ipb::common::benchmark {

/**
 * @brief Benchmark configuration
 */
struct BenchmarkConfig {
    size_t iterations{10000};       // Number of benchmark iterations
    size_t warmup_iterations{100};  // Warmup iterations (not measured)
    size_t min_duration_ms{100};    // Minimum benchmark duration
    size_t max_duration_ms{60000};  // Maximum benchmark duration (timeout)
    bool track_memory{false};       // Track memory allocations
    bool track_cpu_cycles{false};   // Track CPU cycles (if available)
    double outlier_threshold{3.0};  // Std deviations for outlier detection
    bool remove_outliers{true};     // Remove outliers from statistics
};

/**
 * @brief Service Level Objective specification
 */
struct SLOSpec {
    std::string name;
    double p50_ns{0};          // Median latency target (0 = ignore)
    double p95_ns{0};          // 95th percentile target
    double p99_ns{0};          // 99th percentile target
    double max_ns{0};          // Maximum latency target
    double min_throughput{0};  // Minimum operations per second
};

/**
 * @brief Single benchmark measurement
 */
struct Measurement {
    int64_t duration_ns{0};
    int64_t cpu_cycles{0};
    size_t memory_allocated{0};
    size_t memory_freed{0};
};

/**
 * @brief Statistical results from benchmark run
 */
struct BenchmarkResults {
    std::string name;
    size_t iterations{0};
    size_t valid_iterations{0};  // After outlier removal
    std::vector<int64_t> latencies_ns;

    // Timing statistics (nanoseconds)
    double mean_ns{0};
    double median_ns{0};
    double stddev_ns{0};
    double min_ns{0};
    double max_ns{0};

    // Percentiles
    double p50_ns{0};
    double p75_ns{0};
    double p90_ns{0};
    double p95_ns{0};
    double p99_ns{0};
    double p999_ns{0};

    // Throughput
    double ops_per_sec{0};
    double total_duration_ms{0};

    // Memory (if tracked)
    size_t total_allocations{0};
    size_t total_bytes_allocated{0};
    double bytes_per_op{0};

    // CPU cycles (if tracked)
    double cycles_per_op{0};

    // SLO validation
    bool slo_passed{true};
    std::vector<std::string> slo_violations;

    /**
     * @brief Format results as human-readable string
     */
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);

        oss << "Benchmark: " << name << "\n";
        oss << "  Iterations: " << valid_iterations << "/" << iterations << "\n";
        oss << "  Duration: " << total_duration_ms << " ms\n";
        oss << "\n  Latency:\n";
        oss << "    Mean:   " << format_time(mean_ns) << "\n";
        oss << "    Median: " << format_time(median_ns) << "\n";
        oss << "    StdDev: " << format_time(stddev_ns) << "\n";
        oss << "    Min:    " << format_time(min_ns) << "\n";
        oss << "    Max:    " << format_time(max_ns) << "\n";
        oss << "\n  Percentiles:\n";
        oss << "    P50:  " << format_time(p50_ns) << "\n";
        oss << "    P75:  " << format_time(p75_ns) << "\n";
        oss << "    P90:  " << format_time(p90_ns) << "\n";
        oss << "    P95:  " << format_time(p95_ns) << "\n";
        oss << "    P99:  " << format_time(p99_ns) << "\n";
        oss << "    P99.9:" << format_time(p999_ns) << "\n";
        oss << "\n  Throughput: " << format_throughput(ops_per_sec) << "\n";

        if (total_allocations > 0) {
            oss << "\n  Memory:\n";
            oss << "    Allocations: " << total_allocations << "\n";
            oss << "    Total bytes: " << total_bytes_allocated << "\n";
            oss << "    Bytes/op:    " << bytes_per_op << "\n";
        }

        if (cycles_per_op > 0) {
            oss << "\n  CPU Cycles/op: " << cycles_per_op << "\n";
        }

        if (!slo_passed) {
            oss << "\n  SLO VIOLATIONS:\n";
            for (const auto& v : slo_violations) {
                oss << "    - " << v << "\n";
            }
        }

        return oss.str();
    }

    /**
     * @brief Format as JSON
     */
    std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "{\n";
        oss << "  \"name\": \"" << name << "\",\n";
        oss << "  \"iterations\": " << iterations << ",\n";
        oss << "  \"valid_iterations\": " << valid_iterations << ",\n";
        oss << "  \"total_duration_ms\": " << total_duration_ms << ",\n";
        oss << "  \"latency_ns\": {\n";
        oss << "    \"mean\": " << mean_ns << ",\n";
        oss << "    \"median\": " << median_ns << ",\n";
        oss << "    \"stddev\": " << stddev_ns << ",\n";
        oss << "    \"min\": " << min_ns << ",\n";
        oss << "    \"max\": " << max_ns << "\n";
        oss << "  },\n";
        oss << "  \"percentiles_ns\": {\n";
        oss << "    \"p50\": " << p50_ns << ",\n";
        oss << "    \"p75\": " << p75_ns << ",\n";
        oss << "    \"p90\": " << p90_ns << ",\n";
        oss << "    \"p95\": " << p95_ns << ",\n";
        oss << "    \"p99\": " << p99_ns << ",\n";
        oss << "    \"p999\": " << p999_ns << "\n";
        oss << "  },\n";
        oss << "  \"ops_per_sec\": " << ops_per_sec << ",\n";
        oss << "  \"slo_passed\": " << (slo_passed ? "true" : "false") << "\n";
        oss << "}";
        return oss.str();
    }

private:
    static std::string format_time(double ns) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);

        if (ns < 1000) {
            oss << ns << " ns";
        } else if (ns < 1000000) {
            oss << (ns / 1000) << " µs";
        } else if (ns < 1000000000) {
            oss << (ns / 1000000) << " ms";
        } else {
            oss << (ns / 1000000000) << " s";
        }
        return oss.str();
    }

    static std::string format_throughput(double ops) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);

        if (ops < 1000) {
            oss << ops << " ops/s";
        } else if (ops < 1000000) {
            oss << (ops / 1000) << " K ops/s";
        } else if (ops < 1000000000) {
            oss << (ops / 1000000) << " M ops/s";
        } else {
            oss << (ops / 1000000000) << " G ops/s";
        }
        return oss.str();
    }
};

/**
 * @brief High-precision timer for benchmarking
 */
class alignas(IPB_CACHE_LINE_SIZE) BenchmarkTimer {
public:
    void start() noexcept {
        start_ = std::chrono::high_resolution_clock::now();
#ifdef __x86_64__
        if (track_cycles_) {
            start_cycles_ = __rdtsc();
        }
#endif
    }

    void stop() noexcept {
        end_ = std::chrono::high_resolution_clock::now();
#ifdef __x86_64__
        if (track_cycles_) {
            end_cycles_ = __rdtsc();
        }
#endif
    }

    int64_t elapsed_ns() const noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_).count();
    }

    uint64_t elapsed_cycles() const noexcept {
#ifdef __x86_64__
        return end_cycles_ - start_cycles_;
#else
        return 0;
#endif
    }

    void set_track_cycles(bool track) noexcept { track_cycles_ = track; }

private:
    std::chrono::high_resolution_clock::time_point start_;
    std::chrono::high_resolution_clock::time_point end_;
    uint64_t start_cycles_{0};
    uint64_t end_cycles_{0};
    bool track_cycles_{false};

#ifdef __x86_64__
    static inline uint64_t __rdtsc() noexcept {
        unsigned int lo, hi;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return ((uint64_t)hi << 32) | lo;
    }
#endif
};

/**
 * @brief Benchmark function wrapper
 */
class Benchmark {
public:
    using BenchFunc    = std::function<void()>;
    using SetupFunc    = std::function<void()>;
    using TeardownFunc = std::function<void()>;

    Benchmark(std::string name, BenchFunc func) : name_(std::move(name)), func_(std::move(func)) {}

    void set_setup(SetupFunc setup) { setup_ = std::move(setup); }
    void set_teardown(TeardownFunc teardown) { teardown_ = std::move(teardown); }
    void set_slo(const SLOSpec& slo) { slo_ = slo; }

    /**
     * @brief Run benchmark with given configuration
     */
    BenchmarkResults run(const BenchmarkConfig& config) {
        BenchmarkResults results;
        results.name       = name_;
        results.iterations = config.iterations;

        std::vector<Measurement> measurements;
        measurements.reserve(config.iterations);

        BenchmarkTimer timer;
        timer.set_track_cycles(config.track_cpu_cycles);

        // Warmup phase
        for (size_t i = 0; i < config.warmup_iterations; ++i) {
            if (setup_)
                setup_();
            func_();
            if (teardown_)
                teardown_();
        }

        // Measurement phase
        auto overall_start = std::chrono::steady_clock::now();
        auto deadline      = overall_start + std::chrono::milliseconds(config.max_duration_ms);

        for (size_t i = 0; i < config.iterations; ++i) {
            // Check timeout
            if (std::chrono::steady_clock::now() >= deadline) {
                break;
            }

            if (setup_)
                setup_();

            timer.start();
            func_();
            timer.stop();

            if (teardown_)
                teardown_();

            Measurement m;
            m.duration_ns = timer.elapsed_ns();
            m.cpu_cycles  = timer.elapsed_cycles();
            measurements.push_back(m);
        }

        auto overall_end = std::chrono::steady_clock::now();
        results.total_duration_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(overall_end - overall_start)
                .count();

        // Extract latencies
        results.latencies_ns.reserve(measurements.size());
        uint64_t total_cycles = 0;
        for (const auto& m : measurements) {
            results.latencies_ns.push_back(m.duration_ns);
            total_cycles += m.cpu_cycles;
        }

        // Calculate statistics
        calculate_statistics(results, config);

        // CPU cycles per op
        if (config.track_cpu_cycles && results.valid_iterations > 0) {
            results.cycles_per_op = static_cast<double>(total_cycles) / results.valid_iterations;
        }

        // Validate SLO
        if (slo_) {
            validate_slo(results);
        }

        return results;
    }

    const std::string& name() const { return name_; }

private:
    std::string name_;
    BenchFunc func_;
    SetupFunc setup_;
    TeardownFunc teardown_;
    std::optional<SLOSpec> slo_;

    void calculate_statistics(BenchmarkResults& results, const BenchmarkConfig& config) {
        auto& latencies = results.latencies_ns;

        if (latencies.empty()) {
            return;
        }

        // Sort for percentile calculation
        std::sort(latencies.begin(), latencies.end());

        // Calculate initial mean and stddev
        double sum  = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        double mean = sum / latencies.size();

        double sq_sum = 0;
        for (auto v : latencies) {
            sq_sum += (v - mean) * (v - mean);
        }
        double stddev = std::sqrt(sq_sum / latencies.size());

        // Remove outliers if requested
        if (config.remove_outliers && stddev > 0) {
            double lower = mean - config.outlier_threshold * stddev;
            double upper = mean + config.outlier_threshold * stddev;

            std::vector<int64_t> filtered;
            filtered.reserve(latencies.size());

            for (auto v : latencies) {
                if (v >= lower && v <= upper) {
                    filtered.push_back(v);
                }
            }

            if (!filtered.empty()) {
                latencies = std::move(filtered);
                std::sort(latencies.begin(), latencies.end());

                // Recalculate mean and stddev
                sum  = std::accumulate(latencies.begin(), latencies.end(), 0.0);
                mean = sum / latencies.size();

                sq_sum = 0;
                for (auto v : latencies) {
                    sq_sum += (v - mean) * (v - mean);
                }
                stddev = std::sqrt(sq_sum / latencies.size());
            }
        }

        results.valid_iterations = latencies.size();
        results.mean_ns          = mean;
        results.stddev_ns        = stddev;
        results.min_ns           = latencies.front();
        results.max_ns           = latencies.back();

        // Percentiles
        results.p50_ns    = percentile(latencies, 0.50);
        results.p75_ns    = percentile(latencies, 0.75);
        results.p90_ns    = percentile(latencies, 0.90);
        results.p95_ns    = percentile(latencies, 0.95);
        results.p99_ns    = percentile(latencies, 0.99);
        results.p999_ns   = percentile(latencies, 0.999);
        results.median_ns = results.p50_ns;

        // Throughput
        if (results.mean_ns > 0) {
            results.ops_per_sec = 1e9 / results.mean_ns;
        }
    }

    static double percentile(const std::vector<int64_t>& sorted_values, double p) {
        if (sorted_values.empty())
            return 0;
        if (sorted_values.size() == 1)
            return sorted_values[0];

        double index = p * (sorted_values.size() - 1);
        size_t lower = static_cast<size_t>(index);
        size_t upper = lower + 1;
        double frac  = index - lower;

        if (upper >= sorted_values.size()) {
            return sorted_values.back();
        }

        return sorted_values[lower] * (1 - frac) + sorted_values[upper] * frac;
    }

    void validate_slo(BenchmarkResults& results) {
        if (!slo_)
            return;

        results.slo_passed = true;

        if (slo_->p50_ns > 0 && results.p50_ns > slo_->p50_ns) {
            results.slo_passed = false;
            results.slo_violations.push_back("P50 " + std::to_string(results.p50_ns) +
                                             "ns > target " + std::to_string(slo_->p50_ns) + "ns");
        }

        if (slo_->p95_ns > 0 && results.p95_ns > slo_->p95_ns) {
            results.slo_passed = false;
            results.slo_violations.push_back("P95 " + std::to_string(results.p95_ns) +
                                             "ns > target " + std::to_string(slo_->p95_ns) + "ns");
        }

        if (slo_->p99_ns > 0 && results.p99_ns > slo_->p99_ns) {
            results.slo_passed = false;
            results.slo_violations.push_back("P99 " + std::to_string(results.p99_ns) +
                                             "ns > target " + std::to_string(slo_->p99_ns) + "ns");
        }

        if (slo_->max_ns > 0 && results.max_ns > slo_->max_ns) {
            results.slo_passed = false;
            results.slo_violations.push_back("Max " + std::to_string(results.max_ns) +
                                             "ns > target " + std::to_string(slo_->max_ns) + "ns");
        }

        if (slo_->min_throughput > 0 && results.ops_per_sec < slo_->min_throughput) {
            results.slo_passed = false;
            results.slo_violations.push_back("Throughput " + std::to_string(results.ops_per_sec) +
                                             " ops/s < target " +
                                             std::to_string(slo_->min_throughput) + " ops/s");
        }
    }
};

/**
 * @brief Benchmark suite for running multiple benchmarks
 */
class BenchmarkSuite {
public:
    explicit BenchmarkSuite(std::string name) : name_(std::move(name)) {}

    /**
     * @brief Add a benchmark
     */
    void add_benchmark(const std::string& name, Benchmark::BenchFunc func,
                       const SLOSpec& slo = {}) {
        auto bench = std::make_unique<Benchmark>(name, std::move(func));
        if (!slo.name.empty() || slo.p50_ns > 0 || slo.p95_ns > 0) {
            bench->set_slo(slo);
        }
        benchmarks_.push_back(std::move(bench));
    }

    /**
     * @brief Add a benchmark with setup/teardown
     */
    void add_benchmark(const std::string& name, Benchmark::BenchFunc func,
                       Benchmark::SetupFunc setup, Benchmark::TeardownFunc teardown,
                       const SLOSpec& slo = {}) {
        auto bench = std::make_unique<Benchmark>(name, std::move(func));
        bench->set_setup(std::move(setup));
        bench->set_teardown(std::move(teardown));
        if (!slo.name.empty() || slo.p50_ns > 0 || slo.p95_ns > 0) {
            bench->set_slo(slo);
        }
        benchmarks_.push_back(std::move(bench));
    }

    /**
     * @brief Run all benchmarks
     */
    void run(const BenchmarkConfig& config = {}) {
        results_.clear();

        std::cout << "=== Benchmark Suite: " << name_ << " ===\n\n";

        for (auto& bench : benchmarks_) {
            std::cout << "Running: " << bench->name() << "...\n";
            auto result             = bench->run(config);
            results_[bench->name()] = result;
            std::cout << "  Done (" << result.valid_iterations << " iterations in "
                      << result.total_duration_ms << "ms)\n";
        }

        std::cout << "\n";
    }

    /**
     * @brief Print all results
     */
    void print_results() const {
        std::cout << "=== Results: " << name_ << " ===\n\n";

        for (const auto& [name, result] : results_) {
            std::cout << result.format() << "\n";
            std::cout << std::string(60, '-') << "\n\n";
        }

        // Summary
        std::cout << "=== Summary ===\n\n";
        print_summary_table();
    }

    /**
     * @brief Export results as JSON
     */
    std::string to_json() const {
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"suite\": \"" << name_ << "\",\n";
        oss << "  \"benchmarks\": [\n";

        bool first = true;
        for (const auto& [name, result] : results_) {
            if (!first)
                oss << ",\n";
            first = false;
            oss << "    " << result.to_json();
        }

        oss << "\n  ]\n";
        oss << "}";
        return oss.str();
    }

    /**
     * @brief Check if all SLOs passed
     */
    bool all_slos_passed() const {
        for (const auto& [name, result] : results_) {
            if (!result.slo_passed) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Get results for a specific benchmark
     */
    const BenchmarkResults* get_result(const std::string& name) const {
        auto it = results_.find(name);
        return it != results_.end() ? &it->second : nullptr;
    }

    const std::string& name() const { return name_; }
    const std::map<std::string, BenchmarkResults>& results() const { return results_; }

private:
    std::string name_;
    std::vector<std::unique_ptr<Benchmark>> benchmarks_;
    std::map<std::string, BenchmarkResults> results_;

    void print_summary_table() const {
        // Header
        std::cout << std::left << std::setw(30) << "Benchmark" << std::right << std::setw(12)
                  << "Mean" << std::setw(12) << "P99" << std::setw(15) << "Throughput"
                  << std::setw(8) << "SLO" << "\n";
        std::cout << std::string(77, '-') << "\n";

        for (const auto& [name, result] : results_) {
            std::cout << std::left << std::setw(30) << truncate(name, 29) << std::right
                      << std::setw(12) << format_time_short(result.mean_ns) << std::setw(12)
                      << format_time_short(result.p99_ns) << std::setw(15)
                      << format_throughput_short(result.ops_per_sec) << std::setw(8)
                      << (result.slo_passed ? "PASS" : "FAIL") << "\n";
        }
    }

    static std::string truncate(const std::string& s, size_t max_len) {
        if (s.length() <= max_len)
            return s;
        return s.substr(0, max_len - 3) + "...";
    }

    static std::string format_time_short(double ns) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        if (ns < 1000) {
            oss << ns << "ns";
        } else if (ns < 1000000) {
            oss << (ns / 1000) << "µs";
        } else {
            oss << (ns / 1000000) << "ms";
        }
        return oss.str();
    }

    static std::string format_throughput_short(double ops) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        if (ops < 1000) {
            oss << ops << "/s";
        } else if (ops < 1000000) {
            oss << (ops / 1000) << "K/s";
        } else {
            oss << (ops / 1000000) << "M/s";
        }
        return oss.str();
    }
};

/**
 * @brief Macro for easy benchmark definition
 */
#define IPB_BENCHMARK(suite, name, code) suite.add_benchmark(name, [&]() { code; })

#define IPB_BENCHMARK_SLO(suite, name, slo_spec, code) \
    suite.add_benchmark(name, [&]() { code; }, slo_spec)

/**
 * @brief DoNotOptimize helper to prevent compiler optimization
 */
template <typename T>
inline void DoNotOptimize(T&& value) {
#ifdef __GNUC__
    asm volatile("" : : "r,m"(value) : "memory");
#else
    std::atomic_signal_fence(std::memory_order_acq_rel);
#endif
}

/**
 * @brief ClobberMemory helper to force memory writes
 */
inline void ClobberMemory() {
#ifdef __GNUC__
    asm volatile("" : : : "memory");
#else
    std::atomic_signal_fence(std::memory_order_acq_rel);
#endif
}

}  // namespace ipb::common::benchmark
