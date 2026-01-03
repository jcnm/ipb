#pragma once

/**
 * @file transform_pipeline.hpp
 * @brief Composable transformation pipeline
 *
 * The TransformPipeline allows chaining multiple transformers together
 * while maintaining bijectivity. The inverse operation applies
 * transformers in reverse order.
 *
 * @code
 * // Build a pipeline: compress -> encrypt
 * auto pipeline = TransformPipeline::builder()
 *     .add<ZstdTransformer>(CompressionLevel::FAST)
 *     .add<AesGcmTransformer>(key)
 *     .build();
 *
 * // Forward: data -> compressed -> encrypted
 * auto result = pipeline.transform(plaintext);
 *
 * // Inverse: encrypted -> compressed -> data (reverse order)
 * auto original = pipeline.inverse(result.value());
 * @endcode
 */

#include "transformer.hpp"

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ipb::transform {

// Forward declaration
class TransformPipeline;

// ============================================================================
// PIPELINE BUILDER
// ============================================================================

/**
 * @brief Fluent builder for constructing transform pipelines
 */
class PipelineBuilder {
public:
    PipelineBuilder() = default;

    // Move-only (stages_ contains unique_ptr)
    PipelineBuilder(PipelineBuilder&&) noexcept = default;
    PipelineBuilder& operator=(PipelineBuilder&&) noexcept = default;
    PipelineBuilder(const PipelineBuilder&) = delete;
    PipelineBuilder& operator=(const PipelineBuilder&) = delete;

    /**
     * @brief Add a transformer by unique_ptr
     */
    PipelineBuilder& add(std::unique_ptr<ITransformer> transformer) {
        if (transformer) {
            stages_.push_back(std::move(transformer));
        }
        return *this;
    }

    /**
     * @brief Add a transformer by constructing in-place
     */
    template <typename T, typename... Args>
    PipelineBuilder& add(Args&&... args) {
        static_assert(std::is_base_of_v<ITransformer, T>,
                      "T must derive from ITransformer");
        stages_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        return *this;
    }

    /**
     * @brief Add a transformer conditionally
     */
    template <typename T, typename... Args>
    PipelineBuilder& add_if(bool condition, Args&&... args) {
        if (condition) {
            add<T>(std::forward<Args>(args)...);
        }
        return *this;
    }

    /**
     * @brief Add compression stage with specified algorithm
     */
    PipelineBuilder& compress(TransformerId algo,
                               CompressionLevel level = CompressionLevel::DEFAULT);

    /**
     * @brief Add encryption stage with specified algorithm
     */
    PipelineBuilder& encrypt(TransformerId algo,
                              std::span<const uint8_t> key,
                              std::span<const uint8_t> nonce = {});

    /**
     * @brief Build the pipeline
     */
    TransformPipeline build();

    /**
     * @brief Build as unique_ptr
     */
    std::unique_ptr<TransformPipeline> build_unique();

    /**
     * @brief Get number of stages
     */
    size_t size() const noexcept { return stages_.size(); }

    /**
     * @brief Check if builder is empty
     */
    bool empty() const noexcept { return stages_.empty(); }

private:
    std::vector<std::unique_ptr<ITransformer>> stages_;
};

// ============================================================================
// TRANSFORM PIPELINE
// ============================================================================

/**
 * @brief A composable, bijective transformation pipeline
 *
 * Chains multiple transformers together. Forward transformation applies
 * stages in order (first to last), while inverse transformation applies
 * them in reverse order (last to first).
 *
 * Thread Safety:
 * - Pipeline is immutable after construction
 * - transform() and inverse() are thread-safe if underlying transformers are
 */
class TransformPipeline : public ITransformer {
public:
    /**
     * @brief Construct empty pipeline (passthrough)
     */
    TransformPipeline() = default;

    /**
     * @brief Construct with single transformer
     */
    explicit TransformPipeline(std::unique_ptr<ITransformer> transformer) {
        if (transformer) {
            stages_.push_back(std::move(transformer));
        }
    }

    /**
     * @brief Construct from list of transformers
     */
    explicit TransformPipeline(std::vector<std::unique_ptr<ITransformer>> stages)
        : stages_(std::move(stages)) {}

    /**
     * @brief Move constructor
     */
    TransformPipeline(TransformPipeline&&) noexcept = default;

    /**
     * @brief Move assignment
     */
    TransformPipeline& operator=(TransformPipeline&&) noexcept = default;

    // Disable copy (use clone() instead)
    TransformPipeline(const TransformPipeline&) = delete;
    TransformPipeline& operator=(const TransformPipeline&) = delete;

    // ========== Builder Pattern ==========

    /**
     * @brief Create a pipeline builder
     */
    static PipelineBuilder builder() { return PipelineBuilder{}; }

    /**
     * @brief Create pipeline with single transformer
     */
    template <typename T, typename... Args>
    static TransformPipeline single(Args&&... args) {
        static_assert(std::is_base_of_v<ITransformer, T>,
                      "T must derive from ITransformer");
        return TransformPipeline(std::make_unique<T>(std::forward<Args>(args)...));
    }

    // ========== ITransformer Implementation ==========

    /**
     * @brief Apply all transformations in forward order
     *
     * For pipeline [A, B, C]: output = C(B(A(input)))
     */
    Result<std::vector<uint8_t>> transform(std::span<const uint8_t> input) override {
        if (stages_.empty()) {
            return std::vector<uint8_t>(input.begin(), input.end());
        }

        std::vector<uint8_t> current(input.begin(), input.end());

        for (const auto& stage : stages_) {
            auto result = stage->transform(current);
            if (result.is_error()) {
                return result;
            }
            current = std::move(result.value());
        }

        return current;
    }

    /**
     * @brief Apply all transformations in reverse order
     *
     * For pipeline [A, B, C]: output = A⁻¹(B⁻¹(C⁻¹(input)))
     * This is the inverse of transform(), satisfying bijectivity.
     */
    Result<std::vector<uint8_t>> inverse(std::span<const uint8_t> input) override {
        if (stages_.empty()) {
            return std::vector<uint8_t>(input.begin(), input.end());
        }

        std::vector<uint8_t> current(input.begin(), input.end());

        // Apply inverse in reverse order
        for (auto it = stages_.rbegin(); it != stages_.rend(); ++it) {
            auto result = (*it)->inverse(current);
            if (result.is_error()) {
                return result;
            }
            current = std::move(result.value());
        }

        return current;
    }

    /**
     * @brief Get combined transformer ID
     *
     * Returns CUSTOM_START for pipelines with multiple stages.
     */
    TransformerId id() const noexcept override {
        if (stages_.empty()) {
            return TransformerId::NONE;
        }
        if (stages_.size() == 1) {
            return stages_[0]->id();
        }
        return TransformerId::CUSTOM_START;
    }

    std::string_view name() const noexcept override { return "pipeline"; }

    std::string description() const override {
        if (stages_.empty()) {
            return "empty-pipeline";
        }

        std::string desc = "pipeline[";
        for (size_t i = 0; i < stages_.size(); ++i) {
            if (i > 0) desc += " -> ";
            desc += stages_[i]->name();
        }
        desc += "]";
        return desc;
    }

    bool requires_key() const noexcept override {
        return std::any_of(stages_.begin(), stages_.end(),
                           [](const auto& s) { return s->requires_key(); });
    }

    bool has_header() const noexcept override {
        return std::any_of(stages_.begin(), stages_.end(),
                           [](const auto& s) { return s->has_header(); });
    }

    double max_expansion_ratio() const noexcept override {
        double ratio = 1.0;
        for (const auto& stage : stages_) {
            ratio *= stage->max_expansion_ratio();
        }
        return ratio;
    }

    size_t estimate_output_size(size_t input_size) const noexcept override {
        size_t size = input_size;
        for (const auto& stage : stages_) {
            size = stage->estimate_output_size(size);
        }
        return size;
    }

    std::unique_ptr<ITransformer> clone() const override {
        std::vector<std::unique_ptr<ITransformer>> cloned;
        cloned.reserve(stages_.size());
        for (const auto& stage : stages_) {
            cloned.push_back(stage->clone());
        }
        return std::make_unique<TransformPipeline>(std::move(cloned));
    }

    // ========== Pipeline-Specific Methods ==========

    /**
     * @brief Get number of stages in pipeline
     */
    size_t stage_count() const noexcept { return stages_.size(); }

    /**
     * @brief Check if pipeline is empty
     */
    bool empty() const noexcept { return stages_.empty(); }

    /**
     * @brief Get stage at index
     */
    const ITransformer* stage_at(size_t index) const noexcept {
        return index < stages_.size() ? stages_[index].get() : nullptr;
    }

    /**
     * @brief Get all stage IDs
     */
    std::vector<TransformerId> stage_ids() const {
        std::vector<TransformerId> ids;
        ids.reserve(stages_.size());
        for (const auto& stage : stages_) {
            ids.push_back(stage->id());
        }
        return ids;
    }

    /**
     * @brief Transform with statistics
     */
    Result<TransformResult<std::vector<uint8_t>>>
    transform_with_stats(std::span<const uint8_t> input) {
        auto start = std::chrono::steady_clock::now();

        auto result = transform(input);
        if (result.is_error()) {
            return result.error();
        }

        auto end = std::chrono::steady_clock::now();

        TransformStats stats;
        stats.input_size  = input.size();
        stats.output_size = result.value().size();
        stats.ratio       = stats.compression_ratio();
        stats.duration    = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        return TransformResult<std::vector<uint8_t>>(std::move(result.value()), stats);
    }

    /**
     * @brief Compose two pipelines
     *
     * Creates a new pipeline that first applies `first`, then `second`.
     */
    static TransformPipeline compose(TransformPipeline first, TransformPipeline second) {
        std::vector<std::unique_ptr<ITransformer>> combined;
        combined.reserve(first.stages_.size() + second.stages_.size());

        for (auto& stage : first.stages_) {
            combined.push_back(std::move(stage));
        }
        for (auto& stage : second.stages_) {
            combined.push_back(std::move(stage));
        }

        return TransformPipeline(std::move(combined));
    }

    /**
     * @brief Operator for composing pipelines
     */
    TransformPipeline operator|(TransformPipeline other) && {
        return compose(std::move(*this), std::move(other));
    }

private:
    std::vector<std::unique_ptr<ITransformer>> stages_;

    friend class PipelineBuilder;
};

// ============================================================================
// PIPELINE BUILDER IMPLEMENTATION (inline)
// ============================================================================

inline TransformPipeline PipelineBuilder::build() {
    return TransformPipeline(std::move(stages_));
}

inline std::unique_ptr<TransformPipeline> PipelineBuilder::build_unique() {
    return std::make_unique<TransformPipeline>(std::move(stages_));
}

// Note: compress() and encrypt() are implemented in transform_factory.hpp
// after the concrete transformers are defined.

// ============================================================================
// CONVENIENCE ALIASES
// ============================================================================

/**
 * @brief Shared pointer type for thread-safe sharing
 */
using TransformPipelinePtr = std::shared_ptr<TransformPipeline>;
using TransformerPtr       = std::unique_ptr<ITransformer>;

/**
 * @brief Create a shared pipeline
 */
template <typename... Args>
TransformPipelinePtr make_shared_pipeline(Args&&... args) {
    return std::make_shared<TransformPipeline>(std::forward<Args>(args)...);
}

}  // namespace ipb::transform
