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
    const fs::path& outputPath)
{
    fs::create_directories(outputPath.parent_path());

    auto ext = FileUtils::getExtensionLower(entry.sourcePath);

    // If source is already DDS, copy as-is (assume the artist prepared it correctly)
    if (ext == ".dds") {
        return DDSUtils::copyDDS(entry.sourcePath, outputPath);
    }

    // Load image via stb_image
    auto imgData = ImageUtils::loadImage(entry.sourcePath);
    if (imgData.pixels.empty()) {
        spdlog::error("ModExporter: failed to load {}", entry.sourcePath.string());
        return false;
    }

    // Choose compression based on slot
    switch (entry.slot) {
    case PBRTextureSlot::Diffuse:
    case PBRTextureSlot::Normal:
    case PBRTextureSlot::RMAOS:
    case PBRTextureSlot::Subsurface:
    case PBRTextureSlot::CoatNormalRoughness:
    case PBRTextureSlot::Fuzz:
    case PBRTextureSlot::CoatColor:
    case PBRTextureSlot::Emissive:
        // BC7 for most slots (best quality for RGBA data)
        return DDSUtils::saveDDS_BC7(outputPath, imgData.width, imgData.height, imgData.pixels.data());

    case PBRTextureSlot::Displacement:
        // BC4 for single-channel height maps — extract R channel
        {
            const size_t pixelCount = static_cast<size_t>(imgData.width) * imgData.height;
            std::vector<uint8_t> rChannel(pixelCount);
            for (size_t i = 0; i < pixelCount; ++i) {
                rChannel[i] = imgData.pixels[i * 4]; // R
            }
            return DDSUtils::saveDDS_BC4(outputPath, imgData.width, imgData.height, rChannel.data());
        }

    default:
        return DDSUtils::saveDDS_BC7(outputPath, imgData.width, imgData.height, imgData.pixels.data());
    }
}

bool ModExporter::exportTextures(
    const PBRTextureSet& textureSet,
    const fs::path& modFolder)
{
    bool allOk = true;

    for (const auto& [slot, entry] : textureSet.textures) {
        auto outPath = buildOutputPath(modFolder, textureSet.matchTexture, slot);

        if (!fs::exists(entry.sourcePath)) {
            spdlog::warn("ModExporter: source missing: {}", entry.sourcePath.string());
            allOk = false;
            continue;
        }

        if (!exportSingleTexture(entry, outPath)) {
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
        if (!ChannelPacker::packRMAOS(textureSet.channelMaps, rmaosPath)) {
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
