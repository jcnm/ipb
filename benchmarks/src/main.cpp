/**
 * @file main.cpp
 * @brief IPB Benchmark Suite - Main Entry Point
 *
 * Usage examples:
 *   ipb-benchmark                          # Run all benchmarks
 *   ipb-benchmark --category=core          # Run only core benchmarks
 *   ipb-benchmark --category=sinks         # Run only sink benchmarks
 *   ipb-benchmark --component=router       # Run router component only
 *   ipb-benchmark --component=mqtt         # Run MQTT benchmarks only
 *   ipb-benchmark --list                   # List all available benchmarks
 *   ipb-benchmark --save-baseline=v1.5.0   # Save as baseline
 *   ipb-benchmark --compare=v1.4.0         # Compare with baseline
 *   ipb-benchmark --report                 # Generate markdown report
 */

#include <ipb/benchmarks/benchmark_framework.hpp>

// Include all benchmark modules
#include "benchmarks_core.hpp"
#include "benchmarks_sinks.hpp"
#include "benchmarks_scoops.hpp"
#include "benchmarks_transports.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace ipb::benchmark;

//=============================================================================
// Helper Functions
//=============================================================================

std::string format_time_short(double ns) {
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

std::string format_throughput_short(double ops) {
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

//=============================================================================
// CLI Argument Parsing
//=============================================================================

struct CliArgs {
    BenchmarkCategory category{BenchmarkCategory::ALL};
    std::string component;
    std::string single_benchmark;
    std::string version{"dev"};
    std::string baseline_version;
    std::string output_dir{"./benchmarks/results"};
    bool list{false};
    bool verbose{false};
    bool json{false};
    bool report{false};
    bool save_baseline{false};
    bool help{false};
};

void print_help() {
    std::cout << R"(
IPB Benchmark Suite

USAGE:
    ipb-benchmark [OPTIONS]

OPTIONS:
    --help, -h                  Show this help message
    --list, -l                  List all available benchmarks
    --verbose, -v               Enable verbose output
    --json                      Output results in JSON format

SELECTION:
    --category=<cat>            Run benchmarks for category:
                                  core, sinks, scoops, transports, all
    --component=<name>          Run benchmarks for specific component
    --benchmark=<name>          Run single benchmark by full name

VERSIONING:
    --version=<ver>             Set version string (default: dev)
    --save-baseline=<ver>       Save results as baseline for version
    --compare=<ver>             Compare results with baseline version

OUTPUT:
    --output=<dir>              Output directory (default: ./benchmarks/results)
    --report                    Generate markdown report

EXAMPLES:
    # Run all benchmarks
    ipb-benchmark

    # Run only core component benchmarks
    ipb-benchmark --category=core --verbose

    # Run router benchmarks only
    ipb-benchmark --category=core --component=router

    # Run MQTT sink benchmarks
    ipb-benchmark --category=sinks --component=mqtt

    # Save results as baseline for v1.5.0
    ipb-benchmark --version=v1.5.0 --save-baseline=v1.5.0

    # Compare current with v1.4.0 baseline
    ipb-benchmark --version=v1.5.0 --compare=v1.4.0 --report

    # List all available benchmarks
    ipb-benchmark --list

)";
}

CliArgs parse_args(int argc, char** argv) {
    CliArgs args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            args.help = true;
        } else if (arg == "--list" || arg == "-l") {
            args.list = true;
        } else if (arg == "--verbose" || arg == "-v") {
            args.verbose = true;
        } else if (arg == "--json") {
            args.json = true;
        } else if (arg == "--report") {
            args.report = true;
        } else if (arg.starts_with("--category=")) {
            auto cat_str = arg.substr(11);
            auto cat = string_to_category(cat_str);
            if (cat) {
                args.category = *cat;
            } else {
                std::cerr << "Unknown category: " << cat_str << "\n";
            }
        } else if (arg.starts_with("--component=")) {
            args.component = arg.substr(12);
        } else if (arg.starts_with("--benchmark=")) {
            args.single_benchmark = arg.substr(12);
        } else if (arg.starts_with("--version=")) {
            args.version = arg.substr(10);
        } else if (arg.starts_with("--save-baseline=")) {
            args.baseline_version = arg.substr(16);
            args.save_baseline = true;
        } else if (arg.starts_with("--compare=")) {
            args.baseline_version = arg.substr(10);
        } else if (arg.starts_with("--output=")) {
            args.output_dir = arg.substr(9);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
        }
    }

    return args;
}

//=============================================================================
// Main
//=============================================================================

int main(int argc, char** argv) {
    CliArgs args = parse_args(argc, argv);

    if (args.help) {
        print_help();
        return 0;
    }

    // Initialize benchmark modules
    register_core_benchmarks();
    register_sink_benchmarks();
    register_scoop_benchmarks();
    register_transport_benchmarks();

    auto& registry = BenchmarkRegistry::instance();

    // List mode
    if (args.list) {
        std::cout << "Available Benchmarks (" << registry.count() << " total):\n\n";

        for (auto cat : {BenchmarkCategory::CORE, BenchmarkCategory::SINKS,
                         BenchmarkCategory::SCOOPS, BenchmarkCategory::TRANSPORTS}) {
            std::string cat_name = category_to_string(cat);
            auto components = registry.list_components(cat);

            if (!components.empty()) {
                std::cout << "[" << cat_name << "]\n";
                for (const auto& comp : components) {
                    auto benchmarks = registry.get_by_component(cat, comp);
                    std::cout << "  " << comp << " (" << benchmarks.size() << " benchmarks)\n";
                    for (auto* b : benchmarks) {
                        std::cout << "    - " << b->name << "\n";
                    }
                }
                std::cout << "\n";
            }
        }
        return 0;
    }

    // Configure runner
    RunConfig config;
    config.verbose = args.verbose;
    config.json_output = args.json;
    config.output_dir = args.output_dir;
    config.version = args.version;

    BenchmarkRunner runner(config);

    // Print header
    std::cout << "========================================\n";
    std::cout << "     IPB Benchmark Suite v" << args.version << "\n";
    std::cout << "========================================\n\n";

    // Run benchmarks
    std::vector<BenchmarkResult> results;

    if (!args.single_benchmark.empty()) {
        // Single benchmark mode
        auto result = runner.run_single(args.single_benchmark);
        if (result) {
            results.push_back(*result);
        } else {
            std::cerr << "Benchmark not found: " << args.single_benchmark << "\n";
            return 1;
        }
    } else if (!args.component.empty()) {
        // Component mode
        results = runner.run_component(args.category, args.component);
    } else {
        // Category mode
        results = runner.run_category(args.category);
    }

    if (results.empty()) {
        std::cout << "No benchmarks found matching criteria.\n";
        return 0;
    }

    // Print results summary
    std::cout << "\n========================================\n";
    std::cout << "           Results Summary\n";
    std::cout << "========================================\n\n";

    // Table header
    std::cout << std::left << std::setw(35) << "Benchmark"
              << std::right << std::setw(10) << "Mean"
              << std::setw(10) << "P99"
              << std::setw(12) << "Throughput"
              << std::setw(8) << "Status"
              << "\n";
    std::cout << std::string(75, '-') << "\n";

    int passed = 0, failed = 0;
    for (const auto& r : results) {
        std::string name = r.component + "/" + r.name;
        if (name.length() > 34) {
            name = name.substr(0, 31) + "...";
        }

        std::cout << std::left << std::setw(35) << name
                  << std::right << std::setw(10) << format_time_short(r.mean_ns)
                  << std::setw(10) << format_time_short(r.p99_ns)
                  << std::setw(12) << format_throughput_short(r.ops_per_sec)
                  << std::setw(8) << (r.slo_passed ? "PASS" : "FAIL")
                  << "\n";

        if (r.slo_passed) passed++;
        else failed++;
    }

    std::cout << std::string(75, '-') << "\n";
    std::cout << "Total: " << results.size() << " benchmarks, "
              << passed << " passed, " << failed << " failed\n\n";

    // Save results
    runner.save_results(results);

    // Save as baseline if requested
    if (args.save_baseline) {
        std::filesystem::path baseline_dir = args.output_dir + "/../baselines";
        std::filesystem::create_directories(baseline_dir);

        std::string baseline_file = "baseline_" + args.baseline_version + ".json";
        runner.save_results(results, baseline_file);
        std::cout << "Baseline saved: " << baseline_dir / baseline_file << "\n";
    }

    // Compare with baseline if requested
    if (!args.baseline_version.empty() && !args.save_baseline) {
        std::cout << "Loading baseline: " << args.baseline_version << "\n";
        auto baseline = runner.load_baseline(args.baseline_version);

        if (!baseline.empty()) {
            auto comparisons = runner.compare_with_baseline(results, baseline);
            runner.print_comparison_report(comparisons);

            // Generate report if requested
            if (args.report) {
                auto report = runner.generate_markdown_report(results, comparisons);

                std::filesystem::path report_dir = args.output_dir + "/../reports";
                std::filesystem::create_directories(report_dir);

                std::string report_file = "benchmark_report_" + args.version + ".md";
                std::ofstream file(report_dir / report_file);
                if (file) {
                    file << report;
                    std::cout << "Report saved: " << report_dir / report_file << "\n";
                }
            }

            // Check for regressions
            bool has_regression = false;
            for (const auto& c : comparisons) {
                if (c.is_regression()) {
                    has_regression = true;
                    break;
                }
            }

            if (has_regression) {
                std::cerr << "\n⚠️  PERFORMANCE REGRESSION DETECTED!\n";
                return 2;
            }
        } else {
            std::cerr << "Baseline not found: " << args.baseline_version << "\n";
        }
    }

    // Generate standalone report if requested
    if (args.report && args.baseline_version.empty()) {
        auto report = runner.generate_markdown_report(results);

        std::filesystem::path report_dir = args.output_dir + "/../reports";
        std::filesystem::create_directories(report_dir);

        std::string report_file = "benchmark_report_" + args.version + ".md";
        std::ofstream file(report_dir / report_file);
        if (file) {
            file << report;
            std::cout << "Report saved: " << report_dir / report_file << "\n";
        }
    }

    std::cout << "\n========================================\n";
    std::cout << "        Benchmarks Complete\n";
    std::cout << "========================================\n";

    return failed > 0 ? 1 : 0;
}
