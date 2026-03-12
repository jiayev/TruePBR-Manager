#include "ChannelPacker.h"
#include "utils/DDSUtils.h"
#include "utils/FileUtils.h"
#include "utils/ImageUtils.h"

#include "utils/Log.h"

#include <algorithm>
#include <cstring>

namespace tpbr
{

static bool hasNonOpaqueAlpha(const std::vector<uint8_t>& rgba)
{
    for (size_t i = 3; i < rgba.size(); i += 4)
    {
        if (rgba[i] != 255)
        {
            return true;
        }
    }

    return false;
}

/// Load a single-channel greyscale image from any supported format.
/// For multi-channel images, extracts the R channel.
/// For DDS, decompresses first.
/// Returns pixel data resized to (targetW x targetH) if needed.
static bool loadGreyscale(const std::filesystem::path& path, int targetW, int targetH, std::vector<uint8_t>& outChannel)
{
    auto ext = FileUtils::getExtensionLower(path);

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;

    if (ext == ".dds")
    {
        if (!DDSUtils::loadDDS(path, w, h, rgba))
        {
            spdlog::error("ChannelPacker: failed to load DDS {}", path.string());
            return false;
        }
    }
    else
    {
        auto imgData = ImageUtils::loadImage(path);
        if (imgData.pixels.empty())
        {
            spdlog::error("ChannelPacker: failed to load image {}", path.string());
            return false;
        }
        w = imgData.width;
        h = imgData.height;
        rgba = std::move(imgData.pixels);
    }

    // Extract R channel from RGBA
    const size_t pixelCount = static_cast<size_t>(w) * h;
    std::vector<uint8_t> rChannel(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        rChannel[i] = rgba[i * 4]; // R component
    }

    // If target size matches or no resizing needed, use as-is
    if ((targetW == 0 && targetH == 0) || (w == targetW && h == targetH))
    {
        outChannel = std::move(rChannel);
        return true;
    }

    // Simple nearest-neighbor resize for channel data
    outChannel.resize(static_cast<size_t>(targetW) * targetH);
    for (int y = 0; y < targetH; ++y)
    {
        int srcY = y * h / targetH;
        for (int x = 0; x < targetW; ++x)
        {
            int srcX = x * w / targetW;
            outChannel[y * targetW + x] = rChannel[srcY * w + srcX];
        }
    }

    return true;
}

bool ChannelPacker::packRMAOS(const std::map<ChannelMap, std::filesystem::path>& channels,
                              const std::filesystem::path& outputPath, DDSCompressionMode compressionMode, int width,
                              int height)
{
    if (channels.empty())
    {
        spdlog::error("ChannelPacker: no channels provided");
        return false;
    }

    // Determine target resolution from the first available channel
    if (width == 0 || height == 0)
    {
        for (const auto& [ch, path] : channels)
        {
            auto ext = FileUtils::getExtensionLower(path);
            if (ext == ".dds")
            {
                DDSUtils::DDSInfo info;
                if (DDSUtils::getDDSInfo(path, info))
                {
                    width = info.width;
                    height = info.height;
                    break;
                }
            }
            else
            {
                int w, h, c;
                if (ImageUtils::getImageInfo(path, w, h, c))
                {
                    width = w;
                    height = h;
                    break;
                }
            }
        }
    }

    if (width <= 0 || height <= 0)
    {
        spdlog::error("ChannelPacker: could not determine target resolution");
        return false;
    }

    spdlog::info("ChannelPacker: packing RMAOS {}x{} -> {}", width, height, outputPath.string());

    const size_t pixelCount = static_cast<size_t>(width) * height;

    // Default values per True PBR spec:
    //   R = Roughness (default 1.0 = 255)
    //   G = Metallic  (default 0.0 = 0)
    //   B = AO        (default 1.0 = 255)
    //   A = Specular   (default ~0.04*255 ≈ 10, but 255 = full specular range)
    // Using sensible defaults for missing channels
    std::vector<uint8_t> roughness(pixelCount, 255); // fully rough
    std::vector<uint8_t> metallic(pixelCount, 0);    // non-metallic
    std::vector<uint8_t> ao(pixelCount, 255);        // no occlusion
    std::vector<uint8_t> specular(pixelCount, 255);  // full specular (0.04 is set via JSON param)

    // Load each provided channel
    auto loadChannel = [&](ChannelMap ch, std::vector<uint8_t>& target, const char* name)
    {
        auto it = channels.find(ch);
        if (it != channels.end())
        {
            if (!loadGreyscale(it->second, width, height, target))
            {
                spdlog::warn("ChannelPacker: failed to load {} channel, using default", name);
            }
            else
            {
                spdlog::debug("ChannelPacker: loaded {} from {}", name, it->second.filename().string());
            }
        }
    };

    loadChannel(ChannelMap::Roughness, roughness, "Roughness");
    loadChannel(ChannelMap::Metallic, metallic, "Metallic");
    loadChannel(ChannelMap::AO, ao, "AO");
    loadChannel(ChannelMap::Specular, specular, "Specular");

    // Compose RGBA: R=Roughness, G=Metallic, B=AO, A=Specular
    std::vector<uint8_t> rgba(pixelCount * 4);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        rgba[i * 4 + 0] = roughness[i];
        rgba[i * 4 + 1] = metallic[i];
        rgba[i * 4 + 2] = ao[i];
        rgba[i * 4 + 3] = specular[i];
    }

    if (compressionMode == DDSCompressionMode::BC1_Linear && hasNonOpaqueAlpha(rgba))
    {
        spdlog::warn("ChannelPacker: BC1 Linear requested but packed RMAOS contains non-opaque alpha, falling back to "
                     "BC7 Linear");
        compressionMode = DDSCompressionMode::BC7_Linear;
    }

    bool ok = false;
    switch (compressionMode)
    {
    case DDSCompressionMode::BC7_sRGB:
        ok = DDSUtils::saveDDS_BC7(outputPath, width, height, rgba.data(), true);
        break;
    case DDSCompressionMode::BC7_Linear:
        ok = DDSUtils::saveDDS_BC7(outputPath, width, height, rgba.data(), false);
        break;
    case DDSCompressionMode::BC6H_UF16:
        ok = DDSUtils::saveDDS_BC6H(outputPath, width, height, rgba.data());
        break;
    case DDSCompressionMode::BC5_Linear:
        ok = DDSUtils::saveDDS_BC5(outputPath, width, height, rgba.data());
        break;
    case DDSCompressionMode::BC4_Linear:
        ok = DDSUtils::saveDDS_BC4(outputPath, width, height, roughness.data());
        break;
    case DDSCompressionMode::BC1_sRGB:
        ok = DDSUtils::saveDDS_BC1(outputPath, width, height, rgba.data(), true);
        break;
    case DDSCompressionMode::BC1_Linear:
        ok = DDSUtils::saveDDS_BC1(outputPath, width, height, rgba.data(), false);
        break;
    case DDSCompressionMode::RGBA8_sRGB:
        ok = DDSUtils::saveDDS_RGBA(outputPath, width, height, rgba.data(), true);
        break;
    case DDSCompressionMode::RGBA8_Linear:
        ok = DDSUtils::saveDDS_RGBA(outputPath, width, height, rgba.data(), false);
        break;
    }

    if (!ok)
    {
        spdlog::error("ChannelPacker: failed to save RMAOS DDS");
        return false;
    }

    spdlog::info("ChannelPacker: RMAOS packed successfully -> {}", outputPath.string());
    return true;
}

} // namespace tpbr
