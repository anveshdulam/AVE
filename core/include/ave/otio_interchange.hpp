#pragma once

#include "ave/timeline.hpp"
#include "ave/media_pool.hpp"

#include <string>
#include <vector>

namespace ave {

/**
 * @brief Structured loss and warning report produced during OTIO interchange.
 */
struct OtioLossReport {
    std::vector<std::string> warnings{};
    std::vector<std::string> unsupported_features{};

    [[nodiscard]] bool has_warnings() const noexcept { return !warnings.empty(); }
    [[nodiscard]] bool has_loss() const noexcept { return !unsupported_features.empty(); }
};

/**
 * @brief OpenTimelineIO 1.0 JSON format exporter and importer.
 *
 * Implements non-destructive timeline interchange with DaVinci Resolve,
 * Final Cut Pro, and Premiere Pro via standard OTIO JSON representations.
 */
class OtioInterchange {
public:
    /**
     * @brief Exports native Timeline and MediaPool into an OpenTimelineIO (Timeline.1) JSON document.
     */
    [[nodiscard]] static std::string export_to_json(const Timeline& timeline,
                                                    const MediaPool& pool,
                                                    OtioLossReport& report);

    /**
     * @brief Imports an OpenTimelineIO (Timeline.1) JSON document into native Timeline and MediaPool.
     */
    static bool import_from_json(const std::string& otio_json,
                                 Timeline& timeline,
                                 MediaPool& pool,
                                 OtioLossReport& report);
};

} // namespace ave

