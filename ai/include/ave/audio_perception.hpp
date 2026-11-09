#pragma once

#include <ave/model_runtime.hpp>
#include <ave/perception_report.hpp>
#include <ave/time.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ave {

struct AudioPerceptionConfig {
    int sample_rate = 22050;                // Standard sample rate for Beat This! (22.05 kHz)
    int hop_size = 441;                     // 441 samples = 20 ms hop size (50 fps resolution)
    double onset_threshold = 0.30;          // Spectral flux peak detection threshold
    double default_tempo_bpm = 120.0;       // Fallback tempo if unmeasured
    double speech_energy_threshold = 0.05;  // VAD energy threshold for speech vs silence
};

/**
 * @brief Fast spectral-flux transient detector and EBU R128 integrated loudness estimator.
 */
class SpectralFluxOnsetDetector {
public:
    explicit SpectralFluxOnsetDetector(int sample_rate = 22050, int hop_size = 441);

    /**
     * @brief Computes integrated loudness in LUFS from 16-bit PCM mono samples.
     */
    [[nodiscard]] double compute_loudness_lufs(const std::vector<int16_t>& pcm_samples) const;

    /**
     * @brief Detects acoustic transient peaks (drum attacks, percussive hits).
     * @return List of exact rational timestamps of transient onsets.
     */
    [[nodiscard]] std::vector<Rational> detect_onsets(
        const std::vector<int16_t>& pcm_samples,
        double threshold = 0.30,
        std::vector<std::pair<Rational, double>>* out_flux_curve = nullptr
    ) const;

private:
    int m_sample_rate;
    int m_hop_size;
};

/**
 * @brief Musical beat and rhythm tracker with sub-millisecond transient refinement.
 */
class BeatDetector {
public:
    explicit BeatDetector(
        std::shared_ptr<IModelRunner> beat_runner = nullptr,
        AudioPerceptionConfig config = {}
    );

    [[nodiscard]] const AudioPerceptionConfig& config() const noexcept { return m_config; }

    /**
     * @brief Tracks musical beats, downbeats, and tempo curve from mono audio.
     */
    [[nodiscard]] AudioRhythmReport process(
        const std::vector<int16_t>& pcm_samples,
        std::vector<std::pair<Rational, double>>* out_beat_curve = nullptr
    ) const;

private:
    std::shared_ptr<IModelRunner> m_runner;
    AudioPerceptionConfig m_config;
    SpectralFluxOnsetDetector m_onset_detector;

    [[nodiscard]] std::vector<BeatMarker> refine_beats_with_transients(
        const std::vector<BeatMarker>& coarse_beats,
        const std::vector<Rational>& onsets
    ) const;
};

/**
 * @brief Voice Activity Detection (VAD) and word-level speech timestamp processor.
 */
class SpeechDetector {
public:
    explicit SpeechDetector(
        std::shared_ptr<IModelRunner> whisper_runner = nullptr,
        AudioPerceptionConfig config = {}
    );

    /**
     * @brief Detects speech intervals and word boundaries from audio.
     */
    [[nodiscard]] SpeechReport process(const std::vector<int16_t>& pcm_samples) const;

private:
    std::shared_ptr<IModelRunner> m_runner;
    AudioPerceptionConfig m_config;
};

/**
 * @brief Unified audio perception pipeline populating rhythm, beats, loudness, and speech.
 */
class AudioPerceptionPipeline {
public:
    explicit AudioPerceptionPipeline(
        std::shared_ptr<IModelRunner> beat_runner = nullptr,
        std::shared_ptr<IModelRunner> whisper_runner = nullptr,
        AudioPerceptionConfig config = {}
    );

    /**
     * @brief Executes complete audio analysis and populates PerceptionReport.
     */
    bool analyze(const std::vector<int16_t>& pcm_samples, PerceptionReport& out_report) const;

private:
    BeatDetector m_beat_detector;
    SpeechDetector m_speech_detector;
    SpectralFluxOnsetDetector m_onset_detector;
};

} // namespace ave
