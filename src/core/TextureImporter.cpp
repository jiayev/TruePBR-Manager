#include "TextureImporter.h"

#include <spdlog/spdlog.h>

namespace tpbr {

TextureEntry TextureImporter::importTexture(const std::filesystem::path& filePath, PBRTextureSlot slot)
{
    TextureEntry entry;
    entry.sourcePath = filePath;
    entry.slot       = slot;
    entry.format     = filePath.extension().string();

    // TODO: Read actual image metadata via DirectXTex (DDS) or stb_image (PNG/TGA)
    spdlog::info("Importing texture: {} -> slot {}", filePath.string(), slotDisplayName(slot));

    return entry;
}

bool TextureImporter::isPackedRMAOS(const std::filesystem::path& ddsPath)
{
    // TODO: Heuristic — check suffix or DDS header channel count
    auto filename = ddsPath.stem().string();
    return filename.ends_with("_rmaos") || filename.ends_with("_RMAOS");
}

const char* TextureImporter::fileFilter()
{
    return "Images (*.png *.dds *.tga *.bmp);;DDS Files (*.dds);;PNG Files (*.png);;All Files (*)";
}

} // namespace tpbr
