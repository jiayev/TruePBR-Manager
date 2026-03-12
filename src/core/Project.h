#pragma once

#include "PBRTextureSet.h"

#include <filesystem>
#include <string>
#include <vector>

namespace tpbr
{

/// Top-level project — contains multiple PBR texture sets and export settings.
struct Project
{
    std::string name;
    std::filesystem::path outputModFolder;
    std::vector<PBRTextureSet> textureSets;

    /// Add a new empty texture set, returns its index.
    size_t addTextureSet(const std::string& setName, const std::string& vanillaMatch);

    /// Remove texture set by index.
    void removeTextureSet(size_t index);

    /// Save project to file (.tpbr JSON).
    bool save(const std::filesystem::path& filePath) const;

    /// Load project from file.
    static Project load(const std::filesystem::path& filePath);
};

} // namespace tpbr
