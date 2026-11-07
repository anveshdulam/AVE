#include "ave/media_cache.hpp"

#include <sstream>

namespace ave {

MediaCache::MediaCache(std::filesystem::path cache_dir)
    : cache_dir_(std::move(cache_dir)) {
    std::error_code ec;
    std::filesystem::create_directories(cache_dir_, ec);
}

std::string MediaCache::make_thumbnail_key(const std::string& asset_id, Rational timestamp, int width, int height) {
    std::ostringstream ss;
    ss << asset_id << "_" << timestamp.numerator() << "d" << timestamp.denominator() << "_" << width << "x" << height;
    return ss.str();
}

void MediaCache::store_thumbnail(const std::string& asset_id, Rational timestamp, int width, int height, ThumbnailData data) {
    std::scoped_lock lock(mutex_);
    std::string key = make_thumbnail_key(asset_id, timestamp, width, height);
    thumbnail_cache_[key] = std::move(data);
}

std::optional<ThumbnailData> MediaCache::get_thumbnail(const std::string& asset_id, Rational timestamp, int width, int height) const {
    std::scoped_lock lock(mutex_);
    std::string key = make_thumbnail_key(asset_id, timestamp, width, height);
    auto it = thumbnail_cache_.find(key);
    if (it != thumbnail_cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void MediaCache::store_waveform(const std::string& asset_id, WaveformData data) {
    std::scoped_lock lock(mutex_);
    waveform_cache_[asset_id] = std::move(data);
}

std::optional<WaveformData> MediaCache::get_waveform(const std::string& asset_id) const {
    std::scoped_lock lock(mutex_);
    auto it = waveform_cache_.find(asset_id);
    if (it != waveform_cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void MediaCache::clear() {
    std::scoped_lock lock(mutex_);
    thumbnail_cache_.clear();
    waveform_cache_.clear();
}

size_t MediaCache::thumbnail_count() const {
    std::scoped_lock lock(mutex_);
    return thumbnail_cache_.size();
}

size_t MediaCache::waveform_count() const {
    std::scoped_lock lock(mutex_);
    return waveform_cache_.size();
}

} // namespace ave
