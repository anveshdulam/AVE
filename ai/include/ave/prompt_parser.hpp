#pragma once

#include <ave/deterministic_planner.hpp>
#include <ave/language_sidecar.hpp>
#include <ave/reference_fingerprint.hpp>
#include <ave/time.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ave {

enum class PacingIntent {
    Fast,       // 0.5s - 1.5s shots, high cut frequency
    Medium,     // 2.0s - 3.5s shots, standard pacing
    Slow,       // 4.0s - 8.0s shots, relaxed / cinematic
    Dynamic     // Bursty montages alternating with long takes
};

/**
 * @brief Structured constraints and tuning parameters extracted from a natural language user prompt.
 */
struct ParsedPromptConstraints {
    std::string original_prompt;
    std::optional<Rational> target_duration;
    PacingIntent pacing = PacingIntent::Medium;
    bool sync_to_beats = true;
    bool prioritize_downbeats = false;
    double target_motion_energy = 0.35; // [0.0, 1.0]
    std::vector<std::string> visual_keywords;
    std::vector<std::string> negative_keywords;
    bool preserve_speech = true;
    double confidence = 1.0;

    /**
     * @brief Converts constraints into concrete PlannerConfig weights.
     */
    [[nodiscard]] PlannerConfig to_planner_config(const PlannerConfig& base_config = {}) const;

    /**
     * @brief Synthesizes an optimized ReferenceFingerprint matching user intent.
     */
    [[nodiscard]] ReferenceFingerprint to_reference_fingerprint(const ReferenceFingerprint& base_ref = {}) const;
};

/**
 * @brief Translates natural language user editing prompts into structured planner constraints.
 *
 * Emits constraints and weights, NEVER arbitrary raw commands.
 * Completely immune to prompt injection attacks from untrusted video transcripts or metadata.
 */
class PromptParser {
public:
    explicit PromptParser(std::shared_ptr<LanguageSidecar> sidecar = nullptr);

    /**
     * @brief Parses user prompt into structured constraints.
     */
    [[nodiscard]] ParsedPromptConstraints parse(const std::string& user_prompt) const;

private:
    std::shared_ptr<LanguageSidecar> m_sidecar;

    [[nodiscard]] ParsedPromptConstraints parse_rule_based(const std::string& text) const;
};

} // namespace ave

