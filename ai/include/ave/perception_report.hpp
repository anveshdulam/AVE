#pragma once

#include <ave/time.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ave {

enum class ShotTransitionType {
    Cut,
    Dissolve,
    FadeIn,
    FadeOut,
    Unknown
};

[[nodiscard]] std::string shot_transition_type_to_string(ShotTransitionType type);
[[nodiscard]] ShotTransitionType string_to_shot_transition_type(const std::string& str);

struct ShotBoundary {
    Rational start_pts;
    Rational end_pts;
    ShotTransitionType type = ShotTransitionType::Cut;
    double confidence = 1.0;
    std::vector<Rational> keyframes;

    [[nodiscard]] Rational duration() const noexcept {
        return end_pts - start_pts;
    }
};

struct BeatMarker {
    Rational pts;
    bool is_downbeat = false;
    double confidence = 1.0;
};

struct TempoPoint {
    Rational pts;
    double bpm = 120.0;
};

struct AudioSection {
    Rational start_pts;
    Rational end_pts;
    std::string label;
};

struct AudioRhythmReport {
    std::vector<BeatMarker> beats;
    double primary_tempo_bpm = 120.0;
    std::vector<double> alternate_tempos; // e.g. half/double tempo
    std::vector<TempoPoint> tempo_curve;
    std::vector<AudioSection> sections;
    double integrated_loudness_lufs = -14.0;
};

struct SpeechSegment {
    Rational start_pts;
    Rational end_pts;
    double confidence = 1.0;
};

struct WordTimestamp {
    std::string word;
    Rational start_pts;
    Rational end_pts;
    double confidence = 1.0;
};

struct SpeechReport {
    std::vector<SpeechSegment> segments;
    std::vector<WordTimestamp> words;
};

struct ColorLook {
    double lab_l = 50.0;
    double lab_a = 0.0;
    double lab_b = 0.0;
    double contrast = 1.0;
};

struct VisualReport {
    double average_motion_energy = 0.0;
    std::vector<std::pair<Rational, double>> motion_curve;
    ColorLook color_look;
};

struct RawProbabilityCurve {
    std::string name;
    std::vector<std::pair<Rational, double>> points;
};

/**
 * @brief Canonical, versioned structured report of local video and audio perception.
 *
 * All timestamps strictly use exact 64-bit ave::Rational fractions.
 * Floats stop at the perception boundary.
 */
class PerceptionReport {
public:
    PerceptionReport() = default;

    int schema_version = 1;
    std::string asset_id;
    std::string asset_hash;
    Rational duration;
    Rational time_base = Rational(1, 1000);

    std::vector<ShotBoundary> shots;
    AudioRhythmReport rhythm;
    SpeechReport speech;
    VisualReport visual;
    std::vector<RawProbabilityCurve> raw_curves;

    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] static PerceptionReport from_json(const std::string& json_str);

    [[nodiscard]] const RawProbabilityCurve* find_raw_curve(const std::string& name) const;
    void add_raw_curve(std::string name, std::vector<std::pair<Rational, double>> points);
};

} // namespace ave
