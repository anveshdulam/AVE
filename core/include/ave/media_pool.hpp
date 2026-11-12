#pragma once

#include "ave/media_asset.hpp"
#include "ave/time.hpp"
#include "ave/uuid.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ave {

/**
 * @brief Editorial clip reference pointing to an underlying MediaAsset.
 *
 * All cuts, in/out sub-ranges, and metadata annotations are non-destructive
 * and do not modify the original source media.
 */
class MediaClip {
public:
    MediaClip() = default;

    MediaClip(std::string id,
              std::string asset_id,
              std::string name,
              Rational source_in,
              Rational source_out);

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& asset_id() const noexcept { return asset_id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] Rational source_in() const noexcept { return source_in_; }
    [[nodiscard]] Rational source_out() const noexcept { return source_out_; }
    [[nodiscard]] Rational duration() const noexcept { return source_out_ - source_in_; }
    [[nodiscard]] const std::string& color_tag() const noexcept { return color_tag_; }

    void set_name(std::string name) { name_ = std::move(name); }
    void set_source_range(Rational in_point, Rational out_point);
    void set_color_tag(std::string color) { color_tag_ = std::move(color); }

    [[nodiscard]] bool operator==(const MediaClip& other) const = default;

private:
    std::string id_{UUID::generate()};
    std::string asset_id_{};
    std::string name_{"Untitled Clip"};
    Rational source_in_{0, 1};
    Rational source_out_{0, 1};
    std::string color_tag_{"default"};
};

/**
 * @brief Organizational bin (folder) in the Media Pool.
 */
class MediaBin {
public:
    MediaBin() = default;
    explicit MediaBin(std::string id, std::string name, std::string parent_id = "");

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& parent_id() const noexcept { return parent_id_; }
    [[nodiscard]] const std::vector<std::string>& clip_ids() const noexcept { return clip_ids_; }
    [[nodiscard]] const std::vector<std::string>& child_bin_ids() const noexcept { return child_bin_ids_; }

    void set_name(std::string name) { name_ = std::move(name); }
    void set_parent_id(std::string parent_id) { parent_id_ = std::move(parent_id); }
    void add_clip(const std::string& clip_id);
    bool remove_clip(const std::string& clip_id);
    void add_child_bin(const std::string& bin_id);
    bool remove_child_bin(const std::string& bin_id);

    [[nodiscard]] bool operator==(const MediaBin& other) const = default;

private:
    std::string id_{UUID::generate()};
    std::string name_{"Master"};
    std::string parent_id_{};
    std::vector<std::string> clip_ids_{};
    std::vector<std::string> child_bin_ids_{};
};

/**
 * @brief Central repository for media assets, clips, and bin hierarchies.
 */
class MediaPool {
public:
    using ChangeListener = std::function<void()>;
    using ListenerId = uint64_t;

    MediaPool();
    MediaPool(const MediaPool& other);
    MediaPool& operator=(const MediaPool& other);
    MediaPool(MediaPool&& other) noexcept;
    MediaPool& operator=(MediaPool&& other) noexcept;
    ~MediaPool() = default;

    // Asset Management
    const MediaAsset& register_asset(MediaAsset asset);
    [[nodiscard]] const MediaAsset* find_asset(const std::string& asset_id) const noexcept;
    MediaAsset* find_asset_mut(const std::string& asset_id) noexcept;
    bool remove_asset(const std::string& asset_id);
    bool relink_asset_path(const std::string& asset_id,
                           const std::filesystem::path& new_path,
                           std::optional<uint64_t> file_size_bytes = std::nullopt);
    [[nodiscard]] size_t asset_count() const noexcept { return assets_.size(); }
    [[nodiscard]] const std::unordered_map<std::string, MediaAsset>& assets() const noexcept { return assets_; }

    // Bin Management
    [[nodiscard]] const std::string& root_bin_id() const noexcept { return root_bin_id_; }
    std::string create_bin(const std::string& name, const std::string& parent_bin_id = "");
    [[nodiscard]] const MediaBin* find_bin(const std::string& bin_id) const noexcept;
    MediaBin* find_bin_mut(const std::string& bin_id) noexcept;
    bool rename_bin(const std::string& bin_id, const std::string& new_name);
    bool move_bin(const std::string& bin_id, const std::string& new_parent_bin_id);
    bool remove_bin(const std::string& bin_id);

    // Clip Management
    std::string create_clip(const std::string& asset_id,
                            const std::string& target_bin_id = "",
                            std::optional<std::string> custom_name = std::nullopt);
    [[nodiscard]] const MediaClip* find_clip(const std::string& clip_id) const noexcept;
    MediaClip* find_clip_mut(const std::string& clip_id) noexcept;
    bool rename_clip(const std::string& clip_id, const std::string& new_name);
    bool set_clip_source_range(const std::string& clip_id, Rational in_point, Rational out_point);
    bool set_clip_color_tag(const std::string& clip_id, const std::string& color_tag);
    bool move_clip(const std::string& clip_id, const std::string& destination_bin_id);
    bool remove_clip(const std::string& clip_id);

    // Search & Query
    [[nodiscard]] std::vector<const MediaClip*> search_clips(const std::string& query) const;

    // Mutation tracking & Change Listeners
    [[nodiscard]] uint64_t mutation_revision() const noexcept { return mutation_revision_; }
    [[nodiscard]] uint64_t content_digest() const noexcept;
    ListenerId add_change_listener_with_id(ChangeListener listener);
    void remove_change_listener(ListenerId id);

    // Serialization
    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] static MediaPool from_json(const std::string& json_text);

    [[nodiscard]] bool operator==(const MediaPool& other) const;

private:
    struct ListenerEntry {
        ListenerId id{0};
        ChangeListener callback;
    };

    void bump_mutation();
    void notify_listeners();

    std::string root_bin_id_{"root"};
    std::unordered_map<std::string, MediaAsset> assets_{};
    std::unordered_map<std::string, MediaBin> bins_{};
    std::unordered_map<std::string, MediaClip> clips_{};
    uint64_t mutation_revision_{0};
    std::vector<ListenerEntry> listeners_{};
};

} // namespace ave

