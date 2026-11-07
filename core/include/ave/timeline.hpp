#pragma once

#include "ave/time.hpp"
#include "ave/uuid.hpp"
#include "ave/media_asset.hpp"

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <algorithm>

namespace ave {

enum class TrackType {
    Video,
    Audio
};

[[nodiscard]] std::string to_string(TrackType type);
[[nodiscard]] TrackType track_type_from_string(std::string_view str);

/**
 * @brief Represents an individual clip placement on a timeline track.
 */
class TimelineItem {
public:
    TimelineItem() = default;

    TimelineItem(std::string id,
                 std::string clip_id,
                 std::string asset_id,
                 std::string name,
                 Rational timeline_in,
                 Rational duration,
                 Rational source_in,
                 Rational speed = Rational{1, 1});

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& clip_id() const noexcept { return clip_id_; }
    [[nodiscard]] const std::string& asset_id() const noexcept { return asset_id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] Rational timeline_in() const noexcept { return timeline_in_; }
    [[nodiscard]] Rational duration() const noexcept { return duration_; }
    [[nodiscard]] Rational timeline_out() const noexcept { return timeline_in_ + duration_; }
    [[nodiscard]] Rational source_in() const noexcept { return source_in_; }
    [[nodiscard]] Rational speed() const noexcept { return speed_; }
    [[nodiscard]] Rational source_out() const noexcept { return source_in_ + (duration_ * speed_); }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    void set_name(std::string name) { name_ = std::move(name); }
    void set_timeline_in(Rational in_pos) { timeline_in_ = in_pos; }
    void set_duration(Rational dur);
    void set_source_in(Rational src_in) { source_in_ = src_in; }
    void set_speed(Rational spd);
    void set_enabled(bool en) noexcept { enabled_ = en; }

    [[nodiscard]] bool contains_time(Rational time) const noexcept {
        return time >= timeline_in_ && time < timeline_out();
    }

    [[nodiscard]] Rational source_time_at(Rational timeline_time) const;

    [[nodiscard]] bool operator==(const TimelineItem& other) const = default;

private:
    std::string id_{UUID::generate()};
    std::string clip_id_{};
    std::string asset_id_{};
    std::string name_{"Untitled Item"};
    Rational timeline_in_{0, 1};
    Rational duration_{0, 1};
    Rational source_in_{0, 1};
    Rational speed_{1, 1};
    bool enabled_{true};
};

/**
 * @brief Represents a single timeline track (e.g. V1, V2, A1, A2).
 */
class Track {
public:
    Track() = default;
    Track(std::string id, std::string name, TrackType type, int index);

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] TrackType type() const noexcept { return type_; }
    [[nodiscard]] int index() const noexcept { return index_; }
    [[nodiscard]] bool muted() const noexcept { return muted_; }
    [[nodiscard]] bool soloed() const noexcept { return soloed_; }
    [[nodiscard]] bool locked() const noexcept { return locked_; }
    [[nodiscard]] const std::vector<TimelineItem>& items() const noexcept { return items_; }

    void set_name(std::string name) { name_ = std::move(name); }
    void set_muted(bool m) noexcept { muted_ = m; }
    void set_soloed(bool s) noexcept { soloed_ = s; }
    void set_locked(bool l) noexcept { locked_ = l; }

    bool add_item(TimelineItem item);
    bool remove_item(const std::string& item_id);
    [[nodiscard]] const TimelineItem* item_at(Rational time) const noexcept;
    TimelineItem* item_at_mut(Rational time) noexcept;
    [[nodiscard]] const TimelineItem* find_item(const std::string& item_id) const noexcept;
    TimelineItem* find_item_mut(const std::string& item_id) noexcept;

    [[nodiscard]] Rational duration() const noexcept;

    [[nodiscard]] bool operator==(const Track& other) const = default;

private:
    std::string id_{UUID::generate()};
    std::string name_{"Track"};
    TrackType type_{TrackType::Video};
    int index_{0};
    bool muted_{false};
    bool soloed_{false};
    bool locked_{false};
    std::vector<TimelineItem> items_{};

    void sort_items();
};

/**
 * @brief Canonical editorial timeline holding multi-track sequences.
 */
class Timeline {
public:
    Timeline();
    Timeline(std::string id, std::string name, Rational frame_rate,
             int width = 1920, int height = 1080, int sample_rate = 48000);

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] Rational frame_rate() const noexcept { return frame_rate_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] int sample_rate() const noexcept { return sample_rate_; }

    [[nodiscard]] const std::vector<Track>& video_tracks() const noexcept { return video_tracks_; }
    [[nodiscard]] const std::vector<Track>& audio_tracks() const noexcept { return audio_tracks_; }

    Track& add_video_track(std::string name = "");
    Track& add_audio_track(std::string name = "");

    [[nodiscard]] const Track* find_track(const std::string& track_id) const noexcept;
    Track* find_track_mut(const std::string& track_id) noexcept;

    [[nodiscard]] Rational duration() const noexcept;

    [[nodiscard]] std::vector<const TimelineItem*> active_items_at(Rational time) const;

    /**
     * @brief Performs a blade cut splitting a clip into two independent timeline items.
     * @return pair of item IDs: {left_item_id, right_item_id}
     */
    std::pair<std::string, std::string> split_item(const std::string& track_id,
                                                   const std::string& item_id,
                                                   Rational split_time);

    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] static Timeline from_json(const std::string& json_text);

private:
    std::string id_{UUID::generate()};
    std::string name_{"Master Sequence"};
    Rational frame_rate_{24, 1};
    int width_{1920};
    int height_{1080};
    int sample_rate_{48000};
    std::vector<Track> video_tracks_{};
    std::vector<Track> audio_tracks_{};
};

} // namespace ave
