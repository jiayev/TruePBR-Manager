#include "JsonExporter.h"

#include <filesystem>
#include <fstream>
#include <cmath>
#include <nlohmann/json.hpp>
#include "utils/Log.h"

namespace tpbr
{

using json = nlohmann::json;

namespace fs = std::filesystem;

enum class EffectiveMatchField
{
    Diffuse,
    Normal,
};

static double rounded3(double value)
{
    return std::round(value * 1000.0) / 1000.0;
}

static json rounded3Array(const std::array<float, 3>& values)
{
    return json::array({rounded3(values[0]), rounded3(values[1]), rounded3(values[2])});
}

static std::string sanitizedTextureSetStem(const PBRTextureSet& ts)
{
    std::string stem = ts.name;
    if (stem.empty())
    {
        stem = fs::path(ts.matchTexture).stem().string();
    }

    for (char& ch : stem)
    {
        switch (ch)
        {
        case '<':
        case '>':
        case ':':
        case '"':
        case '/':
        case '\\':
        case '|':
        case '?':
        case '*':
            ch = '_';
            break;
        default:
            break;
        }
    }

    if (stem.empty())
    {
        stem = "texture_set";
    }

    return stem;
}

static std::string lowercaseAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch)
                   {
                       if (ch >= 'A' && ch <= 'Z')
                       {
                           return static_cast<char>(ch - 'A' + 'a');
                       }
                       return static_cast<char>(ch);
                   });
    return value;
}

static EffectiveMatchField effectiveMatchField(const PBRTextureSet& ts)
{
    if (ts.matchMode == TextureMatchMode::Diffuse)
    {
        return EffectiveMatchField::Diffuse;
    }
    if (ts.matchMode == TextureMatchMode::Normal)
    {
        return EffectiveMatchField::Normal;
    }

    const std::string stem = lowercaseAscii(fs::path(ts.matchTexture).stem().string());
    if (stem.size() > 2 && stem.ends_with("_n"))
    {
        return EffectiveMatchField::Normal;
    }
    if (stem.size() > 4 && stem.ends_with("_msn"))
    {
        return EffectiveMatchField::Normal;
    }

    return EffectiveMatchField::Diffuse;
}

static std::string renamedMatchBase(const PBRTextureSet& ts)
{
    const std::string renamed = sanitizedTextureSetStem(ts);
    const std::string original = fs::path(ts.matchTexture).stem().string();
    return renamed == original ? std::string() : renamed;
}

static std::string normalizeJsonPath(const fs::path& path)
{
    std::string result = path.string();
    std::replace(result.begin(), result.end(), '/', '\\');
    return result;
}

static bool hasTextureAssignment(const PBRTextureSet& ts, PBRTextureSlot slot)
{
    auto it = ts.textures.find(slot);
    return it != ts.textures.end() && !it->second.sourcePath.empty();
}

static bool hasGeneratedRmaos(const PBRTextureSet& ts)
{
    if (ts.rmaosSourceMode == RMAOSSourceMode::PackedTexture)
    {
        return hasTextureAssignment(ts, PBRTextureSlot::RMAOS);
    }

    for (const auto& [channel, entry] : ts.channelMaps)
    {
        if (!entry.sourcePath.empty())
        {
            return true;
        }
    }

    return false;
}

static bool hasExportedSlot(const PBRTextureSet& ts, PBRTextureSlot slot)
{
    if (slot == PBRTextureSlot::RMAOS)
    {
        return hasGeneratedRmaos(ts);
    }

    return hasTextureAssignment(ts, slot);
}

static fs::path exportedRelativePath(const PBRTextureSet& ts, PBRTextureSlot slot)
{
    fs::path relative = fs::path("textures") / "pbr" / fs::path(ts.matchTexture);
    const std::string filename = sanitizedTextureSetStem(ts) + slotSuffix(slot);
    return relative.parent_path() / filename;
}

static fs::path defaultInferredRelativePath(const PBRTextureSet& ts, PBRTextureSlot slot)
{
    fs::path relative = fs::path("textures") / "pbr" / fs::path(ts.matchTexture);
    const std::string filename = relative.stem().string() + slotSuffix(slot);
    return relative.parent_path() / filename;
}

static std::optional<fs::path> defaultGeneratedPath(const PBRTextureSet& ts, PBRTextureSlot slot)
{
    const fs::path defaultBase = [&]()
    {
        fs::path path = defaultInferredRelativePath(ts, slot);
        const std::string renamed = renamedMatchBase(ts);
        if (!renamed.empty())
        {
            path = path.parent_path() / (renamed + slotSuffix(slot));
        }
        return path;
    }();

    switch (slot)
    {
    case PBRTextureSlot::Diffuse:
        return defaultBase;
    case PBRTextureSlot::Normal:
        return defaultBase;
    case PBRTextureSlot::Emissive:
        return ts.features.emissive ? std::optional<fs::path>(defaultBase) : std::nullopt;
    case PBRTextureSlot::Displacement:
        return ts.features.parallax ? std::optional<fs::path>(defaultBase) : std::nullopt;
    case PBRTextureSlot::RMAOS:
        return std::optional<fs::path>(defaultBase);
    case PBRTextureSlot::CoatNormalRoughness:
        if (ts.features.fuzz && hasExportedSlot(ts, PBRTextureSlot::Fuzz))
        {
            return defaultGeneratedPath(ts, PBRTextureSlot::Fuzz);
        }
        return ts.features.coatNormal ? std::optional<fs::path>(defaultBase) : std::nullopt;
    case PBRTextureSlot::Fuzz:
        return (ts.features.fuzz && hasExportedSlot(ts, PBRTextureSlot::Fuzz)) ? std::optional<fs::path>(defaultBase)
                                                                               : std::nullopt;
    case PBRTextureSlot::Subsurface:
        if (ts.features.coatDiffuse && hasExportedSlot(ts, PBRTextureSlot::CoatColor))
        {
            return defaultGeneratedPath(ts, PBRTextureSlot::CoatColor);
        }
        return (ts.features.subsurface || ts.features.subsurfaceFoliage) ? std::optional<fs::path>(defaultBase)
                                                                         : std::nullopt;
    case PBRTextureSlot::CoatColor:
        return ts.features.coatDiffuse ? std::optional<fs::path>(defaultBase) : std::nullopt;
    }

    return std::nullopt;
}

static void setExplicitSlotIfNeeded(json& entry, const PBRTextureSet& ts, PBRTextureSlot slot, const char* slotName)
{
    if (!hasExportedSlot(ts, slot))
    {
        return;
    }

    const fs::path actualPath = exportedRelativePath(ts, slot);
    const auto expectedPath = defaultGeneratedPath(ts, slot);
    if (expectedPath && fs::path(*expectedPath) == actualPath)
    {
        return;
    }

    entry[slotName] = normalizeJsonPath(actualPath);
}

static json serializeEntryToJson(const PBRTextureSet& ts)
{
    json entry;

    const auto matchField = effectiveMatchField(ts);
    if (matchField == EffectiveMatchField::Normal)
    {
        entry["match_normal"] = ts.matchTexture;
    }
    else
    {
        entry["texture"] = ts.matchTexture;
    }

    const std::string renameBase = renamedMatchBase(ts);
    if (!renameBase.empty())
    {
        entry["rename"] = renameBase;
    }

    if (!hasExportedSlot(ts, PBRTextureSlot::Diffuse))
    {
        entry["lock_diffuse"] = true;
    }
    if (!hasExportedSlot(ts, PBRTextureSlot::Normal))
    {
        entry["lock_normal"] = true;
    }

    // Feature toggles
    entry["emissive"] = ts.features.emissive;
    entry["parallax"] = ts.features.parallax;
    entry["subsurface_foliage"] = ts.features.subsurfaceFoliage;
    entry["subsurface"] = ts.features.subsurface;

    // Base parameters
    entry["specular_level"] = rounded3(ts.params.specularLevel);
    entry["roughness_scale"] = rounded3(ts.params.roughnessScale);
    entry["subsurface_opacity"] = rounded3(ts.params.subsurfaceOpacity);
    entry["displacement_scale"] = rounded3(ts.params.displacementScale);
    entry["subsurface_color"] = rounded3Array(ts.params.subsurfaceColor);

    // Emissive
    if (ts.features.emissive)
    {
        entry["emissive_scale"] = rounded3(ts.params.emissiveScale);
        if (!hasExportedSlot(ts, PBRTextureSlot::Emissive))
        {
            entry["lock_emissive"] = true;
        }
    }

    // Multilayer
    if (ts.features.multilayer || ts.features.coatNormal)
    {
        entry["coat_normal"] = ts.features.coatNormal;
        entry["coat_strength"] = rounded3(ts.params.coatStrength);
        entry["coat_roughness"] = rounded3(ts.params.coatRoughness);
        entry["coat_specular_level"] = rounded3(ts.params.coatSpecularLevel);
        entry["coat_diffuse"] = ts.features.coatDiffuse;
        entry["coat_parallax"] = ts.features.coatParallax;

        if ((ts.features.coatNormal || (ts.features.fuzz && hasExportedSlot(ts, PBRTextureSlot::Fuzz))) &&
            !hasExportedSlot(ts, PBRTextureSlot::CoatNormalRoughness) && !hasExportedSlot(ts, PBRTextureSlot::Fuzz))
        {
            entry["lock_cnr"] = true;
        }
    }

    if ((ts.features.parallax || ts.features.coatParallax) && !hasExportedSlot(ts, PBRTextureSlot::Displacement))
    {
        entry["lock_parallax"] = true;
    }

    if (!hasExportedSlot(ts, PBRTextureSlot::RMAOS))
    {
        entry["lock_rmaos"] = true;
    }

    if ((ts.features.subsurface || ts.features.subsurfaceFoliage || ts.features.coatDiffuse) &&
        !hasExportedSlot(ts, PBRTextureSlot::Subsurface) && !hasExportedSlot(ts, PBRTextureSlot::CoatColor))
    {
        entry["lock_subsurface"] = true;
    }

    // Fuzz
    if (ts.features.fuzz)
    {
        json fuzzObj;
        bool hasFuzzTexture = ts.textures.count(PBRTextureSlot::Fuzz) > 0;
        fuzzObj["texture"] = hasFuzzTexture;
        fuzzObj["color"] = rounded3Array(ts.params.fuzzColor);
        fuzzObj["weight"] = rounded3(ts.params.fuzzWeight);
        entry["fuzz"] = fuzzObj;
    }

    // Glint
    if (ts.features.glint)
    {
        json glintObj;
        glintObj["screen_space_scale"] = rounded3(ts.params.glintScreenSpaceScale);
        glintObj["log_microfacet_density"] = rounded3(ts.params.glintLogMicrofacetDensity);
        glintObj["microfacet_roughness"] = rounded3(ts.params.glintMicrofacetRoughness);
        glintObj["density_randomization"] = rounded3(ts.params.glintDensityRandomization);
        entry["glint"] = glintObj;
    }

    // Hair
    if (ts.features.hair)
    {
        entry["hair"] = true;
    }

    // Vertex color tweaks
    if (!ts.params.vertexColors)
    {
        entry["vertex_colors"] = false;
    }
    if (ts.params.vertexColorLumMult != 1.0f)
    {
        entry["vertex_color_lum_mult"] = rounded3(ts.params.vertexColorLumMult);
    }
    if (ts.params.vertexColorSatMult != 1.0f)
    {
        entry["vertex_color_sat_mult"] = rounded3(ts.params.vertexColorSatMult);
    }

    setExplicitSlotIfNeeded(entry, ts, PBRTextureSlot::Diffuse, "slot1");
    setExplicitSlotIfNeeded(entry, ts, PBRTextureSlot::Normal, "slot2");
    setExplicitSlotIfNeeded(entry, ts, PBRTextureSlot::Emissive, "slot3");
    setExplicitSlotIfNeeded(entry, ts, PBRTextureSlot::Displacement, "slot4");
    setExplicitSlotIfNeeded(entry, ts, PBRTextureSlot::RMAOS, "slot6");

    if (ts.features.fuzz && hasExportedSlot(ts, PBRTextureSlot::Fuzz))
    {
        setExplicitSlotIfNeeded(entry, ts, PBRTextureSlot::Fuzz, "slot7");
    }
    else
    {
        setExplicitSlotIfNeeded(entry, ts, PBRTextureSlot::CoatNormalRoughness, "slot7");
    }

    if (ts.features.coatDiffuse && hasExportedSlot(ts, PBRTextureSlot::CoatColor))
    {
        setExplicitSlotIfNeeded(entry, ts, PBRTextureSlot::CoatColor, "slot8");
    }
    else
    {
        setExplicitSlotIfNeeded(entry, ts, PBRTextureSlot::Subsurface, "slot8");
    }

    // Apply explicit slot path overrides — these always take precedence and
    // force the use of slot commands with the user-specified full relative path.
    auto applyOverride = [&](PBRTextureSlot slot, const char* slotName)
    {
        auto it = ts.slotPathOverrides.find(slot);
        if (it != ts.slotPathOverrides.end() && !it->second.empty())
        {
            entry[slotName] = normalizeJsonPath(fs::path(it->second));
        }
    };
    applyOverride(PBRTextureSlot::Diffuse, "slot1");
    applyOverride(PBRTextureSlot::Normal, "slot2");
    applyOverride(PBRTextureSlot::Emissive, "slot3");
    applyOverride(PBRTextureSlot::Displacement, "slot4");
    applyOverride(PBRTextureSlot::RMAOS, "slot6");
    applyOverride(PBRTextureSlot::CoatNormalRoughness, "slot7");
    applyOverride(PBRTextureSlot::Fuzz, "slot7");
    applyOverride(PBRTextureSlot::Subsurface, "slot8");
    applyOverride(PBRTextureSlot::CoatColor, "slot8");

    return entry;
}

std::string JsonExporter::serializeEntry(const PBRTextureSet& textureSet)
{
    return serializeEntryToJson(textureSet).dump(4);
}

std::string JsonExporter::serializeProject(const Project& project)
{
    json arr = json::array();
    for (const auto& ts : project.textureSets)
    {
        arr.push_back(serializeEntryToJson(ts));
    }
    return arr.dump(4);
}

bool JsonExporter::exportPGPatcherJson(const Project& project, const std::filesystem::path& outputDir)
{
    auto dir = outputDir / "PBRNIFPatcher";
    std::filesystem::create_directories(dir);

    auto filePath = dir / (project.name + ".json");

    std::string content = serializeProject(project);

    std::ofstream ofs(filePath);
    if (!ofs)
    {
        spdlog::error("Failed to write JSON: {}", filePath.string());
        return false;
    }

    ofs << content;
    spdlog::info("Exported PGPatcher JSON: {}", filePath.string());
    return true;
}

} // namespace tpbr
