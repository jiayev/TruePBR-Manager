#pragma once

#include "Project.h"

#include <filesystem>
#include <string>

namespace tpbr
{

/// Generates PGPatcher-compatible JSON files from a project.
class JsonExporter
{
  public:
    /// Export PGPatcher JSON for all texture sets in the project.
    /// Output: <outputDir>/PBRNIFPatcher/<projectName>.json
    static bool exportPGPatcherJson(const Project& project, const std::filesystem::path& outputDir);

    /// Generate JSON string for a single texture set entry.
    static std::string serializeEntry(const PBRTextureSet& textureSet);

    /// Generate full JSON string for the entire project.
    static std::string serializeProject(const Project& project);
};

} // namespace tpbr
