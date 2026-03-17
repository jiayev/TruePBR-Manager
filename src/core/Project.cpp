#include "Project.h"

#include <fstream>
#include <cmath>
#include <nlohmann/json.hpp>
#include "utils/Log.h"
#include <stdexcept>

namespace tpbr
{

using json = nlohmann::json;

static double rounded3(double value)
{
    return std::round(value * 1000.0) / 1000.0;
}

static json rounded3Array(const std::array<float, 3>& values)
{
    return json::array({rounded3(values[0]), rounded3(values[1]), rounded3(values[2])});
}

// ─── CRUD ──────────────────────────────────────────────────

size_t Project::addTextureSet(const std::string& setName, const std::string& vanillaMatch)
{
    PBRTextureSet ts;
    ts.name = setName;
    ts.matchTexture = vanillaMatch;
    textureSets.push_back(std::move(ts));
    return textureSets.size() - 1;
}

void Project::removeTextureSet(size_t index)
{
    if (index < textureSets.size())
    {
        textureSets.erase(textureSets.begin() + static_cast<ptrdiff_t>(index));
    }
}

// ─── JSON Serialization Helpers ────────────────────────────

static json featuresToJson(const PBRFeatureFlags& f)
{
    return json{
        {"emissive", f.emissive},
        {"parallax", f.parallax},
        {"subsurface", f.subsurface},
        {"subsurface_foliage", f.subsurfaceFoliage},
        {"multilayer", f.multilayer},
        {"coat_diffuse", f.coatDiffuse},
        {"coat_parallax", f.coatParallax},
        {"coat_normal", f.coatNormal},
        {"fuzz", f.fuzz},
        {"glint", f.glint},
        {"hair", f.hair},
    };
}

static PBRFeatureFlags featuresFromJson(const json& j)
{
    PBRFeatureFlags f;
    if (j.contains("emissive"))
        f.emissive = j["emissive"];
    if (j.contains("parallax"))
        f.parallax = j["parallax"];
    if (j.contains("subsurface"))
        f.subsurface = j["subsurface"];
    if (j.contains("subsurface_foliage"))
        f.subsurfaceFoliage = j["subsurface_foliage"];
    if (j.contains("multilayer"))
        f.multilayer = j["multilayer"];
    if (j.contains("coat_diffuse"))
        f.coatDiffuse = j["coat_diffuse"];
    if (j.contains("coat_parallax"))
        f.coatParallax = j["coat_parallax"];
    if (j.contains("coat_normal"))
        f.coatNormal = j["coat_normal"];
    if (j.contains("fuzz"))
        f.fuzz = j["fuzz"];
    if (j.contains("glint"))
        f.glint = j["glint"];
    if (j.contains("hair"))
        f.hair = j["hair"];
    return f;
}

static json paramsToJson(const PBRParameters& p)
{
    return json{
        {"specular_level", rounded3(p.specularLevel)},
        {"roughness_scale", rounded3(p.roughnessScale)},
        {"displacement_scale", rounded3(p.displacementScale)},
        {"subsurface_opacity", rounded3(p.subsurfaceOpacity)},
        {"subsurface_color", rounded3Array(p.subsurfaceColor)},
        {"emissive_scale", rounded3(p.emissiveScale)},
        {"emissive_color", rounded3Array(p.emissiveColor)},
        {"coat_strength", rounded3(p.coatStrength)},
        {"coat_roughness", rounded3(p.coatRoughness)},
        {"coat_specular_level", rounded3(p.coatSpecularLevel)},
        {"fuzz_color", rounded3Array(p.fuzzColor)},
        {"fuzz_weight", rounded3(p.fuzzWeight)},
        {"glint_screen_space_scale", rounded3(p.glintScreenSpaceScale)},
        {"glint_log_microfacet_density", rounded3(p.glintLogMicrofacetDensity)},
        {"glint_microfacet_roughness", rounded3(p.glintMicrofacetRoughness)},
        {"glint_density_randomization", rounded3(p.glintDensityRandomization)},
        {"vertex_colors", p.vertexColors},
        {"vertex_color_lum_mult", rounded3(p.vertexColorLumMult)},
        {"vertex_color_sat_mult", rounded3(p.vertexColorSatMult)},
    };
}

static PBRParameters paramsFromJson(const json& j)
{
    PBRParameters p;
    auto get = [&](const char* key, auto& val)
    {
        if (j.contains(key))
            val = j[key];
    };
    get("specular_level", p.specularLevel);
    get("roughness_scale", p.roughnessScale);
    get("displacement_scale", p.displacementScale);
    get("subsurface_opacity", p.subsurfaceOpacity);
    get("emissive_scale", p.emissiveScale);
    get("coat_strength", p.coatStrength);
    get("coat_roughness", p.coatRoughness);
    get("coat_specular_level", p.coatSpecularLevel);
    get("fuzz_weight", p.fuzzWeight);
    get("glint_screen_space_scale", p.glintScreenSpaceScale);
    get("glint_log_microfacet_density", p.glintLogMicrofacetDensity);
    get("glint_microfacet_roughness", p.glintMicrofacetRoughness);
    get("glint_density_randomization", p.glintDensityRandomization);
    get("vertex_colors", p.vertexColors);
    get("vertex_color_lum_mult", p.vertexColorLumMult);
    get("vertex_color_sat_mult", p.vertexColorSatMult);

    if (j.contains("subsurface_color") && j["subsurface_color"].is_array() && j["subsurface_color"].size() == 3)
    {
        p.subsurfaceColor = {j["subsurface_color"][0], j["subsurface_color"][1], j["subsurface_color"][2]};
    }
    if (j.contains("fuzz_color") && j["fuzz_color"].is_array() && j["fuzz_color"].size() == 3)
    {
        p.fuzzColor = {j["fuzz_color"][0], j["fuzz_color"][1], j["fuzz_color"][2]};
    }
    if (j.contains("emissive_color") && j["emissive_color"].is_array() && j["emissive_color"].size() >= 3)
    {
        p.emissiveColor = {j["emissive_color"][0], j["emissive_color"][1], j["emissive_color"][2]};
    }
    return p;
}

static std::string slotToString(PBRTextureSlot slot)
{
    switch (slot)
    {
    case PBRTextureSlot::Diffuse:
        return "diffuse";
    case PBRTextureSlot::Normal:
        return "normal";
    case PBRTextureSlot::Emissive:
        return "emissive";
    case PBRTextureSlot::Displacement:
        return "displacement";
    case PBRTextureSlot::RMAOS:
        return "rmaos";
    case PBRTextureSlot::CoatNormalRoughness:
        return "coat_normal_roughness";
    case PBRTextureSlot::Fuzz:
        return "fuzz";
    case PBRTextureSlot::Subsurface:
        return "subsurface";
    case PBRTextureSlot::CoatColor:
        return "coat_color";
    default:
        return "unknown";
    }
}

static PBRTextureSlot slotFromString(const std::string& s)
{
    if (s == "diffuse")
        return PBRTextureSlot::Diffuse;
    if (s == "normal")
        return PBRTextureSlot::Normal;
    if (s == "emissive")
        return PBRTextureSlot::Emissive;
    if (s == "displacement")
        return PBRTextureSlot::Displacement;
    if (s == "rmaos")
        return PBRTextureSlot::RMAOS;
    if (s == "coat_normal_roughness")
        return PBRTextureSlot::CoatNormalRoughness;
    if (s == "fuzz")
        return PBRTextureSlot::Fuzz;
    if (s == "subsurface")
        return PBRTextureSlot::Subsurface;
    if (s == "coat_color")
        return PBRTextureSlot::CoatColor;
    return PBRTextureSlot::Diffuse;
}

static std::string channelToString(ChannelMap ch)
{
    switch (ch)
    {
    case ChannelMap::Roughness:
        return "roughness";
    case ChannelMap::Metallic:
        return "metallic";
    case ChannelMap::AO:
        return "ao";
    case ChannelMap::Specular:
        return "specular";
    default:
        return "unknown";
    }
}

static ChannelMap channelFromString(const std::string& s)
{
    if (s == "roughness")
        return ChannelMap::Roughness;
    if (s == "metallic")
        return ChannelMap::Metallic;
    if (s == "ao")
        return ChannelMap::AO;
    if (s == "specular")
        return ChannelMap::Specular;
    return ChannelMap::Roughness;
}

static const char* textureAlphaModeKey(TextureAlphaMode mode)
{
    switch (mode)
    {
    case TextureAlphaMode::None:
        return "none";
    case TextureAlphaMode::Opaque:
        return "opaque";
    case TextureAlphaMode::Transparent:
        return "transparent";
    case TextureAlphaMode::Unknown:
    default:
        return "unknown";
    }
}

static TextureAlphaMode textureAlphaModeFromKey(const std::string& value)
{
    if (value == "none")
    {
        return TextureAlphaMode::None;
    }
    if (value == "opaque")
    {
        return TextureAlphaMode::Opaque;
    }
    if (value == "transparent")
    {
        return TextureAlphaMode::Transparent;
    }

    return TextureAlphaMode::Unknown;
}

static json channelMapEntryToJson(const ChannelMapEntry& entry)
{
    return json{
        {"path", entry.sourcePath.string()}, {"width", entry.width},   {"height", entry.height},
        {"channels", entry.channels},        {"format", entry.format},
    };
}

static ChannelMapEntry channelMapEntryFromJson(const json& j)
{
    ChannelMapEntry entry;
    if (j.is_string())
    {
        entry.sourcePath = j.get<std::string>();
        return entry;
    }

    entry.sourcePath = j.value("path", "");
    entry.width = j.value("width", 0);
    entry.height = j.value("height", 0);
    entry.channels = j.value("channels", 0);
    entry.format = j.value("format", "");
    return entry;
}

static json textureSetToJson(const PBRTextureSet& ts)
{
    json j;
    j["name"] = ts.name;
    j["match_texture"] = ts.matchTexture;
    j["match_texture_mode"] = textureMatchModeKey(ts.matchMode);
    j["tags"] = ts.tags;
    j["notes"] = ts.notes;

    // Landscape EDIDs
    if (!ts.landscapeEdids.empty())
    {
        j["landscape_edids"] = ts.landscapeEdids;
    }

    j["features"] = featuresToJson(ts.features);
    j["params"] = paramsToJson(ts.params);
    j["rmaos_source_mode"] = rmaosSourceModeKey(ts.rmaosSourceMode);

    // Textures
    json texArr = json::array();
    for (const auto& [slot, entry] : ts.textures)
    {
        json t;
        t["slot"] = slotToString(slot);
        t["path"] = entry.sourcePath.string();
        t["width"] = entry.width;
        t["height"] = entry.height;
        t["channels"] = entry.channels;
        t["alpha_mode"] = textureAlphaModeKey(entry.alphaMode);
        t["format"] = entry.format;
        texArr.push_back(t);
    }
    j["textures"] = texArr;

    json compressionObj = json::object();
    for (const auto& [slot, mode] : ts.exportCompression)
    {
        compressionObj[slotToString(slot)] = compressionModeKey(mode);
    }
    j["export_compression"] = compressionObj;

    // Export size overrides
    if (!ts.exportSize.empty())
    {
        json sizeObj = json::object();
        for (const auto& [slot, size] : ts.exportSize)
        {
            sizeObj[slotToString(slot)] = json::array({size.first, size.second});
        }
        j["export_size"] = sizeObj;
    }

    // Channel maps
    json chArr = json::object();
    for (const auto& [ch, entry] : ts.channelMaps)
    {
        chArr[channelToString(ch)] = channelMapEntryToJson(entry);
    }
    j["channel_maps"] = chArr;

    // Slot path overrides
    if (!ts.slotPathOverrides.empty())
    {
        json overridesObj = json::object();
        for (const auto& [slot, path] : ts.slotPathOverrides)
        {
            overridesObj[slotToString(slot)] = path;
        }
        j["slot_path_overrides"] = overridesObj;
    }

    return j;
}

static PBRTextureSet textureSetFromJson(const json& j)
{
    PBRTextureSet ts;
    if (j.contains("name"))
        ts.name = j["name"];
    if (j.contains("match_texture"))
        ts.matchTexture = j["match_texture"];
    if (j.contains("match_texture_mode"))
    {
        TextureMatchMode mode;
        if (tryParseTextureMatchMode(j["match_texture_mode"].get<std::string>(), mode))
        {
            ts.matchMode = mode;
        }
    }
    if (j.contains("tags"))
        ts.tags = j["tags"];
    if (j.contains("notes"))
        ts.notes = j["notes"];
    if (j.contains("landscape_edids") && j["landscape_edids"].is_array())
    {
        for (const auto& edid : j["landscape_edids"])
        {
            ts.landscapeEdids.push_back(edid.get<std::string>());
        }
    }
    if (j.contains("features"))
        ts.features = featuresFromJson(j["features"]);
    if (j.contains("params"))
        ts.params = paramsFromJson(j["params"]);
    if (j.contains("rmaos_source_mode"))
    {
        RMAOSSourceMode mode;
        if (tryParseRmaosSourceMode(j["rmaos_source_mode"].get<std::string>(), mode))
        {
            ts.rmaosSourceMode = mode;
        }
    }

    if (j.contains("textures") && j["textures"].is_array())
    {
        for (const auto& t : j["textures"])
        {
            TextureEntry entry;
            auto slot = slotFromString(t.value("slot", "diffuse"));
            entry.slot = slot;
            entry.sourcePath = t.value("path", "");
            entry.width = t.value("width", 0);
            entry.height = t.value("height", 0);
            entry.channels = t.value("channels", 0);
            entry.alphaMode = textureAlphaModeFromKey(t.value("alpha_mode", "unknown"));
            entry.format = t.value("format", "");
            ts.textures[slot] = entry;
        }
    }

    if (j.contains("export_compression") && j["export_compression"].is_object())
    {
        for (auto& [key, val] : j["export_compression"].items())
        {
            DDSCompressionMode mode;
            if (tryParseCompressionMode(val.get<std::string>(), mode))
            {
                ts.exportCompression[slotFromString(key)] = mode;
            }
        }
    }

    if (j.contains("channel_maps") && j["channel_maps"].is_object())
    {
        for (auto& [key, val] : j["channel_maps"].items())
        {
            ts.channelMaps[channelFromString(key)] = channelMapEntryFromJson(val);
        }
    }

    if (j.contains("export_size") && j["export_size"].is_object())
    {
        for (auto& [key, val] : j["export_size"].items())
        {
            if (val.is_array() && val.size() == 2)
            {
                ts.exportSize[slotFromString(key)] = {val[0].get<int>(), val[1].get<int>()};
            }
        }
    }

    if (j.contains("slot_path_overrides") && j["slot_path_overrides"].is_object())
    {
        for (auto& [key, val] : j["slot_path_overrides"].items())
        {
            ts.slotPathOverrides[slotFromString(key)] = val.get<std::string>();
        }
    }

    if (!j.contains("rmaos_source_mode"))
    {
        const bool hasPackedRmaos = ts.textures.find(PBRTextureSlot::RMAOS) != ts.textures.end();
        if (!hasPackedRmaos && !ts.channelMaps.empty())
        {
            ts.rmaosSourceMode = RMAOSSourceMode::SeparateChannels;
        }
    }

    return ts;
}

// ─── Save ──────────────────────────────────────────────────

bool Project::save(const std::filesystem::path& filePath) const
{
    json j;
    j["version"] = "1.0";
    j["name"] = name;
    j["output_mod_folder"] = outputModFolder.string();

    json sets = json::array();
    for (const auto& ts : textureSets)
    {
        sets.push_back(textureSetToJson(ts));
    }
    j["texture_sets"] = sets;

    std::ofstream ofs(filePath);
    if (!ofs)
    {
        spdlog::error("Project::save: failed to open {}", filePath.string());
        return false;
    }

    ofs << j.dump(4);
    spdlog::info("Project saved: {}", filePath.string());
    return true;
}

// ─── Load ──────────────────────────────────────────────────

Project Project::load(const std::filesystem::path& filePath)
{
    Project proj;

    std::ifstream ifs(filePath);
    if (!ifs)
    {
        spdlog::error("Project::load: failed to open {}", filePath.string());
        proj.name = filePath.stem().string();
        return proj;
    }

    json j;
    try
    {
        ifs >> j;
    }
    catch (const json::parse_error& e)
    {
        spdlog::error("Project::load: JSON parse error in {}: {}", filePath.string(), e.what());
        proj.name = filePath.stem().string();
        return proj;
    }

    proj.name = j.value("name", filePath.stem().string());
    if (j.contains("output_mod_folder"))
        proj.outputModFolder = j["output_mod_folder"].get<std::string>();

    if (j.contains("texture_sets") && j["texture_sets"].is_array())
    {
        for (const auto& tsj : j["texture_sets"])
        {
            proj.textureSets.push_back(textureSetFromJson(tsj));
        }
    }

    spdlog::info("Project loaded: {} ({} texture sets)", proj.name, proj.textureSets.size());
    return proj;
}

} // namespace tpbr
