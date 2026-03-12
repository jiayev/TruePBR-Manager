#include "ChannelPacker.h"

#include <spdlog/spdlog.h>

namespace tpbr {

bool ChannelPacker::packRMAOS(
    const std::map<ChannelMap, std::filesystem::path>& channels,
    const std::filesystem::path& outputPath,
    int width,
    int height)
{
    // TODO: Implement using DirectXTex
    // 1. Load each channel image (greyscale)
    // 2. Create RGBA image: R=Roughness, G=Metallic, B=AO, A=Specular
    // 3. Compress to BC1 (no specular) or BC7 (with specular)
    // 4. Save as DDS

    spdlog::info("Packing RMAOS -> {}", outputPath.string());

    for (const auto& [ch, path] : channels) {
        const char* chName = "";
        switch (ch) {
        case ChannelMap::Roughness: chName = "Roughness"; break;
        case ChannelMap::Metallic:  chName = "Metallic"; break;
        case ChannelMap::AO:        chName = "AO"; break;
        case ChannelMap::Specular:  chName = "Specular"; break;
        }
        spdlog::info("  Channel {}: {}", chName, path.string());
    }

    return true;
}

} // namespace tpbr
