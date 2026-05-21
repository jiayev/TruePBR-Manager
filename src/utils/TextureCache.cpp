#include "TextureCache.h"

#include "utils/DDSUtils.h"
#include "utils/FileUtils.h"
#include "utils/ImageUtils.h"
#include "utils/Log.h"

namespace tpbr
{

TextureCache& TextureCache::instance()
{
    static TextureCache s_instance;
    return s_instance;
}

const TextureCache::Entry* TextureCache::get(const std::filesystem::path& path)
{
    if (path.empty() || !std::filesystem::exists(path))
        return nullptr;

    std::error_code ec;
    const auto canonical = std::filesystem::canonical(path, ec);
    if (ec)
        return nullptr;

    const auto writeTime = std::filesystem::last_write_time(canonical, ec);
    if (ec)
        return nullptr;

    CacheKey key{canonical.string(), writeTime};

    std::lock_guard<std::mutex> lock(m_mutex);

    // Check cache hit
    auto it = m_cache.find(key);
    if (it != m_cache.end())
    {
        spdlog::debug("TextureCache: hit {} ({}x{})", path.filename().string(), it->second.width, it->second.height);
        return &it->second;
    }

    // Cache miss — load from disk
    Entry entry;
    const auto ext = FileUtils::getExtensionLower(path);

    if (ext == ".dds")
    {
        bool isSRGB = false;
        if (!DDSUtils::loadDDS(canonical, entry.width, entry.height, entry.rgbaPixels, &isSRGB))
        {
            spdlog::warn("TextureCache: failed to load DDS {}", path.string());
            return nullptr;
        }
        entry.isSRGB = isSRGB;
    }
    else
    {
        auto imgData = ImageUtils::loadImage(canonical);
        if (imgData.pixels.empty())
        {
            spdlog::warn("TextureCache: failed to load image {}", path.string());
            return nullptr;
        }
        entry.width = imgData.width;
        entry.height = imgData.height;
        entry.rgbaPixels = std::move(imgData.pixels);
        entry.isSRGB = false;
    }

    spdlog::debug("TextureCache: miss, loaded {} ({}x{}, {} bytes)", path.filename().string(), entry.width,
                  entry.height, entry.rgbaPixels.size());

    auto [insertIt, _] = m_cache.emplace(std::move(key), std::move(entry));
    return &insertIt->second;
}

void TextureCache::evict(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto canonical = std::filesystem::canonical(path, ec);
    if (ec)
        return;

    const std::string canonicalStr = canonical.string();

    std::lock_guard<std::mutex> lock(m_mutex);

    // Erase all entries with this canonical path (regardless of write time)
    for (auto it = m_cache.begin(); it != m_cache.end();)
    {
        if (it->first.canonicalPath == canonicalStr)
            it = m_cache.erase(it);
        else
            ++it;
    }
}

void TextureCache::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
}

size_t TextureCache::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cache.size();
}

size_t TextureCache::memoryUsage() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t total = 0;
    for (const auto& [key, entry] : m_cache)
    {
        total += entry.rgbaPixels.size();
    }
    return total;
}

} // namespace tpbr
