/**
 * @file test_metrics.cpp
 * @brief Comprehensive tests for metrics.hpp
 *
 * Covers: Counter, Gauge, Histogram, Summary, Timer, MetricRegistry
 */

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <sstream>
#include <thread>
#include <vector>

#include <ipb/common/metrics.hpp>

using namespace ipb::common::metrics;
using namespace std::chrono_literals;

//=============================================================================
// MetricType Tests
//=============================================================================

TEST(MetricTypeTest, TypeToString) {
    EXPECT_EQ(metric_type_string(MetricType::COUNTER), "counter");
    EXPECT_EQ(metric_type_string(MetricType::GAUGE), "gauge");
    EXPECT_EQ(metric_type_string(MetricType::HISTOGRAM), "histogram");
    EXPECT_EQ(metric_type_string(MetricType::SUMMARY), "summary");
}

//=============================================================================
// Counter Tests
//=============================================================================

class CounterTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(CounterTest, BasicConstruction) {
    Counter counter("test_counter", "A test counter", {{"key", "value"}});

    EXPECT_EQ(counter.name(), "test_counter");
    EXPECT_EQ(counter.help(), "A test counter");
    EXPECT_EQ(counter.type(), MetricType::COUNTER);
    EXPECT_DOUBLE_EQ(counter.value(), 0.0);

    Labels labels = counter.labels();
    EXPECT_EQ(labels.size(), 1);
    EXPECT_EQ(labels.at("key"), "value");
}

TEST_F(CounterTest, DefaultConstruction) {
    Counter counter("simple_counter");

    EXPECT_EQ(counter.name(), "simple_counter");
    EXPECT_EQ(counter.help(), "");
    EXPECT_TRUE(counter.labels().empty());
}

TEST_F(CounterTest, IncrementByOne) {
    Counter counter("inc_test");

    EXPECT_DOUBLE_EQ(counter.value(), 0.0);
    counter.inc();
    EXPECT_DOUBLE_EQ(counter.value(), 1.0);
    counter.inc();
    EXPECT_DOUBLE_EQ(counter.value(), 2.0);
}

TEST_F(CounterTest, IncrementByDelta) {
    Counter counter("delta_test");

    counter.inc(5.5);
    EXPECT_NEAR(counter.value(), 5.5, 0.001);

    counter.inc(10.25);
    EXPECT_NEAR(counter.value(), 15.75, 0.001);
}

TEST_F(CounterTest, NegativeDeltaIgnored) {
    Counter counter("negative_test");

    counter.inc(10.0);
    counter.inc(-5.0);  // Should be ignored
    EXPECT_NEAR(counter.value(), 10.0, 0.001);
}

TEST_F(CounterTest, Reset) {
    Counter counter("reset_test");

    counter.inc(100.0);
    EXPECT_NEAR(counter.value(), 100.0, 0.001);

    counter.reset();
    EXPECT_DOUBLE_EQ(counter.value(), 0.0);
}

TEST_F(CounterTest, PrometheusFormat) {
    Counter counter("http_requests_total", "Total HTTP requests", {{"method", "GET"}});
    counter.inc(42);

    std::string format = counter.prometheus_format();
    EXPECT_TRUE(format.find("# HELP http_requests_total") != std::string::npos);
    EXPECT_TRUE(format.find("# TYPE http_requests_total counter") != std::string::npos);
    EXPECT_TRUE(format.find("http_requests_total{method=\"GET\"}") != std::string::npos);
    EXPECT_TRUE(format.find("42") != std::string::npos);
}

TEST_F(CounterTest, PrometheusFormatNoLabels) {
    Counter counter("simple_counter");
    counter.inc(10);

    std::string format = counter.prometheus_format();
    EXPECT_TRUE(format.find("simple_counter ") != std::string::npos);
    EXPECT_FALSE(format.find("{") != std::string::npos);
}

TEST_F(CounterTest, ConcurrentIncrements) {
    Counter counter("concurrent_test");
    constexpr int num_threads = 8;
    constexpr int increments_per_thread = 10000;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&counter]() {
            for (int j = 0; j < increments_per_thread; ++j) {
                counter.inc();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_DOUBLE_EQ(counter.value(), num_threads * increments_per_thread);
}

//=============================================================================
// Gauge Tests
//=============================================================================

class GaugeTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(GaugeTest, BasicConstruction) {
    Gauge gauge("test_gauge", "A test gauge", {{"key", "value"}});

    EXPECT_EQ(gauge.name(), "test_gauge");
    EXPECT_EQ(gauge.help(), "A test gauge");
    EXPECT_EQ(gauge.type(), MetricType::GAUGE);
    EXPECT_DOUBLE_EQ(gauge.value(), 0.0);
}

TEST_F(GaugeTest, SetValue) {
    Gauge gauge("set_test");

    gauge.set(42.5);
    EXPECT_NEAR(gauge.value(), 42.5, 0.001);

    gauge.set(-10.5);
    EXPECT_NEAR(gauge.value(), -10.5, 0.001);
}

TEST_F(GaugeTest, IncrementDecrement) {
    Gauge gauge("inc_dec_test");

    gauge.inc();
    EXPECT_DOUBLE_EQ(gauge.value(), 1.0);

    gauge.inc(5.0);
    EXPECT_NEAR(gauge.value(), 6.0, 0.001);

    gauge.dec();
    EXPECT_NEAR(gauge.value(), 5.0, 0.001);

    gauge.dec(3.0);
    EXPECT_NEAR(gauge.value(), 2.0, 0.001);
}

TEST_F(GaugeTest, NegativeValues) {
    Gauge gauge("negative_test");

    gauge.dec(5.0);
    EXPECT_NEAR(gauge.value(), -5.0, 0.001);
}

TEST_F(GaugeTest, Reset) {
    Gauge gauge("reset_test");

    gauge.set(100.0);
    gauge.reset();
    EXPECT_DOUBLE_EQ(gauge.value(), 0.0);
}

TEST_F(GaugeTest, PrometheusFormat) {
    Gauge gauge("cpu_usage", "CPU usage percentage", {{"core", "0"}});
    gauge.set(75.5);

    std::string format = gauge.prometheus_format();
    EXPECT_TRUE(format.find("# HELP cpu_usage") != std::string::npos);
    EXPECT_TRUE(format.find("# TYPE cpu_usage gauge") != std::string::npos);
    EXPECT_TRUE(format.find("cpu_usage{core=\"0\"}") != std::string::npos);
}

TEST_F(GaugeTest, ConcurrentUpdates) {
    Gauge gauge("concurrent_gauge");
    constexpr int num_threads = 4;
    constexpr int operations_per_thread = 5000;

    std::vector<std::thread> threads;

    // Half threads increment, half decrement
    for (int i = 0; i < num_threads / 2; ++i) {
        threads.emplace_back([&gauge]() {
            for (int j = 0; j < operations_per_thread; ++j) {
                gauge.inc();
            }
        });
    }

    for (int i = 0; i < num_threads / 2; ++i) {
        threads.emplace_back([&gauge]() {
            for (int j = 0; j < operations_per_thread; ++j) {
                gauge.dec();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Equal increments and decrements should result in ~0
    EXPECT_NEAR(gauge.value(), 0.0, 1.0);
}

//=============================================================================
// Histogram Tests
//=============================================================================

class HistogramTest : public ::testing::Test {
protected:
    void SetUp() override {
        buckets_ = {0.1, 0.5, 1.0, 5.0, 10.0};
    }

    std::vector<double> buckets_;
};

TEST_F(HistogramTest, BasicConstruction) {
    auto histogram = std::make_unique<Histogram>("test_histogram", buckets_, "Test histogram");

    EXPECT_EQ(histogram->name(), "test_histogram");
    EXPECT_EQ(histogram->help(), "Test histogram");
    EXPECT_EQ(histogram->type(), MetricType::HISTOGRAM);
    EXPECT_EQ(histogram->count(), 0u);
    EXPECT_DOUBLE_EQ(histogram->sum(), 0.0);
}

TEST_F(HistogramTest, DefaultBuckets) {
    auto histogram = std::make_unique<Histogram>("default_buckets");
    const auto& buckets = histogram->buckets();
    EXPECT_EQ(buckets.size(), Histogram::DEFAULT_BUCKETS.size());
}

TEST_F(HistogramTest, BucketsSorted) {
    std::vector<double> unsorted = {5.0, 1.0, 10.0, 0.5};
    auto histogram = std::make_unique<Histogram>("sorted_test", unsorted);

    const auto& buckets = histogram->buckets();
    EXPECT_DOUBLE_EQ(buckets[0], 0.5);
    EXPECT_DOUBLE_EQ(buckets[1], 1.0);
    EXPECT_DOUBLE_EQ(buckets[2], 5.0);
    EXPECT_DOUBLE_EQ(buckets[3], 10.0);
}

TEST_F(HistogramTest, Observe) {
    auto histogram = std::make_unique<Histogram>("observe_test", buckets_);

    histogram->observe(0.05);
    histogram->observe(0.3);
    histogram->observe(2.0);

    EXPECT_EQ(histogram->count(), 3u);
    EXPECT_NEAR(histogram->sum(), 2.35, 0.001);
}

TEST_F(HistogramTest, BucketCounts) {
    auto histogram = std::make_unique<Histogram>("bucket_test", buckets_);

    // Buckets: 0.1, 0.5, 1.0, 5.0, 10.0, +Inf
    histogram->observe(0.05);   // <= 0.1, increments buckets 0,1,2,3,4,5
    histogram->observe(0.2);    // <= 0.5, increments buckets 1,2,3,4,5
    histogram->observe(0.8);    // <= 1.0, increments buckets 2,3,4,5
    histogram->observe(3.0);    // <= 5.0, increments buckets 3,4,5
    histogram->observe(7.0);    // <= 10.0, increments buckets 4,5

    // Histogram buckets are cumulative (values increment from matching bucket to +Inf)
    EXPECT_EQ(histogram->bucket_count(0), 1u);  // <= 0.1: only 0.05
    EXPECT_EQ(histogram->bucket_count(1), 2u);  // <= 0.5: 0.05, 0.2
    EXPECT_EQ(histogram->bucket_count(2), 3u);  // <= 1.0: 0.05, 0.2, 0.8
    EXPECT_EQ(histogram->bucket_count(3), 4u);  // <= 5.0: 0.05, 0.2, 0.8, 3.0
    EXPECT_EQ(histogram->bucket_count(4), 5u);  // <= 10.0: all 5 values
    EXPECT_EQ(histogram->bucket_count(5), 5u);  // +Inf: all 5 values
}

TEST_F(HistogramTest, Reset) {
    auto histogram = std::make_unique<Histogram>("reset_test", buckets_);

    histogram->observe(1.0);
    histogram->observe(2.0);
    histogram->reset();

    EXPECT_EQ(histogram->count(), 0u);
    EXPECT_DOUBLE_EQ(histogram->sum(), 0.0);
    EXPECT_EQ(histogram->bucket_count(0), 0u);
}

TEST_F(HistogramTest, PrometheusFormat) {
    auto histogram = std::make_unique<Histogram>("prometheus_test", buckets_, "Test help");

    histogram->observe(0.5);
    std::string format = histogram->prometheus_format();

    EXPECT_NE(format.find("# HELP prometheus_test"), std::string::npos);
    EXPECT_NE(format.find("# TYPE prometheus_test histogram"), std::string::npos);
    EXPECT_NE(format.find("prometheus_test_bucket"), std::string::npos);
    EXPECT_NE(format.find("prometheus_test_sum"), std::string::npos);
    EXPECT_NE(format.find("prometheus_test_count"), std::string::npos);
    EXPECT_NE(format.find("le=\"+Inf\""), std::string::npos);
}

TEST_F(HistogramTest, ThreadSafety) {
    auto histogram = std::make_unique<Histogram>("concurrent_test", buckets_);
    constexpr int num_threads = 4;
    constexpr int observations_per_thread = 1000;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&histogram, observations_per_thread]() {
            for (int j = 0; j < observations_per_thread; ++j) {
                histogram->observe(static_cast<double>(j % 10) * 0.1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(histogram->count(), static_cast<uint64_t>(num_threads * observations_per_thread));
}

TEST_F(HistogramTest, ExtremeValues) {
    auto histogram = std::make_unique<Histogram>("extreme_test", buckets_);

    histogram->observe(0.0001);     // Very small
    histogram->observe(1000000.0);  // Very large

    EXPECT_EQ(histogram->count(), 2u);
    EXPECT_GT(histogram->sum(), 1000000.0);
}

//=============================================================================
// Timer Tests
//=============================================================================

class TimerTest : public ::testing::Test {
protected:
    void SetUp() override {
        histogram_ = std::make_unique<Histogram>("timer_histogram", std::vector<double>{0.001, 0.01, 0.1, 1.0});
    }

    std::unique_ptr<Histogram> histogram_;
};

TEST_F(TimerTest, AutomaticTiming) {
    {
        Timer timer(*histogram_);
        std::this_thread::sleep_for(10ms);
    }  // Timer records on destruction

    EXPECT_EQ(histogram_->count(), 1u);
    EXPECT_GT(histogram_->sum(), 0.0);
}

TEST_F(TimerTest, MultipleTimes) {
    for (int i = 0; i < 5; ++i) {
        Timer timer(*histogram_);
        std::this_thread::sleep_for(1ms);
    }

    EXPECT_EQ(histogram_->count(), 5u);
    EXPECT_GT(histogram_->sum(), 0.0);
}

TEST_F(TimerTest, TimingAccuracy) {
    {
        Timer timer(*histogram_);
        std::this_thread::sleep_for(50ms);
    }

    // Should be approximately 0.05 seconds (with generous tolerance for CI runners)
    EXPECT_GE(histogram_->sum(), 0.03);
    EXPECT_LE(histogram_->sum(), 0.30);
}

//=============================================================================
// Summary Tests
//=============================================================================

class SummaryTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(SummaryTest, BasicConstruction) {
    Summary summary("request_duration", Summary::DEFAULT_QUANTILES, "Request duration");

    EXPECT_EQ(summary.name(), "request_duration");
    EXPECT_EQ(summary.type(), MetricType::SUMMARY);
    EXPECT_EQ(summary.count(), 0);
    EXPECT_DOUBLE_EQ(summary.sum(), 0.0);
}

TEST_F(SummaryTest, Observe) {
    Summary summary("observe_test");

    summary.observe(1.0);
    summary.observe(2.0);
    summary.observe(3.0);

    EXPECT_EQ(summary.count(), 3);
    EXPECT_NEAR(summary.sum(), 6.0, 0.001);
}

TEST_F(SummaryTest, QuantileCalculation) {
    Summary summary("quantile_test");

    // Add values 1-100
    for (int i = 1; i <= 100; ++i) {
        summary.observe(static_cast<double>(i));
    }

    // Median should be around 50
    double p50 = summary.quantile_value(0.5);
    EXPECT_NEAR(p50, 50.0, 2.0);

    // P90 should be around 90
    double p90 = summary.quantile_value(0.9);
    EXPECT_NEAR(p90, 90.0, 2.0);

    // P99 should be around 99
    double p99 = summary.quantile_value(0.99);
    EXPECT_NEAR(p99, 99.0, 2.0);
}

TEST_F(SummaryTest, EmptyQuantile) {
    Summary summary("empty_test");

    // Empty summary should return 0
    EXPECT_DOUBLE_EQ(summary.quantile_value(0.5), 0.0);
}

TEST_F(SummaryTest, Reset) {
    Summary summary("reset_test");

    summary.observe(10.0);
    summary.observe(20.0);

    summary.reset();

    EXPECT_EQ(summary.count(), 0);
    EXPECT_DOUBLE_EQ(summary.sum(), 0.0);
    EXPECT_DOUBLE_EQ(summary.quantile_value(0.5), 0.0);
}

TEST_F(SummaryTest, PrometheusFormat) {
    Summary summary("response_size", Summary::DEFAULT_QUANTILES, "Response size", {{"handler", "api"}});

    summary.observe(100.0);
    summary.observe(200.0);
    summary.observe(300.0);

    std::string format = summary.prometheus_format();

    EXPECT_TRUE(format.find("# HELP response_size") != std::string::npos);
    EXPECT_TRUE(format.find("# TYPE response_size summary") != std::string::npos);
    EXPECT_TRUE(format.find("quantile=\"0.50\"") != std::string::npos);
    EXPECT_TRUE(format.find("quantile=\"0.90\"") != std::string::npos);
    EXPECT_TRUE(format.find("quantile=\"0.99\"") != std::string::npos);
    EXPECT_TRUE(format.find("response_size_sum") != std::string::npos);
    EXPECT_TRUE(format.find("response_size_count") != std::string::npos);
}

TEST_F(SummaryTest, ConcurrentObservations) {
    Summary summary("concurrent_summary");
    constexpr int num_threads = 4;
    constexpr int observations_per_thread = 100;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&summary]() {
            for (int j = 0; j < observations_per_thread; ++j) {
                summary.observe(1.0);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(summary.count(), num_threads * observations_per_thread);
}

//=============================================================================
// MetricRegistry Tests
//=============================================================================

class MetricRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset registry state before each test
        MetricRegistry::instance().reset_all();
    }
};

TEST_F(MetricRegistryTest, Singleton) {
    MetricRegistry& r1 = MetricRegistry::instance();
    MetricRegistry& r2 = MetricRegistry::instance();

    EXPECT_EQ(&r1, &r2);
}

TEST_F(MetricRegistryTest, CounterRegistration) {
    auto& registry = MetricRegistry::instance();

    Counter& c1 = registry.counter("test_counter", {{"env", "test"}}, "Test counter");
    Counter& c2 = registry.counter("test_counter", {{"env", "test"}});

    // Same name and labels should return same counter
    EXPECT_EQ(&c1, &c2);

    c1.inc();
    EXPECT_DOUBLE_EQ(c2.value(), 1.0);
}

TEST_F(MetricRegistryTest, DifferentLabels) {
    auto& registry = MetricRegistry::instance();

    Counter& c1 = registry.counter("http_requests", {{"method", "GET"}});
    Counter& c2 = registry.counter("http_requests", {{"method", "POST"}});

    // Different labels should create different counters
    EXPECT_NE(&c1, &c2);

    c1.inc();
    EXPECT_DOUBLE_EQ(c1.value(), 1.0);
    EXPECT_DOUBLE_EQ(c2.value(), 0.0);
}

TEST_F(MetricRegistryTest, GaugeRegistration) {
    auto& registry = MetricRegistry::instance();

    Gauge& g1 = registry.gauge("memory_usage", {}, "Memory usage");
    Gauge& g2 = registry.gauge("memory_usage", {});

    EXPECT_EQ(&g1, &g2);

    g1.set(100.0);
    EXPECT_DOUBLE_EQ(g2.value(), 100.0);
}

TEST_F(MetricRegistryTest, HistogramRegistration) {
    auto& registry = MetricRegistry::instance();

    std::vector<double> buckets = {1.0, 5.0, 10.0};
    Histogram& h1 = registry.histogram("test_histogram", buckets, {}, "Test histogram");
    Histogram& h2 = registry.histogram("test_histogram", buckets, {});

    EXPECT_EQ(&h1, &h2);

    h1.observe(3.0);
    EXPECT_EQ(h2.count(), 1u);
}

TEST_F(MetricRegistryTest, SummaryRegistration) {
    auto& registry = MetricRegistry::instance();

    Summary& s1 = registry.summary("response_time");
    Summary& s2 = registry.summary("response_time");

    EXPECT_EQ(&s1, &s2);
}

TEST_F(MetricRegistryTest, MetricCount) {
    auto& registry = MetricRegistry::instance();

    size_t initial = registry.metric_count();

    registry.counter("count_test_counter");
    registry.gauge("count_test_gauge");
    registry.histogram("count_test_histogram");
    registry.summary("count_test_summary");

    EXPECT_EQ(registry.metric_count(), initial + 4);
}

TEST_F(MetricRegistryTest, PrometheusExport) {
    auto& registry = MetricRegistry::instance();

    auto& counter = registry.counter("export_counter", {{"type", "test"}}, "Export test counter");
    counter.inc(42);

    auto& gauge = registry.gauge("export_gauge", {}, "Export test gauge");
    gauge.set(100.0);

    std::string output = registry.prometheus_export();

    EXPECT_TRUE(output.find("export_counter") != std::string::npos);
    EXPECT_TRUE(output.find("export_gauge") != std::string::npos);
}

TEST_F(MetricRegistryTest, ResetAll) {
    auto& registry = MetricRegistry::instance();

    auto& counter = registry.counter("reset_all_counter");
    counter.inc(100);

    auto& gauge = registry.gauge("reset_all_gauge");
    gauge.set(50.0);

    registry.reset_all();

    EXPECT_DOUBLE_EQ(counter.value(), 0.0);
    EXPECT_DOUBLE_EQ(gauge.value(), 0.0);
}

TEST_F(MetricRegistryTest, ConcurrentAccess) {
    auto& registry = MetricRegistry::instance();
    constexpr int num_threads = 8;
    constexpr int ops_per_thread = 1000;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&registry, i]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                // Each thread works with its own named metrics
                std::string name = "thread_" + std::to_string(i) + "_counter";
                auto& counter = registry.counter(name);
                counter.inc();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Verify each thread's counter has the right value
    for (int i = 0; i < num_threads; ++i) {
        std::string name = "thread_" + std::to_string(i) + "_counter";
        auto& counter = registry.counter(name);
        EXPECT_DOUBLE_EQ(counter.value(), ops_per_thread);
    }
}

//=============================================================================
// Labels Formatting Tests
//=============================================================================

TEST(LabelsTest, MultipleLabels) {
    Counter counter("multi_label", "Help", {{"method", "GET"}, {"path", "/api"}, {"status", "200"}});

    std::string format = counter.prometheus_format();

    EXPECT_TRUE(format.find("method=\"GET\"") != std::string::npos);
    EXPECT_TRUE(format.find("path=\"/api\"") != std::string::npos);
    EXPECT_TRUE(format.find("status=\"200\"") != std::string::npos);
}

TEST(LabelsTest, EmptyLabels) {
    Counter counter("no_labels");
    std::string format = counter.prometheus_format();

    // Should not have curly braces
    std::string name_line;
    std::istringstream iss(format);
    while (std::getline(iss, name_line)) {
        if (name_line.find("# ") != 0 && name_line.find("no_labels") == 0) {
            EXPECT_EQ(name_line.find("{"), std::string::npos);
            break;
        }
    }
}

//=============================================================================
// Edge Cases
//=============================================================================

TEST(EdgeCaseTest, VeryLargeValues) {
    Counter counter("large_value");

    // Maximum safe value with PRECISION=1e6 is ~1e13 to avoid uint64_t overflow
    double large_value = 1e12;
    counter.inc(large_value);
    EXPECT_NEAR(counter.value(), large_value, 1.0);
}

TEST(EdgeCaseTest, VerySmallValues) {
    Gauge gauge("small_value");

    double small_value = 1e-9;
    gauge.set(small_value);
    // May lose some precision due to fixed-point conversion
    EXPECT_NEAR(gauge.value(), small_value, 1e-6);
}

TEST(EdgeCaseTest, HistogramInfBucket) {
    std::vector<double> buckets = {1.0, 10.0};
    auto histogram = std::make_unique<Histogram>("inf_test", buckets);

    histogram->observe(100.0);  // Goes to +Inf bucket

    // Last bucket (+Inf) should have the count
    EXPECT_EQ(histogram->bucket_count(buckets.size()), 1u);
}
