#include "ModExporter.h"
#include "JsonExporter.h"

#include <filesystem>
#include <spdlog/spdlog.h>

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

bool ModExporter::exportTextures(
    const PBRTextureSet& textureSet,
    const fs::path& modFolder)
{
    for (const auto& [slot, entry] : textureSet.textures) {
        auto outPath = buildOutputPath(modFolder, textureSet.matchTexture, slot);
        fs::create_directories(outPath.parent_path());

        // TODO: Convert/copy DDS to output path
        // For now, just copy the source file
        if (fs::exists(entry.sourcePath)) {
            fs::copy_file(entry.sourcePath, outPath, fs::copy_options::overwrite_existing);
            spdlog::info("Exported: {}", outPath.string());
        } else {
            spdlog::warn("Source missing: {}", entry.sourcePath.string());
        }
    }
    return true;
}

bool ModExporter::exportMod(const Project& project)
{
    if (project.outputModFolder.empty()) {
        spdlog::error("No output mod folder set");
        return false;
    }

    spdlog::info("Exporting mod to: {}", project.outputModFolder.string());

    // Export textures for each set
    for (const auto& ts : project.textureSets) {
        exportTextures(ts, project.outputModFolder);
    }

    // Export PGPatcher JSON
    JsonExporter::exportPGPatcherJson(project, project.outputModFolder);

    spdlog::info("Export complete");
    return true;
}

} // namespace tpbr
