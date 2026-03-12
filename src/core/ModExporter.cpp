#include "ModExporter.h"
#include "ChannelPacker.h"
#include "JsonExporter.h"
#include "utils/DDSUtils.h"
#include "utils/FileUtils.h"
#include "utils/ImageUtils.h"

#include <filesystem>
#include "utils/Log.h"

namespace tpbr {

namespace fs = std::filesystem;

static DDSCompressionMode exportCompressionForSlot(const PBRTextureSet& textureSet, PBRTextureSlot slot)
{
    auto it = textureSet.exportCompression.find(slot);
    if (it != textureSet.exportCompression.end()) {
        return it->second;
    }

    return defaultCompressionForSlot(slot);
}

static bool loadTextureRGBA(const fs::path& sourcePath,
                           int& width,
                           int& height,
                           std::vector<uint8_t>& rgbaPixels)
{
    const auto ext = FileUtils::getExtensionLower(sourcePath);
    if (ext == ".dds") {
        return DDSUtils::loadDDS(sourcePath, width, height, rgbaPixels);
    }

    auto imgData = ImageUtils::loadImage(sourcePath);
    if (imgData.pixels.empty()) {
        return false;
    }

    width = imgData.width;
    height = imgData.height;
    rgbaPixels = std::move(imgData.pixels);
    return true;
}

static bool saveTextureWithCompression(const fs::path& outputPath,
                                       int width,
                                       int height,
                                       const std::vector<uint8_t>& rgbaPixels,
                                       DDSCompressionMode compressionMode)
{
    switch (compressionMode) {
    case DDSCompressionMode::BC7_sRGB:
        return DDSUtils::saveDDS_BC7(outputPath, width, height, rgbaPixels.data(), true);
    case DDSCompressionMode::BC7_Linear:
        return DDSUtils::saveDDS_BC7(outputPath, width, height, rgbaPixels.data(), false);
    case DDSCompressionMode::BC6H_UF16:
        return DDSUtils::saveDDS_BC6H(outputPath, width, height, rgbaPixels.data());
    case DDSCompressionMode::BC5_Linear:
        return DDSUtils::saveDDS_BC5(outputPath, width, height, rgbaPixels.data());
    case DDSCompressionMode::BC4_Linear:
        {
            const size_t pixelCount = static_cast<size_t>(width) * height;
            std::vector<uint8_t> rChannel(pixelCount);
            for (size_t i = 0; i < pixelCount; ++i) {
                rChannel[i] = rgbaPixels[i * 4];
            }
            return DDSUtils::saveDDS_BC4(outputPath, width, height, rChannel.data());
        }
    case DDSCompressionMode::BC1_sRGB:
        return DDSUtils::saveDDS_BC1(outputPath, width, height, rgbaPixels.data(), true);
    case DDSCompressionMode::BC1_Linear:
        return DDSUtils::saveDDS_BC1(outputPath, width, height, rgbaPixels.data(), false);
    case DDSCompressionMode::RGBA8_sRGB:
        return DDSUtils::saveDDS_RGBA(outputPath, width, height, rgbaPixels.data(), true);
    case DDSCompressionMode::RGBA8_Linear:
        return DDSUtils::saveDDS_RGBA(outputPath, width, height, rgbaPixels.data(), false);
    }

    return false;
}

fs::path ModExporter::buildOutputPath(
    const fs::path& modFolder,
    const std::string& matchTexture,
    PBRTextureSlot slot)
{
    // PBR textures go under textures/pbr/<original_path><suffix>
    // matchTexture example: "architecture\\whiterun\\wrwoodplank01"
    fs::path relative = fs::path("textures") / "pbr" / matchTexture;
    std::string filename = relative.stem().string() + slotSuffix(slot);
    return modFolder / relative.parent_path() / filename;
}

/// Export a single texture to DDS. If the source is already DDS, copy as-is.
/// If the source is PNG/TGA/BMP, convert to the appropriate DDS format for the slot.
static bool exportSingleTexture(
    const TextureEntry& entry,
    const fs::path& outputPath,
    DDSCompressionMode compressionMode)
{
    fs::create_directories(outputPath.parent_path());

    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgbaPixels;
    if (!loadTextureRGBA(entry.sourcePath, width, height, rgbaPixels)) {
        spdlog::error("ModExporter: failed to load {}", entry.sourcePath.string());
        return false;
    }

    return saveTextureWithCompression(outputPath, width, height, rgbaPixels, compressionMode);
}

bool ModExporter::exportTextures(
    const PBRTextureSet& textureSet,
    const fs::path& modFolder)
{
    bool allOk = true;

    for (const auto& [slot, entry] : textureSet.textures) {
        auto outPath = buildOutputPath(modFolder, textureSet.matchTexture, slot);
        auto compressionMode = exportCompressionForSlot(textureSet, slot);

        if (!fs::exists(entry.sourcePath)) {
            spdlog::warn("ModExporter: source missing: {}", entry.sourcePath.string());
            allOk = false;
            continue;
        }

        if (!exportSingleTexture(entry, outPath, compressionMode)) {
            spdlog::error("ModExporter: failed to export {} -> {}", entry.sourcePath.string(), outPath.string());
            allOk = false;
        } else {
            spdlog::info("ModExporter: exported {}", outPath.string());
        }
    }

    // If there are individual channel maps but no RMAOS texture, pack them
    if (textureSet.textures.find(PBRTextureSlot::RMAOS) == textureSet.textures.end()
        && !textureSet.channelMaps.empty())
    {
        auto rmaosPath = buildOutputPath(modFolder, textureSet.matchTexture, PBRTextureSlot::RMAOS);
        auto compressionMode = exportCompressionForSlot(textureSet, PBRTextureSlot::RMAOS);
        if (!ChannelPacker::packRMAOS(textureSet.channelMaps, rmaosPath, compressionMode)) {
            spdlog::error("ModExporter: RMAOS packing failed for {}", textureSet.name);
            allOk = false;
        }
    }

    return allOk;
}

bool ModExporter::exportMod(const Project& project)
{
    if (project.outputModFolder.empty()) {
        spdlog::error("ModExporter: no output mod folder set");
        return false;
    }

    spdlog::info("ModExporter: exporting to {}", project.outputModFolder.string());

    bool allOk = true;

    // Export textures for each set
    for (const auto& ts : project.textureSets) {
        if (!exportTextures(ts, project.outputModFolder)) {
            allOk = false;
        }
    }

    // Export PGPatcher JSON
    if (!JsonExporter::exportPGPatcherJson(project, project.outputModFolder)) {
        allOk = false;
    }

    if (allOk) {
        spdlog::info("ModExporter: export complete (all OK)");
    } else {
        spdlog::warn("ModExporter: export complete with some errors");
    }

    return allOk;
}

} // namespace tpbr
