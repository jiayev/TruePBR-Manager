#pragma once

#include "Project.h"

#include <filesystem>
#include <string>

namespace tpbr
{

/// Exports PBRTextureSets JSON files for Landscape-enabled texture sets.
///
/// For each texture set that has landscapeEdids, generates:
///   PBRTextureSets/<edid>.json
///
/// The JSON contains only material parameters (roughnessScale, displacementScale,
/// specularLevel, subsurfaceColor, subsurfaceOpacity) per the True PBR landscape spec.
///
/// Textures are NOT exported here — they are handled by ModExporter as usual,
/// since Landscape and NIF share the same texture files.
class LandscapeExporter
{
  public:
    /// Export landscape JSON files for all texture sets that have landscapeEdids.
    static bool exportLandscapeJsons(const Project& project);

    /// Generate the PBRTextureSets JSON content for a texture set.
    static std::string serializeLandscapeJson(const PBRParameters& params);
};

} // namespace tpbr
