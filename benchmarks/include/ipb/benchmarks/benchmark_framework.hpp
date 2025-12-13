#pragma once

/**
 * @file benchmark_framework.hpp
 * @brief Enterprise-grade benchmarking framework for IPB
 *
 * Comprehensive benchmarking infrastructure:
 * - Modular categories: core, sinks, scoops, transports
 * - Selective execution (run specific components)
 * - Historical result storage per release
 * - Comparison with baselines and competitors
 * - JSON/CSV export for CI/CD integration
 * - Statistical analysis with percentiles
 *
 * Usage:
 *   ipb-benchmark --category=core --component=router
 *   ipb-benchmark --category=sinks --component=mqtt
 *   ipb-benchmark --all --save-baseline=v1.5.0
 *   ipb-benchmark --compare-with=v1.4.0
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace ipb::benchmark {

//=============================================================================
// Enumerations and Constants
//=============================================================================

/**
 * @brief Benchmark categories
 */
enum class BenchmarkCategory {
    CORE,        // Core framework components
    SINKS,       // Output sinks (MQTT, HTTP, etc.)
    SCOOPS,      // Input sources (OPC-UA, Modbus, etc.)
    TRANSPORTS,  // Transport layers
    ALL          // Run all categories
};

/**
 * @brief Convert category to string
 */
inline std::string category_to_string(BenchmarkCategory cat) {
    switch (cat) {
        case BenchmarkCategory::CORE:
            return "core";
        case BenchmarkCategory::SINKS:
            return "sinks";
        case BenchmarkCategory::SCOOPS:
            return "scoops";
        case BenchmarkCategory::TRANSPORTS:
            return "transports";
        case BenchmarkCategory::ALL:
            return "all";
    }
    return "unknown";
}

/**
 * @brief Parse category from string
 */
inline std::optional<BenchmarkCategory> string_to_category(const std::string& str) {
    if (str == "core")
        return BenchmarkCategory::CORE;
    if (str == "sinks")
        return BenchmarkCategory::SINKS;
    if (str == "scoops")
        return BenchmarkCategory::SCOOPS;
    if (str == "transports")
        return BenchmarkCategory::TRANSPORTS;
    if (str == "all")
        return BenchmarkCategory::ALL;
    return std::nullopt;
}

//=============================================================================
// Result Structures
//=============================================================================

/**
 * @brief Single benchmark measurement result
 */
struct BenchmarkResult {
    std::string name;
    std::string category;
    std::string component;

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
    size_t iterations{0};
    double duration_ms{0};

    // Memory (if tracked)
    size_t memory_bytes{0};
    double bytes_per_op{0};

    // Metadata
    std::string timestamp;
    std::string version;
    std::string git_commit;
    std::string platform;
    std::string compiler;

    // SLO validation
    bool slo_passed{true};
    std::vector<std::string> slo_violations;

    /**
     * @brief Format as JSON
     */
    std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "    {\n";
        oss << "      \"name\": \"" << name << "\",\n";
        oss << "      \"category\": \"" << category << "\",\n";
        oss << "      \"component\": \"" << component << "\",\n";
        oss << "      \"iterations\": " << iterations << ",\n";
        oss << "      \"duration_ms\": " << duration_ms << ",\n";
        oss << "      \"timing_ns\": {\n";
        oss << "        \"mean\": " << mean_ns << ",\n";
        oss << "        \"median\": " << median_ns << ",\n";
        oss << "        \"stddev\": " << stddev_ns << ",\n";
        oss << "        \"min\": " << min_ns << ",\n";
        oss << "        \"max\": " << max_ns << "\n";
        oss << "      },\n";
        oss << "      \"percentiles_ns\": {\n";
        oss << "        \"p50\": " << p50_ns << ",\n";
        oss << "        \"p75\": " << p75_ns << ",\n";
        oss << "        \"p90\": " << p90_ns << ",\n";
        oss << "        \"p95\": " << p95_ns << ",\n";
        oss << "        \"p99\": " << p99_ns << ",\n";
        oss << "        \"p999\": " << p999_ns << "\n";
        oss << "      },\n";
        oss << "      \"throughput\": " << ops_per_sec << ",\n";
        oss << "      \"slo_passed\": " << (slo_passed ? "true" : "false") << "\n";
        oss << "    }";
        return oss.str();
    }

    /**
     * @brief Format as CSV row
     */
    std::string to_csv_row() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << category << "," << component << "," << name << ",";
        oss << iterations << "," << duration_ms << ",";
        oss << mean_ns << "," << median_ns << "," << stddev_ns << ",";
        oss << min_ns << "," << max_ns << ",";
        oss << p50_ns << "," << p95_ns << "," << p99_ns << ",";
        oss << ops_per_sec << "," << (slo_passed ? "PASS" : "FAIL");
        return oss.str();
    }

    static std::string csv_header() {
        return "category,component,name,iterations,duration_ms,"
               "mean_ns,median_ns,stddev_ns,min_ns,max_ns,"
               "p50_ns,p95_ns,p99_ns,ops_per_sec,slo_status";
    }
};

/**
 * @brief Comparison result between two benchmark runs
 */
struct ComparisonResult {
    std::string benchmark_name;
    double baseline_mean_ns;
    double current_mean_ns;
    double change_percent;
    double baseline_p99_ns;
    double current_p99_ns;
    double p99_change_percent;
    double baseline_ops;
    double current_ops;
    double ops_change_percent;

    bool is_regression() const {
        // Consider >5% slowdown as regression
        return change_percent > 5.0 || p99_change_percent > 10.0;
    }

    bool is_improvement() const { return change_percent < -5.0; }

    std::string status() const {
        if (is_regression())
            return "REGRESSION";
        if (is_improvement())
            return "IMPROVED";
        return "STABLE";
    }
};

//=============================================================================
// Benchmark Registration
//=============================================================================

/**
 * @brief Single benchmark function definition
 */
struct BenchmarkDef {
    std::string name;
    BenchmarkCategory category;
    std::string component;
    std::function<void()> setup;
    std::function<void()> benchmark;
    std::function<void()> teardown;
    size_t iterations{10000};
    size_t warmup{100};

    // SLO targets
    double target_p50_ns{0};
    double target_p99_ns{0};
    double target_ops{0};
};

/**
 * @brief Benchmark registry for component registration
 */
class BenchmarkRegistry {
public:
    static BenchmarkRegistry& instance() {
        static BenchmarkRegistry registry;
        return registry;
    }

    /**
     * @brief Register a benchmark
     */
    void register_benchmark(BenchmarkDef def) {
        std::string key  = category_to_string(def.category) + "/" + def.component + "/" + def.name;
        benchmarks_[key] = std::move(def);

        // Track components per category
        components_[def.category].insert(def.component);
    }

    /**
     * @brief Get all benchmarks for a category
     */
    std::vector<BenchmarkDef*> get_by_category(BenchmarkCategory cat) {
        std::vector<BenchmarkDef*> result;
        std::string prefix = category_to_string(cat) + "/";

        for (auto& [key, def] : benchmarks_) {
            if (cat == BenchmarkCategory::ALL || key.starts_with(prefix)) {
                result.push_back(&def);
            }
        }
        return result;
    }

    /**
     * @brief Get benchmarks for specific component
     */
    std::vector<BenchmarkDef*> get_by_component(BenchmarkCategory cat,
                                                const std::string& component) {
        std::vector<BenchmarkDef*> result;
        std::string prefix = category_to_string(cat) + "/" + component + "/";

        for (auto& [key, def] : benchmarks_) {
            if (key.starts_with(prefix)) {
                result.push_back(&def);
            }
        }
        return result;
    }

    /**
     * @brief Get single benchmark by full name
     */
    BenchmarkDef* get_by_name(const std::string& full_name) {
        auto it = benchmarks_.find(full_name);
        return it != benchmarks_.end() ? &it->second : nullptr;
    }

    /**
     * @brief List all components for a category
     */
    std::set<std::string> list_components(BenchmarkCategory cat) const {
        auto it = components_.find(cat);
        return it != components_.end() ? it->second : std::set<std::string>{};
    }

    /**
     * @brief List all registered benchmarks
     */
    std::vector<std::string> list_all() const {
        std::vector<std::string> result;
        for (const auto& [key, _] : benchmarks_) {
            result.push_back(key);
        }
        return result;
    }

    size_t count() const { return benchmarks_.size(); }

private:
    BenchmarkRegistry() = default;
    std::map<std::string, BenchmarkDef> benchmarks_;
    std::map<BenchmarkCategory, std::set<std::string>> components_;
};

//=============================================================================
// Benchmark Runner
//=============================================================================

/**
 * @brief Configuration for benchmark execution
 */
struct RunConfig {
    size_t default_iterations{10000};
    size_t default_warmup{100};
    size_t min_duration_ms{100};
    size_t max_duration_ms{30000};
    double outlier_threshold{3.0};
    bool remove_outliers{true};
    bool verbose{false};
    bool json_output{false};
    std::string output_dir{"./benchmarks/results"};
    std::string version;
    std::string baseline_version;
};

/**
 * @brief Main benchmark runner
 */
class BenchmarkRunner {
public:
    explicit BenchmarkRunner(RunConfig config = {}) : config_(std::move(config)) {
        // Get system info
        platform_  = get_platform_info();
        compiler_  = get_compiler_info();
        timestamp_ = get_timestamp();

        if (config_.version.empty()) {
            config_.version = "dev";
        }
    }

    /**
     * @brief Run benchmarks by category
     */
    std::vector<BenchmarkResult> run_category(BenchmarkCategory cat) {
        auto benchmarks = BenchmarkRegistry::instance().get_by_category(cat);
        return run_benchmarks(benchmarks);
    }

    /**
     * @brief Run benchmarks for specific component
     */
    std::vector<BenchmarkResult> run_component(BenchmarkCategory cat,
                                               const std::string& component) {
        auto benchmarks = BenchmarkRegistry::instance().get_by_component(cat, component);
        return run_benchmarks(benchmarks);
    }

    /**
     * @brief Run all benchmarks
     */
    std::vector<BenchmarkResult> run_all() { return run_category(BenchmarkCategory::ALL); }

    /**
     * @brief Run specific benchmark by name
     */
    std::optional<BenchmarkResult> run_single(const std::string& name) {
        auto* def = BenchmarkRegistry::instance().get_by_name(name);
        if (!def)
            return std::nullopt;
        return run_single_benchmark(*def);
    }

    /**
     * @brief Save results to file
     */
    void save_results(const std::vector<BenchmarkResult>& results,
                      const std::string& filename = "") {
        std::filesystem::path dir(config_.output_dir);
        std::filesystem::create_directories(dir);

        std::string fname = filename.empty()
                              ? "benchmark_" + config_.version + "_" + timestamp_ + ".json"
                              : filename;

        std::filesystem::path path = dir / fname;

        std::ofstream file(path);
        if (!file) {
            std::cerr << "Failed to open " << path << " for writing\n";
            return;
        }

        file << to_json(results);

        if (config_.verbose) {
            std::cout << "Results saved to: " << path << "\n";
        }

        // Also save CSV
        std::filesystem::path csv_path = dir / (fname.substr(0, fname.rfind('.')) + ".csv");
        std::ofstream csv_file(csv_path);
        if (csv_file) {
            csv_file << BenchmarkResult::csv_header() << "\n";
            for (const auto& r : results) {
                csv_file << r.to_csv_row() << "\n";
            }
        }
    }

    /**
     * @brief Load baseline results
     */
    std::vector<BenchmarkResult> load_baseline(const std::string& version) {
        std::filesystem::path dir(config_.output_dir + "/../baselines");
        std::filesystem::path path = dir / ("baseline_" + version + ".json");
        return load_results(path);
    }

    /**
     * @brief Compare current results with baseline
     */
    std::vector<ComparisonResult> compare_with_baseline(
        const std::vector<BenchmarkResult>& current, const std::vector<BenchmarkResult>& baseline) {
        std::vector<ComparisonResult> comparisons;

        // Index baseline by name
        std::map<std::string, const BenchmarkResult*> baseline_map;
        for (const auto& b : baseline) {
            baseline_map[b.category + "/" + b.component + "/" + b.name] = &b;
        }

        for (const auto& c : current) {
            std::string key = c.category + "/" + c.component + "/" + c.name;
            auto it         = baseline_map.find(key);

            if (it != baseline_map.end()) {
                const auto& b = *it->second;
                ComparisonResult comp;
                comp.benchmark_name     = key;
                comp.baseline_mean_ns   = b.mean_ns;
                comp.current_mean_ns    = c.mean_ns;
                comp.change_percent     = calc_change_percent(b.mean_ns, c.mean_ns);
                comp.baseline_p99_ns    = b.p99_ns;
                comp.current_p99_ns     = c.p99_ns;
                comp.p99_change_percent = calc_change_percent(b.p99_ns, c.p99_ns);
                comp.baseline_ops       = b.ops_per_sec;
                comp.current_ops        = c.ops_per_sec;
                comp.ops_change_percent = calc_change_percent(b.ops_per_sec, c.ops_per_sec);
                comparisons.push_back(comp);
            }
        }

        return comparisons;
    }

    /**
     * @brief Print comparison report
     */
    void print_comparison_report(const std::vector<ComparisonResult>& comparisons) {
        std::cout << "\n========== Benchmark Comparison Report ==========\n\n";

        // Summary counts
        int regressions = 0, improvements = 0, stable = 0;
        for (const auto& c : comparisons) {
            if (c.is_regression())
                regressions++;
            else if (c.is_improvement())
                improvements++;
            else
                stable++;
        }

        std::cout << "Summary: " << comparisons.size() << " benchmarks compared\n";
        std::cout << "  Regressions:  " << regressions << "\n";
        std::cout << "  Improvements: " << improvements << "\n";
        std::cout << "  Stable:       " << stable << "\n\n";

        // Detailed table
        std::cout << std::left << std::setw(40) << "Benchmark" << std::right << std::setw(12)
                  << "Baseline" << std::setw(12) << "Current" << std::setw(10) << "Change"
                  << std::setw(10) << "Status" << "\n";
        std::cout << std::string(84, '-') << "\n";

        for (const auto& c : comparisons) {
            std::cout << std::left << std::setw(40) << truncate(c.benchmark_name, 39) << std::right
                      << std::setw(12) << format_time(c.baseline_mean_ns) << std::setw(12)
                      << format_time(c.current_mean_ns) << std::setw(9) << std::fixed
                      << std::setprecision(1) << c.change_percent << "%" << std::setw(10)
                      << c.status() << "\n";
        }

        std::cout << "\n";

        // List regressions in detail
        if (regressions > 0) {
            std::cout << "=== REGRESSIONS (require attention) ===\n";
            for (const auto& c : comparisons) {
                if (c.is_regression()) {
                    std::cout << "  " << c.benchmark_name << "\n";
                    std::cout << "    Mean: " << format_time(c.baseline_mean_ns) << " -> "
                              << format_time(c.current_mean_ns) << " (+" << std::fixed
                              << std::setprecision(1) << c.change_percent << "%)\n";
                    std::cout << "    P99:  " << format_time(c.baseline_p99_ns) << " -> "
                              << format_time(c.current_p99_ns) << " (+" << c.p99_change_percent
                              << "%)\n";
                }
            }
            std::cout << "\n";
        }
    }

    /**
     * @brief Generate markdown report
     */
    std::string generate_markdown_report(const std::vector<BenchmarkResult>& results,
                                         const std::vector<ComparisonResult>& comparisons = {}) {
        std::ostringstream md;

        md << "# IPB Benchmark Report\n\n";
        md << "**Version:** " << config_.version << "  \n";
        md << "**Date:** " << timestamp_ << "  \n";
        md << "**Platform:** " << platform_ << "  \n";
        md << "**Compiler:** " << compiler_ << "  \n\n";

        // Summary table
        md << "## Summary\n\n";
        md << "| Category | Benchmarks | Passed | Failed | Avg Throughput |\n";
        md << "|----------|------------|--------|--------|----------------|\n";

        std::map<std::string, std::vector<const BenchmarkResult*>> by_category;
        for (const auto& r : results) {
            by_category[r.category].push_back(&r);
        }

        for (const auto& [cat, cat_results] : by_category) {
            int passed = 0, failed = 0;
            double total_ops = 0;
            for (const auto* r : cat_results) {
                if (r->slo_passed)
                    passed++;
                else
                    failed++;
                total_ops += r->ops_per_sec;
            }
            double avg_ops = cat_results.empty() ? 0 : total_ops / cat_results.size();

            md << "| " << cat << " | " << cat_results.size() << " | " << passed << " | " << failed
               << " | " << format_throughput(avg_ops) << " |\n";
        }

        md << "\n";

        // Detailed results by category
        md << "## Detailed Results\n\n";

        for (const auto& [cat, cat_results] : by_category) {
            md << "### " << cat << "\n\n";
            md << "| Benchmark | Mean | P99 | Throughput | Status |\n";
            md << "|-----------|------|-----|------------|--------|\n";

            for (const auto* r : cat_results) {
                md << "| " << r->component << "/" << r->name << " | " << format_time(r->mean_ns)
                   << " | " << format_time(r->p99_ns) << " | " << format_throughput(r->ops_per_sec)
                   << " | " << (r->slo_passed ? "✓" : "✗") << " |\n";
            }
            md << "\n";
        }

        // Comparison section if available
        if (!comparisons.empty()) {
            md << "## Comparison with Baseline\n\n";
            md << "| Benchmark | Baseline | Current | Change | Status |\n";
            md << "|-----------|----------|---------|--------|--------|\n";

            for (const auto& c : comparisons) {
                std::string status_emoji =
                    c.is_regression() ? "⚠️" : (c.is_improvement() ? "✨" : "➖");
                md << "| " << c.benchmark_name << " | " << format_time(c.baseline_mean_ns) << " | "
                   << format_time(c.current_mean_ns) << " | " << std::fixed << std::setprecision(1)
                   << c.change_percent << "%" << " | " << status_emoji << " |\n";
            }
            md << "\n";
        }

        return md.str();
    }

    const RunConfig& config() const { return config_; }

private:
    RunConfig config_;
    std::string platform_;
    std::string compiler_;
    std::string timestamp_;

    std::vector<BenchmarkResult> run_benchmarks(std::vector<BenchmarkDef*>& benchmarks) {
        std::vector<BenchmarkResult> results;
        results.reserve(benchmarks.size());

        size_t total   = benchmarks.size();
        size_t current = 0;

        for (auto* def : benchmarks) {
            ++current;
            if (config_.verbose) {
                std::cout << "[" << current << "/" << total << "] Running: " << def->component
                          << "/" << def->name << "... ";
                std::cout.flush();
            }

            auto result = run_single_benchmark(*def);
            results.push_back(result);

            if (config_.verbose) {
                std::cout << "done (" << format_time(result.mean_ns) << " mean, "
                          << format_throughput(result.ops_per_sec) << ")\n";
            }
        }

        return results;
    }

    BenchmarkResult run_single_benchmark(const BenchmarkDef& def) {
        BenchmarkResult result;
        result.name      = def.name;
        result.category  = category_to_string(def.category);
        result.component = def.component;
        result.timestamp = timestamp_;
        result.version   = config_.version;
        result.platform  = platform_;
        result.compiler  = compiler_;

        size_t iterations = def.iterations > 0 ? def.iterations : config_.default_iterations;
        size_t warmup     = def.warmup > 0 ? def.warmup : config_.default_warmup;

        // Warmup
        for (size_t i = 0; i < warmup; ++i) {
            if (def.setup)
                def.setup();
            def.benchmark();
            if (def.teardown)
                def.teardown();
        }

        // Collect measurements
        std::vector<int64_t> latencies;
        latencies.reserve(iterations);

        auto overall_start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < iterations; ++i) {
            if (def.setup)
                def.setup();

            auto start = std::chrono::high_resolution_clock::now();
            def.benchmark();
            auto end = std::chrono::high_resolution_clock::now();

            if (def.teardown)
                def.teardown();

            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            latencies.push_back(ns);
        }

        auto overall_end = std::chrono::high_resolution_clock::now();
        result.duration_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(overall_end - overall_start)
                .count();

        // Calculate statistics
        calculate_statistics(result, latencies);

        // Validate SLO
        result.slo_passed = true;
        if (def.target_p50_ns > 0 && result.p50_ns > def.target_p50_ns) {
            result.slo_passed = false;
            result.slo_violations.push_back("P50 exceeded target");
        }
        if (def.target_p99_ns > 0 && result.p99_ns > def.target_p99_ns) {
            result.slo_passed = false;
            result.slo_violations.push_back("P99 exceeded target");
        }
        if (def.target_ops > 0 && result.ops_per_sec < def.target_ops) {
            result.slo_passed = false;
            result.slo_violations.push_back("Throughput below target");
        }

        return result;
    }

    void calculate_statistics(BenchmarkResult& result, std::vector<int64_t>& latencies) {
        if (latencies.empty())
            return;

        std::sort(latencies.begin(), latencies.end());

        // Remove outliers if configured
        if (config_.remove_outliers && latencies.size() > 10) {
            double sum  = std::accumulate(latencies.begin(), latencies.end(), 0.0);
            double mean = sum / latencies.size();

            double sq_sum = 0;
            for (auto v : latencies) {
                sq_sum += (v - mean) * (v - mean);
            }
            double stddev = std::sqrt(sq_sum / latencies.size());

            double lower = mean - config_.outlier_threshold * stddev;
            double upper = mean + config_.outlier_threshold * stddev;

            std::vector<int64_t> filtered;
            for (auto v : latencies) {
                if (v >= lower && v <= upper) {
                    filtered.push_back(v);
                }
            }

            if (!filtered.empty()) {
                latencies = std::move(filtered);
                std::sort(latencies.begin(), latencies.end());
            }
        }

        result.iterations = latencies.size();

        // Mean and stddev
        double sum     = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        result.mean_ns = sum / latencies.size();

        double sq_sum = 0;
        for (auto v : latencies) {
            sq_sum += (v - result.mean_ns) * (v - result.mean_ns);
        }
        result.stddev_ns = std::sqrt(sq_sum / latencies.size());

        // Min/max
        result.min_ns = latencies.front();
        result.max_ns = latencies.back();

        // Percentiles
        result.p50_ns    = percentile(latencies, 0.50);
        result.p75_ns    = percentile(latencies, 0.75);
        result.p90_ns    = percentile(latencies, 0.90);
        result.p95_ns    = percentile(latencies, 0.95);
        result.p99_ns    = percentile(latencies, 0.99);
        result.p999_ns   = percentile(latencies, 0.999);
        result.median_ns = result.p50_ns;

        // Throughput
        if (result.mean_ns > 0) {
            result.ops_per_sec = 1e9 / result.mean_ns;
        }
    }

    static double percentile(const std::vector<int64_t>& sorted, double p) {
        if (sorted.empty())
            return 0;
        double idx   = p * (sorted.size() - 1);
        size_t lower = static_cast<size_t>(idx);
        size_t upper = lower + 1;
        double frac  = idx - lower;

        if (upper >= sorted.size())
            return sorted.back();
        return sorted[lower] * (1 - frac) + sorted[upper] * frac;
    }

    static double calc_change_percent(double baseline, double current) {
        if (baseline == 0)
            return 0;
        return ((current - baseline) / baseline) * 100.0;
    }

    std::string to_json(const std::vector<BenchmarkResult>& results) {
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"metadata\": {\n";
        oss << "    \"version\": \"" << config_.version << "\",\n";
        oss << "    \"timestamp\": \"" << timestamp_ << "\",\n";
        oss << "    \"platform\": \"" << platform_ << "\",\n";
        oss << "    \"compiler\": \"" << compiler_ << "\"\n";
        oss << "  },\n";
        oss << "  \"results\": [\n";

        for (size_t i = 0; i < results.size(); ++i) {
            oss << results[i].to_json();
            if (i < results.size() - 1)
                oss << ",";
            oss << "\n";
        }

        oss << "  ]\n";
        oss << "}\n";
        return oss.str();
    }

    std::vector<BenchmarkResult> load_results(const std::filesystem::path& path) {
        // Simplified JSON parsing - in production use a proper JSON library
        std::vector<BenchmarkResult> results;
        // TODO: Implement JSON parsing
        return results;
    }

    static std::string get_timestamp() {
        auto now  = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
        return ss.str();
    }

    static std::string get_platform_info() {
#ifdef __linux__
        return "Linux";
#elif defined(_WIN32)
        return "Windows";
#elif defined(__APPLE__)
        return "macOS";
#else
        return "Unknown";
#endif
    }

    static std::string get_compiler_info() {
#ifdef __GNUC__
        return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#elif defined(__clang__)
        return "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(_MSC_VER)
        return "MSVC " + std::to_string(_MSC_VER);
#else
        return "Unknown";
#endif
    }

    static std::string format_time(double ns) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        if (ns < 1000) {
            oss << ns << "ns";
        } else if (ns < 1000000) {
            oss << (ns / 1000) << "µs";
        } else if (ns < 1000000000) {
            oss << (ns / 1000000) << "ms";
        } else {
            oss << (ns / 1000000000) << "s";
        }
        return oss.str();
    }

    static std::string format_throughput(double ops) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        if (ops < 1000) {
            oss << ops << "/s";
        } else if (ops < 1000000) {
            oss << (ops / 1000) << "K/s";
        } else if (ops < 1000000000) {
            oss << (ops / 1000000) << "M/s";
        } else {
            oss << (ops / 1000000000) << "G/s";
        }
        return oss.str();
    }

    static std::string truncate(const std::string& s, size_t max_len) {
        if (s.length() <= max_len)
            return s;
        return s.substr(0, max_len - 3) + "...";
    }
};

//=============================================================================
// Registration Macros
//=============================================================================

/**
 * @brief Helper for benchmark registration
 */
#define IPB_REGISTER_BENCHMARK(category, component, name, func)                               \
    namespace {                                                                               \
    struct BenchmarkRegistrar_##category##_##component##_##name {                             \
        BenchmarkRegistrar_##category##_##component##_##name() {                              \
            ipb::benchmark::BenchmarkDef def;                                                 \
            def.name      = #name;                                                            \
            def.category  = ipb::benchmark::BenchmarkCategory::category;                      \
            def.component = #component;                                                       \
            def.benchmark = func;                                                             \
            ipb::benchmark::BenchmarkRegistry::instance().register_benchmark(std::move(def)); \
        }                                                                                     \
    };                                                                                        \
    static BenchmarkRegistrar_##category##_##component##_##name                               \
        registrar_##category##_##component##_##name;                                          \
    }

#define IPB_REGISTER_BENCHMARK_WITH_SLO(category, component, name, func, p50, p99, ops)       \
    namespace {                                                                               \
    struct BenchmarkRegistrar_##category##_##component##_##name {                             \
        BenchmarkRegistrar_##category##_##component##_##name() {                              \
            ipb::benchmark::BenchmarkDef def;                                                 \
            def.name          = #name;                                                        \
            def.category      = ipb::benchmark::BenchmarkCategory::category;                  \
            def.component     = #component;                                                   \
            def.benchmark     = func;                                                         \
            def.target_p50_ns = p50;                                                          \
            def.target_p99_ns = p99;                                                          \
            def.target_ops    = ops;                                                          \
            ipb::benchmark::BenchmarkRegistry::instance().register_benchmark(std::move(def)); \
        }                                                                                     \
    };                                                                                        \
    static BenchmarkRegistrar_##category##_##component##_##name                               \
        registrar_##category##_##component##_##name;                                          \
    }

}  // namespace ipb::benchmark
