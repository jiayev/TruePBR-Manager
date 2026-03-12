#include "JsonExporter.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "utils/Log.h"

namespace tpbr {

using json = nlohmann::json;

static json serializeEntryToJson(const PBRTextureSet& ts)
{
    json entry;

    // Matching field — vanilla diffuse path
    entry["texture"] = ts.matchTexture;

    // Feature toggles
    entry["emissive"]            = ts.features.emissive;
    entry["parallax"]            = ts.features.parallax;
    entry["subsurface_foliage"]  = ts.features.subsurfaceFoliage;
    entry["subsurface"]          = ts.features.subsurface;

    // Base parameters
    entry["specular_level"]      = ts.params.specularLevel;
    entry["roughness_scale"]     = ts.params.roughnessScale;
    entry["subsurface_opacity"]  = ts.params.subsurfaceOpacity;
    entry["displacement_scale"]  = ts.params.displacementScale;
    entry["subsurface_color"]    = {
        ts.params.subsurfaceColor[0],
        ts.params.subsurfaceColor[1],
        ts.params.subsurfaceColor[2]
    };

    // Emissive
    if (ts.features.emissive) {
        entry["emissive_scale"] = ts.params.emissiveScale;
    }

    // Multilayer
    if (ts.features.multilayer || ts.features.coatNormal) {
        entry["coat_normal"]         = ts.features.coatNormal;
        entry["coat_strength"]       = ts.params.coatStrength;
        entry["coat_roughness"]      = ts.params.coatRoughness;
        entry["coat_specular_level"] = ts.params.coatSpecularLevel;
        entry["coat_diffuse"]        = ts.features.coatDiffuse;
        entry["coat_parallax"]       = ts.features.coatParallax;
    }

    // Fuzz
    if (ts.features.fuzz) {
        json fuzzObj;
        bool hasFuzzTexture = ts.textures.count(PBRTextureSlot::Fuzz) > 0;
        fuzzObj["texture"] = hasFuzzTexture;
        fuzzObj["color"]   = {ts.params.fuzzColor[0], ts.params.fuzzColor[1], ts.params.fuzzColor[2]};
        fuzzObj["weight"]  = ts.params.fuzzWeight;
        entry["fuzz"]      = fuzzObj;
    }

    // Glint
    if (ts.features.glint) {
        json glintObj;
        glintObj["screen_space_scale"]     = ts.params.glintScreenSpaceScale;
        glintObj["log_microfacet_density"] = ts.params.glintLogMicrofacetDensity;
        glintObj["microfacet_roughness"]   = ts.params.glintMicrofacetRoughness;
        glintObj["density_randomization"]  = ts.params.glintDensityRandomization;
        entry["glint"]                     = glintObj;
    }

    // Hair
    if (ts.features.hair) {
        entry["hair"] = true;
    }

    // Vertex color tweaks
    if (!ts.params.vertexColors) {
        entry["vertex_colors"] = false;
    }
    if (ts.params.vertexColorLumMult != 1.0f) {
        entry["vertex_color_lum_mult"] = ts.params.vertexColorLumMult;
    }
    if (ts.params.vertexColorSatMult != 1.0f) {
        entry["vertex_color_sat_mult"] = ts.params.vertexColorSatMult;
    }

    return entry;
}

std::string JsonExporter::serializeEntry(const PBRTextureSet& textureSet)
{
    return serializeEntryToJson(textureSet).dump(4);
}

std::string JsonExporter::serializeProject(const Project& project)
{
    json arr = json::array();
    for (const auto& ts : project.textureSets) {
        arr.push_back(serializeEntryToJson(ts));
    }
    return arr.dump(4);
}

bool JsonExporter::exportPGPatcherJson(
    const Project& project,
    const std::filesystem::path& outputDir)
{
    auto dir = outputDir / "PBRNIFPatcher";
    std::filesystem::create_directories(dir);

    auto filePath = dir / (project.name + ".json");

    std::string content = serializeProject(project);

    std::ofstream ofs(filePath);
    if (!ofs) {
        spdlog::error("Failed to write JSON: {}", filePath.string());
        return false;
    }

    ofs << content;
    spdlog::info("Exported PGPatcher JSON: {}", filePath.string());
    return true;
}

} // namespace tpbr
