#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace tpbr
{

/// In-memory cache for decoded RGBA8 texture data.
///
/// Eliminates redundant disk reads and decompressions by caching the decoded
/// RGBA8 pixel data keyed by (canonicalPath, lastWriteTime). Every consumer
/// (thumbnails, 2D preview, 3D preview, export) shares the same cache.
///
/// Thread-safe: all public methods are guarded by a mutex.
class TextureCache
{
  public:
    /// Get the singleton instance.
    static TextureCache& instance();

    /// Cached texture data (RGBA8).
    struct Entry
    {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgbaPixels;
        bool isSRGB = false;
    };

    /// Load or retrieve cached RGBA8 pixel data for a texture file.
    /// Supports DDS (via DDSUtils) and raster images (via stb_image).
    /// Returns nullptr if the file cannot be loaded.
    const Entry* get(const std::filesystem::path& path);

    /// Evict a single path from the cache (e.g., after the file changes on disk).
    void evict(const std::filesystem::path& path);

    /// Clear all cached entries.
    void clear();

    /// Current number of cached entries.
    size_t size() const;

    /// Approximate total memory used by cached pixel data (in bytes).
    size_t memoryUsage() const;

  private:
    TextureCache() = default;
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    struct CacheKey
    {
        std::string canonicalPath;
        std::filesystem::file_time_type lastWriteTime;

        bool operator<(const CacheKey& rhs) const
        {
            if (canonicalPath != rhs.canonicalPath)
                return canonicalPath < rhs.canonicalPath;
            return lastWriteTime < rhs.lastWriteTime;
        }
    };

    mutable std::mutex m_mutex;
    std::map<CacheKey, Entry> m_cache;
};

} // namespace tpbr
