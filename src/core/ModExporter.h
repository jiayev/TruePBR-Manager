#pragma once

#include "Project.h"

#include <filesystem>

namespace tpbr {

/// Exports the full mod package: DDS files + PGPatcher JSON.
class ModExporter {
public:
    /// Export everything to the target mod folder.
    /// Creates textures/pbr/... structure and PBRNIFPatcher JSON.
    static bool exportMod(const Project& project);

    /// Export DDS files for a single texture set.
    static bool exportTextures(
        const PBRTextureSet& textureSet,
        const std::filesystem::path& modFolder);

private:
    /// Build the output path for a texture: textures/pbr/<matchParent>/<textureSetName><suffix>
    static std::filesystem::path buildOutputPath(
        const std::filesystem::path& modFolder,
    const PBRTextureSet& textureSet,
        PBRTextureSlot slot);
};

} // namespace tpbr
