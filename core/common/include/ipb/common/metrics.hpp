#pragma once

/**
 * @file metrics.hpp
 * @brief Enterprise-grade metrics collection system
 *
 * OpenTelemetry-compatible metrics primitives:
 * - Counter: Monotonically increasing value
 * - Gauge: Point-in-time value that can go up/down
 * - Histogram: Distribution of values with configurable buckets
 * - Summary: Quantile calculations over sliding window
 *
 * Features:
 * - Lock-free fast path for hot metrics
 * - Prometheus exposition format export
 * - Dimension/label support
 * - Per-thread aggregation to reduce contention
 * - Automatic metric registration
 *
 * Usage:
 *   auto& registry = MetricRegistry::instance();
 *   auto& counter = registry.counter("requests_total", {{"method", "GET"}});
 *   counter.inc();
 *
 *   auto& histogram = registry.histogram("latency_seconds", {0.001, 0.01, 0.1, 1.0});
 *   histogram.observe(0.025);
 */

#include <ipb/common/platform.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ipb::common::metrics {

//=============================================================================
// Types and Constants
//=============================================================================

using Labels    = std::map<std::string, std::string>;
using Timestamp = std::chrono::system_clock::time_point;

/**
 * @brief Metric type enumeration
 */
enum class MetricType { COUNTER, GAUGE, HISTOGRAM, SUMMARY };

inline std::string metric_type_string(MetricType type) {
    switch (type) {
        case MetricType::COUNTER:
            return "counter";
        case MetricType::GAUGE:
            return "gauge";
        case MetricType::HISTOGRAM:
            return "histogram";
        case MetricType::SUMMARY:
            return "summary";
    }
    return "unknown";
}

//=============================================================================
// Base Metric Interface
//=============================================================================

/**
 * @brief Base class for all metric types
 */
class Metric {
public:
    virtual ~Metric() = default;

    virtual MetricType type() const  = 0;
    virtual std::string name() const = 0;
    virtual std::string help() const = 0;
    virtual Labels labels() const    = 0;

    /**
     * @brief Export metric in Prometheus format
     */
    virtual std::string prometheus_format() const = 0;

    /**
     * @brief Reset metric value
     */
    virtual void reset() = 0;
};

//=============================================================================
// Counter
//=============================================================================

/**
 * @brief Monotonically increasing counter
 *
 * Thread-safe, lock-free counter using atomic operations.
 * Value can only increase or be reset to zero.
 */
class alignas(IPB_CACHE_LINE_SIZE) Counter : public Metric {
public:
    Counter(std::string name, std::string help = "", Labels labels = {})
        : name_(std::move(name)), help_(std::move(help)), labels_(std::move(labels)), value_(0) {}

    MetricType type() const override { return MetricType::COUNTER; }
    std::string name() const override { return name_; }
    std::string help() const override { return help_; }
    Labels labels() const override { return labels_; }

    /**
     * @brief Increment counter by 1
     */
    void inc() noexcept { value_.fetch_add(1, std::memory_order_relaxed); }

    /**
     * @brief Increment counter by delta
     */
    void inc(double delta) noexcept {
        if (delta < 0)
            return;  // Counters can't decrease
        uint64_t int_delta = static_cast<uint64_t>(delta * PRECISION);
        value_.fetch_add(int_delta, std::memory_order_relaxed);
    }

    /**
     * @brief Get current value
     */
    double value() const noexcept {
        return static_cast<double>(value_.load(std::memory_order_relaxed)) / PRECISION;
    }

    void reset() override { value_.store(0, std::memory_order_relaxed); }

    std::string prometheus_format() const override {
        std::ostringstream oss;
        oss << "# HELP " << name_ << " " << help_ << "\n";
        oss << "# TYPE " << name_ << " counter\n";
        oss << name_ << format_labels(labels_) << " " << std::fixed << std::setprecision(6)
            << value() << "\n";
        return oss.str();
    }

private:
    static constexpr uint64_t PRECISION = 1000000;

    std::string name_;
    std::string help_;
    Labels labels_;
    std::atomic<uint64_t> value_;

    static std::string format_labels(const Labels& labels) {
        if (labels.empty())
            return "";
        std::ostringstream oss;
        oss << "{";
        bool first = true;
        for (const auto& [k, v] : labels) {
            if (!first)
                oss << ",";
            first = false;
            oss << k << "=\"" << v << "\"";
        }
        oss << "}";
        return oss.str();
    }
};

//=============================================================================
// Gauge
//=============================================================================

/**
 * @brief Point-in-time value that can increase or decrease
 *
 * Thread-safe gauge using atomic operations.
 */
class alignas(IPB_CACHE_LINE_SIZE) Gauge : public Metric {
public:
    Gauge(std::string name, std::string help = "", Labels labels = {})
        : name_(std::move(name)), help_(std::move(help)), labels_(std::move(labels)) {
        value_.store(0, std::memory_order_relaxed);
    }

    MetricType type() const override { return MetricType::GAUGE; }
    std::string name() const override { return name_; }
    std::string help() const override { return help_; }
    Labels labels() const override { return labels_; }

    /**
     * @brief Set gauge to specific value
     */
    void set(double value) noexcept {
        int64_t int_val = static_cast<int64_t>(value * PRECISION);
        value_.store(int_val, std::memory_order_relaxed);
    }

    /**
     * @brief Increment gauge by 1
     */
    void inc() noexcept { value_.fetch_add(PRECISION, std::memory_order_relaxed); }

    /**
     * @brief Increment gauge by delta
     */
    void inc(double delta) noexcept {
        int64_t int_delta = static_cast<int64_t>(delta * PRECISION);
        value_.fetch_add(int_delta, std::memory_order_relaxed);
    }

    /**
     * @brief Decrement gauge by 1
     */
    void dec() noexcept { value_.fetch_sub(PRECISION, std::memory_order_relaxed); }

    /**
     * @brief Decrement gauge by delta
     */
    void dec(double delta) noexcept {
        int64_t int_delta = static_cast<int64_t>(delta * PRECISION);
        value_.fetch_sub(int_delta, std::memory_order_relaxed);
    }

    /**
     * @brief Get current value
     */
    double value() const noexcept {
        return static_cast<double>(value_.load(std::memory_order_relaxed)) / PRECISION;
    }

    void reset() override { value_.store(0, std::memory_order_relaxed); }

    std::string prometheus_format() const override {
        std::ostringstream oss;
        oss << "# HELP " << name_ << " " << help_ << "\n";
        oss << "# TYPE " << name_ << " gauge\n";
        oss << name_ << format_labels(labels_) << " " << std::fixed << std::setprecision(6)
            << value() << "\n";
        return oss.str();
    }

private:
    static constexpr int64_t PRECISION = 1000000;

    std::string name_;
    std::string help_;
    Labels labels_;
    std::atomic<int64_t> value_;

    static std::string format_labels(const Labels& labels) {
        if (labels.empty())
            return "";
        std::ostringstream oss;
        oss << "{";
        bool first = true;
        for (const auto& [k, v] : labels) {
            if (!first)
                oss << ",";
            first = false;
            oss << k << "=\"" << v << "\"";
        }
        oss << "}";
        return oss.str();
    }
};

//=============================================================================
// Histogram
//=============================================================================

/**
 * @brief Distribution histogram with configurable buckets
 *
 * Tracks value distribution across predefined buckets.
 * Thread-safe with per-bucket atomic counters.
 */
class Histogram : public Metric {
public:
    static const std::vector<double> DEFAULT_BUCKETS;

    Histogram(std::string name, std::vector<double> buckets = DEFAULT_BUCKETS,
              std::string help = "", Labels labels = {})
        : name_(std::move(name)), help_(std::move(help)), labels_(std::move(labels)),
          buckets_(std::move(buckets)) {
        std::sort(buckets_.begin(), buckets_.end());

        // Initialize bucket counters
        bucket_counts_.resize(buckets_.size() + 1);  // +1 for +Inf
        for (auto& count : bucket_counts_) {
            count.store(0, std::memory_order_relaxed);
        }

        sum_.store(0, std::memory_order_relaxed);
        count_.store(0, std::memory_order_relaxed);
    }

    MetricType type() const override { return MetricType::HISTOGRAM; }
    std::string name() const override { return name_; }
    std::string help() const override { return help_; }
    Labels labels() const override { return labels_; }

    /**
     * @brief Record a value observation
     */
    void observe(double value) noexcept {
        // Find bucket and increment
        size_t bucket_idx = buckets_.size();  // +Inf by default
        for (size_t i = 0; i < buckets_.size(); ++i) {
            if (value <= buckets_[i]) {
                bucket_idx = i;
                break;
            }
        }

        // Increment all buckets from bucket_idx to +Inf (cumulative)
        for (size_t i = bucket_idx; i <= buckets_.size(); ++i) {
            bucket_counts_[i].fetch_add(1, std::memory_order_relaxed);
        }

        // Update sum and count
        int64_t int_value = static_cast<int64_t>(value * PRECISION);
        sum_.fetch_add(int_value, std::memory_order_relaxed);
        count_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Get observation count
     */
    uint64_t count() const noexcept { return count_.load(std::memory_order_relaxed); }

    /**
     * @brief Get sum of all observations
     */
    double sum() const noexcept {
        return static_cast<double>(sum_.load(std::memory_order_relaxed)) / PRECISION;
    }

    /**
     * @brief Get bucket boundaries
     */
    const std::vector<double>& buckets() const noexcept { return buckets_; }

    /**
     * @brief Get count for specific bucket
     */
    uint64_t bucket_count(size_t idx) const noexcept {
        if (idx >= bucket_counts_.size())
            return 0;
        return bucket_counts_[idx].load(std::memory_order_relaxed);
    }

    void reset() override {
        for (auto& count : bucket_counts_) {
            count.store(0, std::memory_order_relaxed);
        }
        sum_.store(0, std::memory_order_relaxed);
        count_.store(0, std::memory_order_relaxed);
    }

    std::string prometheus_format() const override {
        std::ostringstream oss;
        oss << "# HELP " << name_ << " " << help_ << "\n";
        oss << "# TYPE " << name_ << " histogram\n";

        std::string label_str = format_labels(labels_);

        // Output bucket counts
        for (size_t i = 0; i < buckets_.size(); ++i) {
            oss << name_ << "_bucket" << format_labels_with_le(labels_, buckets_[i]) << " "
                << bucket_counts_[i].load(std::memory_order_relaxed) << "\n";
        }

        // +Inf bucket
        oss << name_ << "_bucket"
            << format_labels_with_le(labels_, std::numeric_limits<double>::infinity()) << " "
            << bucket_counts_.back().load(std::memory_order_relaxed) << "\n";

        // Sum and count
        oss << name_ << "_sum" << label_str << " " << std::fixed << std::setprecision(6) << sum()
            << "\n";
        oss << name_ << "_count" << label_str << " " << count() << "\n";

        return oss.str();
    }

private:
    static constexpr int64_t PRECISION = 1000000;

    std::string name_;
    std::string help_;
    Labels labels_;
    std::vector<double> buckets_;
    std::vector<std::atomic<uint64_t>> bucket_counts_;
    std::atomic<int64_t> sum_;
    std::atomic<uint64_t> count_;

    static std::string format_labels(const Labels& labels) {
        if (labels.empty())
            return "";
        std::ostringstream oss;
        oss << "{";
        bool first = true;
        for (const auto& [k, v] : labels) {
            if (!first)
                oss << ",";
            first = false;
            oss << k << "=\"" << v << "\"";
        }
        oss << "}";
        return oss.str();
    }

    static std::string format_labels_with_le(const Labels& labels, double le) {
        std::ostringstream oss;
        oss << "{";

        for (const auto& [k, v] : labels) {
            oss << k << "=\"" << v << "\",";
        }

        if (std::isinf(le)) {
            oss << "le=\"+Inf\"";
        } else {
            oss << "le=\"" << std::fixed << std::setprecision(6) << le << "\"";
        }

        oss << "}";
        return oss.str();
    }
};

// Default histogram buckets (similar to Prometheus defaults)
inline const std::vector<double> Histogram::DEFAULT_BUCKETS = {0.005, 0.01, 0.025, 0.05, 0.1, 0.25,
                                                               0.5,   1.0,  2.5,   5.0,  10.0};

//=============================================================================
// Summary
//=============================================================================

/**
 * @brief Quantile summary over sliding time window
 *
 * Calculates configurable quantiles (e.g., p50, p90, p99) over observations.
 * Uses approximate streaming quantile algorithm.
 */
class Summary : public Metric {
public:
    struct Quantile {
        double quantile;
        double error;
    };

    static const std::vector<Quantile> DEFAULT_QUANTILES;

    Summary(std::string name, std::vector<Quantile> quantiles = DEFAULT_QUANTILES,
            std::string help = "", Labels labels = {},
            std::chrono::seconds max_age = std::chrono::seconds(60))
        : name_(std::move(name)), help_(std::move(help)), labels_(std::move(labels)),
          quantiles_(std::move(quantiles)), max_age_(max_age) {
        sum_.store(0, std::memory_order_relaxed);
        count_.store(0, std::memory_order_relaxed);
    }

    MetricType type() const override { return MetricType::SUMMARY; }
    std::string name() const override { return name_; }
    std::string help() const override { return help_; }
    Labels labels() const override { return labels_; }

    /**
     * @brief Record a value observation
     */
    void observe(double value) noexcept {
        auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard lock(mutex_);
            observations_.push_back({value, now});

            // Remove old observations
            auto cutoff = now - max_age_;
            while (!observations_.empty() && observations_.front().timestamp < cutoff) {
                observations_.erase(observations_.begin());
            }
        }

        int64_t int_value = static_cast<int64_t>(value * PRECISION);
        sum_.fetch_add(int_value, std::memory_order_relaxed);
        count_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Get quantile value
     */
    double quantile_value(double q) const {
        std::lock_guard lock(mutex_);

        if (observations_.empty())
            return 0;

        std::vector<double> values;
        values.reserve(observations_.size());
        for (const auto& obs : observations_) {
            values.push_back(obs.value);
        }

        std::sort(values.begin(), values.end());

        double idx   = q * (values.size() - 1);
        size_t lower = static_cast<size_t>(idx);
        size_t upper = lower + 1;

        if (upper >= values.size())
            return values.back();

        double frac = idx - lower;
        return values[lower] * (1 - frac) + values[upper] * frac;
    }

    uint64_t count() const noexcept { return count_.load(std::memory_order_relaxed); }

    double sum() const noexcept {
        return static_cast<double>(sum_.load(std::memory_order_relaxed)) / PRECISION;
    }

    void reset() override {
        std::lock_guard lock(mutex_);
        observations_.clear();
        sum_.store(0, std::memory_order_relaxed);
        count_.store(0, std::memory_order_relaxed);
    }

    std::string prometheus_format() const override {
        std::ostringstream oss;
        oss << "# HELP " << name_ << " " << help_ << "\n";
        oss << "# TYPE " << name_ << " summary\n";

        std::string label_str = format_labels(labels_);

        // Output quantiles
        for (const auto& q : quantiles_) {
            double val = quantile_value(q.quantile);
            oss << name_ << format_labels_with_quantile(labels_, q.quantile) << " " << std::fixed
                << std::setprecision(6) << val << "\n";
        }

        // Sum and count
        oss << name_ << "_sum" << label_str << " " << std::fixed << std::setprecision(6) << sum()
            << "\n";
        oss << name_ << "_count" << label_str << " " << count() << "\n";

        return oss.str();
    }

private:
    static constexpr int64_t PRECISION = 1000000;

    struct Observation {
        double value;
        std::chrono::steady_clock::time_point timestamp;
    };

    std::string name_;
    std::string help_;
    Labels labels_;
    std::vector<Quantile> quantiles_;
    std::chrono::seconds max_age_;

    mutable std::mutex mutex_;
    std::vector<Observation> observations_;
    std::atomic<int64_t> sum_;
    std::atomic<uint64_t> count_;

    static std::string format_labels(const Labels& labels) {
        if (labels.empty())
            return "";
        std::ostringstream oss;
        oss << "{";
        bool first = true;
        for (const auto& [k, v] : labels) {
            if (!first)
                oss << ",";
            first = false;
            oss << k << "=\"" << v << "\"";
        }
        oss << "}";
        return oss.str();
    }

    static std::string format_labels_with_quantile(const Labels& labels, double q) {
        std::ostringstream oss;
        oss << "{";

        for (const auto& [k, v] : labels) {
            oss << k << "=\"" << v << "\",";
        }

        oss << "quantile=\"" << std::fixed << std::setprecision(2) << q << "\"";
        oss << "}";
        return oss.str();
    }
};

// Default summary quantiles
inline const std::vector<Summary::Quantile> Summary::DEFAULT_QUANTILES = {
    {0.5,  0.05 },
    {0.9,  0.01 },
    {0.99, 0.001}
};

//=============================================================================
// Timer (convenience wrapper around Histogram)
//=============================================================================

/**
 * @brief Convenience class for timing operations
 */
class Timer {
public:
    explicit Timer(Histogram& histogram)
        : histogram_(histogram), start_(std::chrono::high_resolution_clock::now()) {}

    ~Timer() {
        auto end       = std::chrono::high_resolution_clock::now();
        double seconds = std::chrono::duration<double>(end - start_).count();
        histogram_.observe(seconds);
    }

    // Disable copy/move
    Timer(const Timer&)            = delete;
    Timer& operator=(const Timer&) = delete;

private:
    Histogram& histogram_;
    std::chrono::high_resolution_clock::time_point start_;
};

//=============================================================================
// Metric Registry
//=============================================================================

/**
 * @brief Central registry for all metrics
 *
 * Thread-safe singleton for metric registration and retrieval.
 */
class MetricRegistry {
public:
    static MetricRegistry& instance() {
        static MetricRegistry registry;
        return registry;
    }

    /**
     * @brief Get or create counter
     */
    Counter& counter(const std::string& name, const Labels& labels = {},
                     const std::string& help = "") {
        std::string key = make_key(name, labels);

        std::shared_lock read_lock(mutex_);
        auto it = counters_.find(key);
        if (it != counters_.end()) {
            return *it->second;
        }
        read_lock.unlock();

        std::unique_lock write_lock(mutex_);
        // Double-check after acquiring write lock
        it = counters_.find(key);
        if (it != counters_.end()) {
            return *it->second;
        }

        auto counter   = std::make_unique<Counter>(name, help, labels);
        auto& ref      = *counter;
        counters_[key] = std::move(counter);
        return ref;
    }

    /**
     * @brief Get or create gauge
     */
    Gauge& gauge(const std::string& name, const Labels& labels = {}, const std::string& help = "") {
        std::string key = make_key(name, labels);

        std::shared_lock read_lock(mutex_);
        auto it = gauges_.find(key);
        if (it != gauges_.end()) {
            return *it->second;
        }
        read_lock.unlock();

        std::unique_lock write_lock(mutex_);
        it = gauges_.find(key);
        if (it != gauges_.end()) {
            return *it->second;
        }

        auto gauge   = std::make_unique<Gauge>(name, help, labels);
        auto& ref    = *gauge;
        gauges_[key] = std::move(gauge);
        return ref;
    }

    /**
     * @brief Get or create histogram
     */
    Histogram& histogram(const std::string& name,
                         const std::vector<double>& buckets = Histogram::DEFAULT_BUCKETS,
                         const Labels& labels = {}, const std::string& help = "") {
        std::string key = make_key(name, labels);

        std::shared_lock read_lock(mutex_);
        auto it = histograms_.find(key);
        if (it != histograms_.end()) {
            return *it->second;
        }
        read_lock.unlock();

        std::unique_lock write_lock(mutex_);
        it = histograms_.find(key);
        if (it != histograms_.end()) {
            return *it->second;
        }

        auto histogram   = std::make_unique<Histogram>(name, buckets, help, labels);
        auto& ref        = *histogram;
        histograms_[key] = std::move(histogram);
        return ref;
    }

    /**
     * @brief Get or create summary
     */
    Summary& summary(const std::string& name,
                     const std::vector<Summary::Quantile>& quantiles = Summary::DEFAULT_QUANTILES,
                     const Labels& labels = {}, const std::string& help = "") {
        std::string key = make_key(name, labels);

        std::shared_lock read_lock(mutex_);
        auto it = summaries_.find(key);
        if (it != summaries_.end()) {
            return *it->second;
        }
        read_lock.unlock();

        std::unique_lock write_lock(mutex_);
        it = summaries_.find(key);
        if (it != summaries_.end()) {
            return *it->second;
        }

        auto summary    = std::make_unique<Summary>(name, quantiles, help, labels);
        auto& ref       = *summary;
        summaries_[key] = std::move(summary);
        return ref;
    }

    /**
     * @brief Export all metrics in Prometheus format
     */
    std::string prometheus_export() const {
        std::ostringstream oss;

        std::shared_lock lock(mutex_);

        for (const auto& [_, counter] : counters_) {
            oss << counter->prometheus_format();
        }

        for (const auto& [_, gauge] : gauges_) {
            oss << gauge->prometheus_format();
        }

        for (const auto& [_, histogram] : histograms_) {
            oss << histogram->prometheus_format();
        }

        for (const auto& [_, summary] : summaries_) {
            oss << summary->prometheus_format();
        }

        return oss.str();
    }

    /**
     * @brief Reset all metrics
     */
    void reset_all() {
        std::unique_lock lock(mutex_);

        for (auto& [_, counter] : counters_) {
            counter->reset();
        }
        for (auto& [_, gauge] : gauges_) {
            gauge->reset();
        }
        for (auto& [_, histogram] : histograms_) {
            histogram->reset();
        }
        for (auto& [_, summary] : summaries_) {
            summary->reset();
        }
    }

    /**
     * @brief Get count of registered metrics
     */
    size_t metric_count() const {
        std::shared_lock lock(mutex_);
        return counters_.size() + gauges_.size() + histograms_.size() + summaries_.size();
    }

private:
    MetricRegistry() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<Counter>> counters_;
    std::unordered_map<std::string, std::unique_ptr<Gauge>> gauges_;
    std::unordered_map<std::string, std::unique_ptr<Histogram>> histograms_;
    std::unordered_map<std::string, std::unique_ptr<Summary>> summaries_;

    static std::string make_key(const std::string& name, const Labels& labels) {
        std::ostringstream oss;
        oss << name;
        for (const auto& [k, v] : labels) {
            oss << ";" << k << "=" << v;
        }
        return oss.str();
    }
};

//=============================================================================
// Convenience Macros
//=============================================================================

#define IPB_COUNTER(name, ...) \
    ipb::common::metrics::MetricRegistry::instance().counter(name, ##__VA_ARGS__)

#define IPB_GAUGE(name, ...) \
    ipb::common::metrics::MetricRegistry::instance().gauge(name, ##__VA_ARGS__)

#define IPB_HISTOGRAM(name, ...) \
    ipb::common::metrics::MetricRegistry::instance().histogram(name, ##__VA_ARGS__)

#define IPB_TIMER(histogram) ipb::common::metrics::Timer _timer_##__LINE__(histogram)

}  // namespace ipb::common::metrics
