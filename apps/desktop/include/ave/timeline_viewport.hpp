#pragma once

#include "ave/time.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace ave {

/**
 * @brief Manages 2D viewport coordinates, time-to-pixel projection,
 * zoom factor, and virtual scrolling for the multi-track timeline.
 */
class TimelineViewport {
public:
    explicit TimelineViewport(double pixels_per_second = 100.0,
                              double visible_width = 1920.0,
                              double visible_height = 400.0);

    [[nodiscard]] double pixels_per_second() const noexcept { return pixels_per_second_; }
    void set_pixels_per_second(double pps);

    [[nodiscard]] double scroll_offset_x() const noexcept { return scroll_offset_x_; }
    [[nodiscard]] double scroll_offset_y() const noexcept { return scroll_offset_y_; }
    void set_scroll_offset_x(double offset_x) noexcept;
    void set_scroll_offset_y(double offset_y) noexcept;

    [[nodiscard]] double visible_width() const noexcept { return visible_width_; }
    [[nodiscard]] double visible_height() const noexcept { return visible_height_; }
    void set_viewport_size(double width, double height) noexcept;

    /**
     * @brief Projects a rational timestamp into horizontal canvas pixel coordinate.
     */
    [[nodiscard]] double time_to_pixel(Rational time) const noexcept;

    /**
     * @brief Inverses horizontal canvas pixel coordinate into a rational timestamp,
     * quantized to the given frame rate.
     */
    [[nodiscard]] Rational pixel_to_time(double pixel, Rational frame_rate) const;

    /**
     * @brief Computes the visible rational time range [start_time, end_time].
     */
    [[nodiscard]] std::pair<Rational, Rational> visible_time_range(Rational frame_rate) const;

    /**
     * @brief Calculates vertical track lane top position in pixels.
     */
    [[nodiscard]] double track_y(size_t track_index, double track_height = 64.0, double track_spacing = 4.0) const noexcept;

    /**
     * @brief Zooms in or out centered at a specific pivot pixel (e.g. mouse cursor or playhead).
     */
    void zoom_at(double pivot_pixel, double zoom_multiplier);

    /**
     * @brief Pans viewport by pixel delta.
     */
    void pan(double delta_x, double delta_y) noexcept;

private:
    double pixels_per_second_{100.0};
    double scroll_offset_x_{0.0};
    double scroll_offset_y_{0.0};
    double visible_width_{1920.0};
    double visible_height_{400.0};
};

} // namespace ave
