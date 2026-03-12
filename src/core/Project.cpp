#include "Project.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace tpbr {

size_t Project::addTextureSet(const std::string& setName, const std::string& vanillaMatch)
{
    PBRTextureSet ts;
    ts.name         = setName;
    ts.matchTexture = vanillaMatch;
    textureSets.push_back(std::move(ts));
    return textureSets.size() - 1;
}

void Project::removeTextureSet(size_t index)
{
    if (index < textureSets.size()) {
        textureSets.erase(textureSets.begin() + static_cast<ptrdiff_t>(index));
    }
}

bool Project::save(const std::filesystem::path& filePath) const
{
    // TODO: Implement full project serialization
    spdlog::info("Saving project '{}' to {}", name, filePath.string());
    return true;
}

Project Project::load(const std::filesystem::path& filePath)
{
    // TODO: Implement full project deserialization
    spdlog::info("Loading project from {}", filePath.string());
    Project proj;
    proj.name = filePath.stem().string();
    return proj;
}

} // namespace tpbr
