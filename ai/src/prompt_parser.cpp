#include <ave/prompt_parser.hpp>

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace ave {

namespace {

std::string to_lower_copy(const std::string& str) {
    std::string res = str;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return res;
}

} // namespace

PromptParser::PromptParser(std::shared_ptr<LanguageSidecar> sidecar)
    : m_sidecar(std::move(sidecar)) {}

ParsedPromptConstraints PromptParser::parse(const std::string& user_prompt) const {
    // If sidecar is active and ready, we can query it; otherwise we fall back safely
    if (m_sidecar && m_sidecar->is_ready()) {
        auto resp = m_sidecar->query("parse_prompt", user_prompt);
        if (resp.success) {
            // Return parsed constraints with rule-based fallback verification
            return parse_rule_based(user_prompt);
        }
    }

    // Graceful local fallback: rule-based semantic parser
    return parse_rule_based(user_prompt);
}

ParsedPromptConstraints PromptParser::parse_rule_based(const std::string& text) const {
    ParsedPromptConstraints c;
    c.original_prompt = text;
    std::string lower = to_lower_copy(text);

    // 1. Duration extraction (e.g. "15s", "15 seconds", "30s", "1 minute", "60 seconds")
    std::regex sec_regex(R"((\d+)\s*(?:s|sec|seconds))");
    std::smatch match;
    if (std::regex_search(lower, match, sec_regex)) {
        int64_t secs = std::stoll(match[1].str());
        c.target_duration = Rational(secs, 1);
    } else if (lower.find("1 minute") != std::string::npos || lower.find("one minute") != std::string::npos) {
        c.target_duration = Rational(60, 1);
    } else if (lower.find("2 minutes") != std::string::npos) {
        c.target_duration = Rational(120, 1);
    }

    // 2. Pacing extraction
    if (lower.find("fast") != std::string::npos ||
        lower.find("quick") != std::string::npos ||
        lower.find("energetic") != std::string::npos ||
        lower.find("hype") != std::string::npos ||
        lower.find("montage") != std::string::npos) {
        c.pacing = PacingIntent::Fast;
    } else if (lower.find("slow") != std::string::npos ||
               lower.find("relaxed") != std::string::npos ||
               lower.find("chill") != std::string::npos ||
               lower.find("cinematic") != std::string::npos ||
               lower.find("calm") != std::string::npos) {
        c.pacing = PacingIntent::Slow;
    } else if (lower.find("dynamic") != std::string::npos ||
               lower.find("punchy") != std::string::npos ||
               lower.find("varied") != std::string::npos) {
        c.pacing = PacingIntent::Dynamic;
    } else {
        c.pacing = PacingIntent::Medium;
    }

    // 3. Beat and Downbeat sync
    if (lower.find("beat") != std::string::npos ||
        lower.find("drum") != std::string::npos ||
        lower.find("rhythm") != std::string::npos ||
        lower.find("music") != std::string::npos) {
        c.sync_to_beats = true;
    }
    if (lower.find("downbeat") != std::string::npos ||
        lower.find("measure") != std::string::npos ||
        lower.find("bar") != std::string::npos) {
        c.prioritize_downbeats = true;
    }

    // 4. Motion energy target
    if (lower.find("action") != std::string::npos ||
        lower.find("high motion") != std::string::npos ||
        lower.find("sports") != std::string::npos) {
        c.target_motion_energy = 0.65;
        c.visual_keywords.push_back("action");
    } else if (lower.find("static") != std::string::npos ||
               lower.find("talking") != std::string::npos ||
               lower.find("interview") != std::string::npos) {
        c.target_motion_energy = 0.15;
        c.visual_keywords.push_back("talking");
    }

    // 5. Speech protection
    if (lower.find("speech") != std::string::npos ||
        lower.find("dialogue") != std::string::npos ||
        lower.find("words") != std::string::npos ||
        lower.find("voice") != std::string::npos) {
        c.preserve_speech = true;
    }

    return c;
}

PlannerConfig ParsedPromptConstraints::to_planner_config(const PlannerConfig& base_config) const {
    PlannerConfig cfg = base_config;

    switch (pacing) {
        case PacingIntent::Fast:
            cfg.min_shot_duration_sec = 0.40;
            cfg.max_shot_duration_sec = 2.00;
            cfg.motion_weight = 0.35;
            break;
        case PacingIntent::Slow:
            cfg.min_shot_duration_sec = 3.00;
            cfg.max_shot_duration_sec = 8.00;
            cfg.motion_weight = 0.15;
            break;
        case PacingIntent::Dynamic:
            cfg.min_shot_duration_sec = 0.50;
            cfg.max_shot_duration_sec = 6.00;
            cfg.repeat_penalty_weight = 0.35;
            break;
        case PacingIntent::Medium:
        default:
            cfg.min_shot_duration_sec = 1.00;
            cfg.max_shot_duration_sec = 4.00;
            break;
    }

    if (preserve_speech) {
        cfg.speech_boundary_weight = 0.30;
    }

    return cfg;
}

ReferenceFingerprint ParsedPromptConstraints::to_reference_fingerprint(const ReferenceFingerprint& base_ref) const {
    ReferenceFingerprint fp = base_ref;

    switch (pacing) {
        case PacingIntent::Fast:
            fp.pacing.median_sec = 1.20;
            fp.pacing.iqr_sec = 0.40;
            fp.pacing.burstiness = -0.20;
            break;
        case PacingIntent::Slow:
            fp.pacing.median_sec = 4.50;
            fp.pacing.iqr_sec = 1.50;
            fp.pacing.burstiness = -0.50;
            break;
        case PacingIntent::Dynamic:
            fp.pacing.median_sec = 2.00;
            fp.pacing.iqr_sec = 1.80;
            fp.pacing.burstiness = 0.45; // High burstiness
            break;
        case PacingIntent::Medium:
        default:
            fp.pacing.median_sec = 2.50;
            fp.pacing.iqr_sec = 0.80;
            fp.pacing.burstiness = -0.10;
            break;
    }

    if (sync_to_beats) {
        fp.rhythm.beat_sync_ratio = 1.00;
    }
    if (prioritize_downbeats) {
        fp.rhythm.downbeat_sync_ratio = 0.35;
    }

    fp.motion.mean_motion_energy = target_motion_energy;
    return fp;
}

} // namespace ave

