#include "ave/timeline_viewport.hpp"

namespace ave {

TimelineViewport::TimelineViewport(double pixels_per_second,
                                   double visible_width,
                                   double visible_height)
    : pixels_per_second_(std::clamp(pixels_per_second, 10.0, 2000.0)),
      visible_width_(std::max(100.0, visible_width)),
      visible_height_(std::max(100.0, visible_height)) {}

void TimelineViewport::set_pixels_per_second(double pps) {
    pixels_per_second_ = std::clamp(pps, 10.0, 2000.0);
}

void TimelineViewport::set_scroll_offset_x(double offset_x) noexcept {
    scroll_offset_x_ = std::max(0.0, offset_x);
}

void TimelineViewport::set_scroll_offset_y(double offset_y) noexcept {
    scroll_offset_y_ = std::max(0.0, offset_y);
}

void TimelineViewport::set_viewport_size(double width, double height) noexcept {
    visible_width_ = std::max(100.0, width);
    visible_height_ = std::max(100.0, height);
}

double TimelineViewport::time_to_pixel(Rational time) const noexcept {
    double secs = time.to_seconds();
    return (secs * pixels_per_second_) - scroll_offset_x_;
}

Rational TimelineViewport::pixel_to_time(double pixel, Rational frame_rate) const {
    const double canvas_px = pixel + scroll_offset_x_;
    const Rational effective_fps =
        (frame_rate.numerator() > 0 && frame_rate.denominator() > 0)
            ? frame_rate
            : Rational{24, 1};

    if (pixels_per_second_ <= 0.0 || !std::isfinite(pixels_per_second_)) {
        return Rational{0, 1};
    }

    const double secs = canvas_px / pixels_per_second_;
    return ui_seconds_to_rational_pts_safe(secs, effective_fps).pts;
}

std::pair<Rational, Rational> TimelineViewport::visible_time_range(Rational frame_rate) const {
    Rational start = pixel_to_time(0.0, frame_rate);
    Rational end = pixel_to_time(visible_width_, frame_rate);
    return {start, end};
}

double TimelineViewport::track_y(size_t track_index, double track_height, double track_spacing) const noexcept {
    double y = static_cast<double>(track_index) * (track_height + track_spacing);
    return y - scroll_offset_y_;
}

void TimelineViewport::zoom_at(double pivot_pixel, double zoom_multiplier) {
    if (zoom_multiplier <= 0.0) return;

    double time_sec = (pivot_pixel + scroll_offset_x_) / pixels_per_second_;
    double new_pps = std::clamp(pixels_per_second_ * zoom_multiplier, 10.0, 2000.0);

    double new_scroll = (time_sec * new_pps) - pivot_pixel;
    pixels_per_second_ = new_pps;
    scroll_offset_x_ = std::max(0.0, new_scroll);
}

void TimelineViewport::pan(double delta_x, double delta_y) noexcept {
    scroll_offset_x_ = std::max(0.0, scroll_offset_x_ - delta_x);
    scroll_offset_y_ = std::max(0.0, scroll_offset_y_ - delta_y);
}

} // namespace ave
