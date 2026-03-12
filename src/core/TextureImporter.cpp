#include "TextureImporter.h"
#include "utils/DDSUtils.h"
#include "utils/FileUtils.h"
#include "utils/ImageUtils.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace tpbr {

TextureEntry TextureImporter::importTexture(const std::filesystem::path& filePath, PBRTextureSlot slot)
{
    TextureEntry entry;
    entry.sourcePath = filePath;
    entry.slot       = slot;

    auto ext = FileUtils::getExtensionLower(filePath);
    entry.format = ext;

    if (ext == ".dds") {
        // Use DirectXTex to read DDS metadata
        DDSUtils::DDSInfo info;
        if (DDSUtils::getDDSInfo(filePath, info)) {
            entry.width    = info.width;
            entry.height   = info.height;
            entry.channels = info.channels;
            spdlog::info("Imported DDS: {} ({}x{}, {}) -> {}",
                         filePath.filename().string(), info.width, info.height,
                         info.formatName, slotDisplayName(slot));
        } else {
            spdlog::warn("Failed to read DDS metadata: {}", filePath.string());
        }
    } else {
        // Use stb_image for PNG/TGA/BMP/JPG
        int w = 0, h = 0, c = 0;
        if (ImageUtils::getImageInfo(filePath, w, h, c)) {
            entry.width    = w;
            entry.height   = h;
            entry.channels = c;
            spdlog::info("Imported image: {} ({}x{}, {}ch) -> {}",
                         filePath.filename().string(), w, h, c, slotDisplayName(slot));
        } else {
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
    if (FileUtils::getExtensionLower(ddsPath) == ".dds") {
        DDSUtils::DDSInfo info;
        if (DDSUtils::getDDSInfo(ddsPath, info)) {
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
