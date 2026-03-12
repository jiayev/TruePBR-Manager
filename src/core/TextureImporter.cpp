#include "TextureImporter.h"
#include "utils/DDSUtils.h"
#include "utils/FileUtils.h"
#include "utils/ImageUtils.h"

#include "utils/Log.h"

#include <algorithm>

namespace tpbr
{

static TextureAlphaMode alphaModeFromPixels(const std::vector<uint8_t>& rgbaPixels)
{
    if (rgbaPixels.empty())
    {
        return TextureAlphaMode::Unknown;
    }

    for (size_t i = 3; i < rgbaPixels.size(); i += 4)
    {
        if (rgbaPixels[i] != 255)
        {
            return TextureAlphaMode::Transparent;
        }
    }

    return TextureAlphaMode::Opaque;
}

static TextureAlphaMode detectDDSAlphaMode(const std::filesystem::path& filePath, const DDSUtils::DDSInfo& info)
{
    if (!info.hasAlpha)
    {
        return TextureAlphaMode::None;
    }

    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgbaPixels;
    if (!DDSUtils::loadDDS(filePath, width, height, rgbaPixels) || rgbaPixels.empty())
    {
        return TextureAlphaMode::Unknown;
    }

    return alphaModeFromPixels(rgbaPixels);
}

static TextureAlphaMode detectImageAlphaMode(const std::filesystem::path& filePath, int channels)
{
    if (channels != 2 && channels != 4)
    {
        return TextureAlphaMode::None;
    }

    const auto image = ImageUtils::loadImage(filePath);
    if (image.pixels.empty())
    {
        return TextureAlphaMode::Unknown;
    }

    return alphaModeFromPixels(image.pixels);
}

static const char* channelDisplayName(ChannelMap channel)
{
    switch (channel)
    {
    case ChannelMap::Roughness:
        return "Roughness";
    case ChannelMap::Metallic:
        return "Metallic";
    case ChannelMap::AO:
        return "AO";
    case ChannelMap::Specular:
        return "Specular";
    default:
        return "Unknown";
    }
}

TextureEntry TextureImporter::importTexture(const std::filesystem::path& filePath, PBRTextureSlot slot)
{
    TextureEntry entry;
    entry.sourcePath = filePath;
    entry.slot = slot;

    auto ext = FileUtils::getExtensionLower(filePath);
    entry.format = ext;

    if (ext == ".dds")
    {
        // Use DirectXTex to read DDS metadata
        DDSUtils::DDSInfo info;
        if (DDSUtils::getDDSInfo(filePath, info))
        {
            entry.width = info.width;
            entry.height = info.height;
            entry.channels = info.channels;
            entry.alphaMode = detectDDSAlphaMode(filePath, info);
            spdlog::info("Imported DDS: {} ({}x{}, {}) -> {}", filePath.filename().string(), info.width, info.height,
                         info.formatName, slotDisplayName(slot));
        }
        else
        {
            spdlog::warn("Failed to read DDS metadata: {}", filePath.string());
        }
    }
    else
    {
        // Use stb_image for PNG/TGA/BMP/JPG
        int w = 0, h = 0, c = 0;
        if (ImageUtils::getImageInfo(filePath, w, h, c))
        {
            entry.width = w;
            entry.height = h;
            entry.channels = c;
            entry.alphaMode = detectImageAlphaMode(filePath, c);
            spdlog::info("Imported image: {} ({}x{}, {}ch) -> {}", filePath.filename().string(), w, h, c,
                         slotDisplayName(slot));
        }
        else
        {
            spdlog::warn("Failed to read image metadata: {}", filePath.string());
        }
    }

    return entry;
}

ChannelMapEntry TextureImporter::importChannelMap(const std::filesystem::path& filePath, ChannelMap channel)
{
    ChannelMapEntry entry;
    entry.sourcePath = filePath;

    auto ext = FileUtils::getExtensionLower(filePath);
    entry.format = ext;

    if (ext == ".dds")
    {
        DDSUtils::DDSInfo info;
        if (DDSUtils::getDDSInfo(filePath, info))
        {
            entry.width = info.width;
            entry.height = info.height;
            entry.channels = info.channels;
            spdlog::info("Imported RMAOS channel: {} ({}x{}, {}) -> {}", filePath.filename().string(), info.width,
                         info.height, info.formatName, channelDisplayName(channel));
        }
        else
        {
            spdlog::warn("Failed to read DDS metadata: {}", filePath.string());
        }
    }
    else
    {
        int w = 0;
        int h = 0;
        int c = 0;
        if (ImageUtils::getImageInfo(filePath, w, h, c))
        {
            entry.width = w;
            entry.height = h;
            entry.channels = c;
            spdlog::info("Imported RMAOS channel: {} ({}x{}, {}ch) -> {}", filePath.filename().string(), w, h, c,
                         channelDisplayName(channel));
        }
        else
        {
            spdlog::warn("Failed to read image metadata: {}", filePath.string());
        }
    }

    return entry;
}

bool TextureImporter::isPackedRMAOS(const std::filesystem::path& ddsPath)
{
    // First check filename convention
    auto stem = ddsPath.stem().string();
    std::string stemLower = stem;
    std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(), ::tolower);

    if (stemLower.ends_with("_rmaos"))
        return true;

    // For DDS files, check if it's a 4-channel compressed texture (BC7/BC1/BC3)
    if (FileUtils::getExtensionLower(ddsPath) == ".dds")
    {
        DDSUtils::DDSInfo info;
        if (DDSUtils::getDDSInfo(ddsPath, info))
        {
            return info.channels == 4;
        }
    }

    return false;
}

const char* TextureImporter::fileFilter()
{
    return "Images (*.png *.dds *.tga *.bmp);;DDS Files (*.dds);;PNG Files (*.png);;All Files (*)";
}

} // namespace tpbr
