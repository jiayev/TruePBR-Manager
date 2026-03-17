#include "ModImporter.h"

#include "utils/Log.h"

#include <algorithm>
#include <fstream>
#include <numeric>
#include <nlohmann/json.hpp>

namespace tpbr
{

using json = nlohmann::json;
namespace fs = std::filesystem;

// ─── Helpers ───────────────────────────────────────────────

static std::string toLowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch)
                   {
                       if (ch >= 'A' && ch <= 'Z')
                           return static_cast<char>(ch - 'A' + 'a');
                       return static_cast<char>(ch);
                   });
    return s;
}

/// Normalize path separators to backslash (PGPatcher convention).
static std::string normalizePath(const std::string& path)
{
    std::string result = path;
    std::replace(result.begin(), result.end(), '/', '\\');
    return result;
}

/// Normalize path separators to forward slash for filesystem operations.
static std::string toForwardSlash(const std::string& path)
{
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

/// Get a float value from JSON, with fallback.
static float getFloat(const json& j, const char* key, float fallback)
{
    if (j.contains(key) && j[key].is_number())
        return j[key].get<float>();
    return fallback;
}

/// Get a bool value from JSON, with fallback.
static bool getBool(const json& j, const char* key, bool fallback)
{
    if (j.contains(key) && j[key].is_boolean())
        return j[key].get<bool>();
    return fallback;
}

/// Get a string value from JSON, with fallback.
static std::string getString(const json& j, const char* key, const std::string& fallback = "")
{
    if (j.contains(key) && j[key].is_string())
        return j[key].get<std::string>();
    return fallback;
}

/// Get a 3-element float array from JSON.
static std::array<float, 3> getFloat3(const json& j, const char* key,
                                      const std::array<float, 3>& fallback = {1.0f, 1.0f, 1.0f})
{
    if (j.contains(key) && j[key].is_array() && j[key].size() >= 3)
    {
        return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>()};
    }
    return fallback;
}

/// Try to find a DDS file on disk (case-insensitive search).
/// Returns the actual path if found, or empty path if not.
static fs::path findDDSFile(const fs::path& expectedPath)
{
    // First try exact path
    if (fs::exists(expectedPath))
        return expectedPath;

    // Case-insensitive search in the parent directory
    auto parentDir = expectedPath.parent_path();
    if (!fs::is_directory(parentDir))
        return {};

    std::string targetName = toLowerAscii(expectedPath.filename().string());
    for (const auto& dirEntry : fs::directory_iterator(parentDir))
    {
        if (dirEntry.is_regular_file())
        {
            std::string entryName = toLowerAscii(dirEntry.path().filename().string());
            if (entryName == targetName)
                return dirEntry.path();
        }
    }

    return {};
}

/// Determine the base stem and directory for texture resolution.
/// Given a match texture like "architecture\\whiterun\\wrwoodplank01",
/// returns the stem "wrwoodplank01" and the relative directory "architecture/whiterun".
struct MatchInfo
{
    std::string stem;    // e.g. "wrwoodplank01"
    std::string relDir;  // e.g. "architecture/whiterun"
    bool isNormalMatch;  // true if matched via match_normal
    std::string renamed; // non-empty if "rename" was specified
};

static MatchInfo extractMatchInfo(const json& entry)
{
    MatchInfo info;
    info.isNormalMatch = false;

    // Determine which match field is used
    std::string matchPath;
    if (entry.contains("match_normal") && entry["match_normal"].is_string())
    {
        matchPath = entry["match_normal"].get<std::string>();
        info.isNormalMatch = true;
    }
    else if (entry.contains("match_diffuse") && entry["match_diffuse"].is_string())
    {
        matchPath = entry["match_diffuse"].get<std::string>();
    }
    else if (entry.contains("texture") && entry["texture"].is_string())
    {
        matchPath = entry["texture"].get<std::string>();
    }

    matchPath = toForwardSlash(matchPath);

    // Extract stem and directory
    fs::path matchFs(matchPath);
    info.stem = matchFs.stem().string();
    info.relDir = matchFs.parent_path().string();

    // Check for rename
    info.renamed = getString(entry, "rename");

    return info;
}

// ─── Texture Resolution ────────────────────────────────────

/// Build the expected texture path under the mod directory for a given slot.
/// PBR textures live under textures/pbr/<matchPath>/<stem><suffix>.
static fs::path buildExpectedTexturePath(const fs::path& modDir, const std::string& relDir, const std::string& stem,
                                         PBRTextureSlot slot)
{
    fs::path texDir = modDir / "textures" / "pbr";
    if (!relDir.empty())
        texDir = texDir / relDir;
    return texDir / (stem + slotSuffix(slot));
}

static bool resolveTexture(PBRTextureSet& ts, PBRTextureSlot slot, const fs::path& ddsPath,
                           std::vector<ImportDiagnostic>& diagnostics)
{
    auto found = findDDSFile(ddsPath);
    if (found.empty())
    {
        return false;
    }

    // Create a texture entry with the resolved path.
    // We only record the path and format here; full metadata (width, height,
    // channels) will be populated when the user interacts with the texture
    // in the UI, or can be filled in by a subsequent TextureImporter call.
    TextureEntry entry;
    entry.sourcePath = found;
    entry.slot = slot;
    entry.format = ".dds";
    ts.textures[slot] = entry;
    return true;
}

// ─── JSON Parsing ──────────────────────────────────────────

static json mergeDefaults(const json& defaults, const json& entry)
{
    // Start with a copy of the entry
    json merged = entry;

    // Copy fields from defaults that are not present in the entry
    for (auto it = defaults.begin(); it != defaults.end(); ++it)
    {
        if (!merged.contains(it.key()))
        {
            merged[it.key()] = it.value();
        }
        else if (it.value().is_object() && merged[it.key()].is_object())
        {
            // For nested objects (like "fuzz" or "glint"), merge recursively
            for (auto inner = it.value().begin(); inner != it.value().end(); ++inner)
            {
                if (!merged[it.key()].contains(inner.key()))
                {
                    merged[it.key()][inner.key()] = inner.value();
                }
            }
        }
    }

    return merged;
}

static PBRTextureSet parseEntry(const json& entry, const fs::path& modDir, std::vector<ImportDiagnostic>& diagnostics)
{
    PBRTextureSet ts;

    // ── Match field ────────────────────────────────────────
    auto matchInfo = extractMatchInfo(entry);

    if (matchInfo.stem.empty())
    {
        diagnostics.push_back({ImportDiagnostic::Severity::Warning, "Entry has no match texture field, skipping."});
        return ts;
    }

    // Build matchTexture (the vanilla diffuse/normal path without pbr prefix)
    if (!matchInfo.relDir.empty())
    {
        // Normalize to backslash for PGPatcher convention
        std::string normalizedDir = matchInfo.relDir;
        std::replace(normalizedDir.begin(), normalizedDir.end(), '/', '\\');
        ts.matchTexture = normalizedDir + "\\" + matchInfo.stem;
    }
    else
    {
        ts.matchTexture = matchInfo.stem;
    }

    // Set match mode
    if (matchInfo.isNormalMatch)
    {
        ts.matchMode = TextureMatchMode::Normal;
    }
    else
    {
        ts.matchMode = TextureMatchMode::Diffuse;
    }

    // Name: use rename if present, otherwise the match stem
    ts.name = matchInfo.renamed.empty() ? matchInfo.stem : matchInfo.renamed;

    // The effective stem for texture file names is the rename (if any), else the match stem
    const std::string textureStem = matchInfo.renamed.empty() ? matchInfo.stem : matchInfo.renamed;

    // ── Feature flags ──────────────────────────────────────
    ts.features.emissive = getBool(entry, "emissive", false);
    ts.features.parallax = getBool(entry, "parallax", false);
    ts.features.subsurface = getBool(entry, "subsurface", false);
    ts.features.subsurfaceFoliage = getBool(entry, "subsurface_foliage", false);
    ts.features.coatNormal = getBool(entry, "coat_normal", false);
    ts.features.coatDiffuse = getBool(entry, "coat_diffuse", false);
    ts.features.coatParallax = getBool(entry, "coat_parallax", false);
    ts.features.hair = getBool(entry, "hair", false);

    // Multilayer is implied by coat_normal
    if (ts.features.coatNormal || ts.features.coatDiffuse || ts.features.coatParallax)
    {
        ts.features.multilayer = true;
    }

    // Fuzz (nested object)
    if (entry.contains("fuzz") && entry["fuzz"].is_object())
    {
        ts.features.fuzz = true;
        const auto& fuzzObj = entry["fuzz"];
        ts.params.fuzzColor = getFloat3(fuzzObj, "color", {1.0f, 1.0f, 1.0f});
        ts.params.fuzzWeight = getFloat(fuzzObj, "weight", 1.0f);
    }

    // Glint (nested object)
    if (entry.contains("glint") && entry["glint"].is_object())
    {
        ts.features.glint = true;
        const auto& glintObj = entry["glint"];
        ts.params.glintScreenSpaceScale = getFloat(glintObj, "screen_space_scale", 0.0f);
        ts.params.glintLogMicrofacetDensity = getFloat(glintObj, "log_microfacet_density", 0.0f);
        ts.params.glintMicrofacetRoughness = getFloat(glintObj, "microfacet_roughness", 0.0f);
        ts.params.glintDensityRandomization = getFloat(glintObj, "density_randomization", 0.0f);
    }

    // ── Parameters ─────────────────────────────────────────
    ts.params.specularLevel = getFloat(entry, "specular_level", 0.04f);
    ts.params.roughnessScale = getFloat(entry, "roughness_scale", 1.0f);
    ts.params.displacementScale = getFloat(entry, "displacement_scale", 1.0f);
    ts.params.subsurfaceOpacity = getFloat(entry, "subsurface_opacity", 1.0f);
    ts.params.subsurfaceColor = getFloat3(entry, "subsurface_color", {1.0f, 1.0f, 1.0f});
    ts.params.emissiveScale = getFloat(entry, "emissive_scale", 0.0f);
    ts.params.emissiveColor = getFloat3(entry, "emissive_color", {1.0f, 1.0f, 1.0f});

    // Coat parameters
    ts.params.coatStrength = getFloat(entry, "coat_strength", 0.0f);
    ts.params.coatRoughness = getFloat(entry, "coat_roughness", 0.0f);
    ts.params.coatSpecularLevel = getFloat(entry, "coat_specular_level", 0.04f);

    // Vertex color parameters
    ts.params.vertexColors = getBool(entry, "vertex_colors", true);
    ts.params.vertexColorLumMult = getFloat(entry, "vertex_color_lum_mult", 1.0f);
    ts.params.vertexColorSatMult = getFloat(entry, "vertex_color_sat_mult", 1.0f);

    // ── Slot overrides (explicit slot paths) ───────────────
    // PGPatcher slot1..slot8 map to NIF TX00..TX07
    // slot1=Diffuse(TX00), slot2=Normal(TX01), slot3=Emissive(TX02),
    // slot4=Displacement(TX03), slot5=N/A(TX04), slot6=RMAOS(TX05),
    // slot7=CoatNormalRoughness/Fuzz(TX06), slot8=Subsurface/CoatColor(TX07)

    struct SlotMapping
    {
        const char* jsonKey;
        PBRTextureSlot slot;
    };
    static const SlotMapping slotMappings[] = {
        {"slot1", PBRTextureSlot::Diffuse},      {"slot2", PBRTextureSlot::Normal}, {"slot3", PBRTextureSlot::Emissive},
        {"slot4", PBRTextureSlot::Displacement}, {"slot6", PBRTextureSlot::RMAOS},
    };

    for (const auto& mapping : slotMappings)
    {
        std::string slotPath = getString(entry, mapping.jsonKey);
        if (!slotPath.empty())
        {
            ts.slotPathOverrides[mapping.slot] = normalizePath(slotPath);
        }
    }

    // slot7 depends on whether fuzz is active
    {
        std::string slot7Path = getString(entry, "slot7");
        if (!slot7Path.empty())
        {
            PBRTextureSlot slot7 = ts.features.fuzz ? PBRTextureSlot::Fuzz : PBRTextureSlot::CoatNormalRoughness;
            ts.slotPathOverrides[slot7] = normalizePath(slot7Path);
        }
    }

    // slot8 depends on whether coat_diffuse is active
    {
        std::string slot8Path = getString(entry, "slot8");
        if (!slot8Path.empty())
        {
            PBRTextureSlot slot8 = ts.features.coatDiffuse ? PBRTextureSlot::CoatColor : PBRTextureSlot::Subsurface;
            ts.slotPathOverrides[slot8] = normalizePath(slot8Path);
        }
    }

    // ── Resolve textures on disk ───────────────────────────
    // For each slot, try to find the DDS file under textures/pbr/

    // Helper: resolve a texture either from explicit slot path or from convention
    auto tryResolve = [&](PBRTextureSlot slot, bool featureRequired = false, bool featureActive = true)
    {
        if (featureRequired && !featureActive)
            return;

        // Check for lock fields — if locked, PG won't touch this slot
        // (but we still try to find the texture in case the mod ships it)
        // lock fields don't affect our import — we import whatever exists

        // Check if there's an explicit slot override path
        auto overrideIt = ts.slotPathOverrides.find(slot);
        if (overrideIt != ts.slotPathOverrides.end() && !overrideIt->second.empty())
        {
            // The override path is a full relative path like "textures\\pbr\\whatever.dds"
            fs::path fullPath = modDir / toForwardSlash(overrideIt->second);
            if (resolveTexture(ts, slot, fullPath, diagnostics))
            {
                return;
            }
            diagnostics.push_back(
                {ImportDiagnostic::Severity::Info,
                 "Explicit slot path not found on disk: " + overrideIt->second + " (for " + ts.name + ")"});
        }

        // Fall back to convention-based path
        fs::path expected = buildExpectedTexturePath(modDir, toForwardSlash(matchInfo.relDir), textureStem, slot);
        if (!resolveTexture(ts, slot, expected, diagnostics))
        {
            // If rename is in effect, also try with the original match stem
            if (!matchInfo.renamed.empty() && matchInfo.renamed != matchInfo.stem)
            {
                fs::path altPath =
                    buildExpectedTexturePath(modDir, toForwardSlash(matchInfo.relDir), matchInfo.stem, slot);
                resolveTexture(ts, slot, altPath, diagnostics);
            }
        }
    };

    // Lock fields tell us PG won't auto-add a texture for that slot;
    // but the mod might still ship the texture. We always try to resolve.
    bool lockDiffuse = getBool(entry, "lock_diffuse", false);
    bool lockNormal = getBool(entry, "lock_normal", false);

    // Always try to resolve diffuse and normal (unless locked AND no override)
    tryResolve(PBRTextureSlot::Diffuse);
    tryResolve(PBRTextureSlot::Normal);

    // RMAOS — always expected for PBR
    tryResolve(PBRTextureSlot::RMAOS);

    // Feature-dependent slots
    tryResolve(PBRTextureSlot::Emissive, true, ts.features.emissive);
    tryResolve(PBRTextureSlot::Displacement, true, ts.features.parallax || ts.features.coatParallax);

    // Slot 7: CoatNormalRoughness or Fuzz
    if (ts.features.fuzz)
    {
        // Check if fuzz has a texture (fuzz.texture == true)
        bool fuzzTexture = false;
        if (entry.contains("fuzz") && entry["fuzz"].is_object())
        {
            fuzzTexture = getBool(entry["fuzz"], "texture", false);
        }
        if (fuzzTexture)
        {
            tryResolve(PBRTextureSlot::Fuzz);
        }
    }
    else if (ts.features.coatNormal)
    {
        tryResolve(PBRTextureSlot::CoatNormalRoughness);
    }

    // Slot 8: Subsurface or CoatColor
    if (ts.features.coatDiffuse)
    {
        tryResolve(PBRTextureSlot::CoatColor);
    }
    else if (ts.features.subsurface || ts.features.subsurfaceFoliage)
    {
        tryResolve(PBRTextureSlot::Subsurface);
    }

    // ── Diagnostics summary ────────────────────────────────
    int textureCount = static_cast<int>(ts.textures.size());
    if (textureCount == 0)
    {
        diagnostics.push_back({ImportDiagnostic::Severity::Warning, "No textures found on disk for entry: " + ts.name +
                                                                        " (match: " + ts.matchTexture + ")"});
    }
    else
    {
        diagnostics.push_back({ImportDiagnostic::Severity::Info,
                               "Imported " + std::to_string(textureCount) + " textures for: " + ts.name});
    }

    return ts;
}

std::vector<PBRTextureSet> ModImporter::parseJsonFile(const fs::path& jsonPath, const fs::path& modDir,
                                                      std::vector<ImportDiagnostic>& diagnostics)
{
    std::vector<PBRTextureSet> results;

    std::ifstream ifs(jsonPath);
    if (!ifs)
    {
        diagnostics.push_back({ImportDiagnostic::Severity::Error, "Failed to open JSON file: " + jsonPath.string()});
        return results;
    }

    json root;
    try
    {
        ifs >> root;
    }
    catch (const json::parse_error& e)
    {
        diagnostics.push_back({ImportDiagnostic::Severity::Error,
                               "JSON parse error in " + jsonPath.filename().string() + ": " + e.what()});
        return results;
    }

    // Determine format: flat array or {default, entries}
    json defaults = json::object();
    json entries;

    if (root.is_array())
    {
        entries = root;
    }
    else if (root.is_object())
    {
        if (root.contains("default") && root["default"].is_object())
        {
            defaults = root["default"];
        }
        if (root.contains("entries") && root["entries"].is_array())
        {
            entries = root["entries"];
        }
        else
        {
            // Could be a single entry object (not standard but handle gracefully)
            diagnostics.push_back({ImportDiagnostic::Severity::Warning,
                                   jsonPath.filename().string() +
                                       ": expected array or {default, entries} object. Trying as single entry."});
            auto merged = mergeDefaults(defaults, root);
            auto ts = parseEntry(merged, modDir, diagnostics);
            if (!ts.matchTexture.empty())
            {
                results.push_back(std::move(ts));
            }
            return results;
        }
    }
    else
    {
        diagnostics.push_back(
            {ImportDiagnostic::Severity::Error, jsonPath.filename().string() + ": unexpected JSON root type."});
        return results;
    }

    // Parse each entry
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (!entries[i].is_object())
        {
            diagnostics.push_back(
                {ImportDiagnostic::Severity::Warning,
                 jsonPath.filename().string() + " entry " + std::to_string(i) + ": not a JSON object, skipping."});
            continue;
        }

        // Skip entries that are delete operations
        if (getBool(entries[i], "delete", false))
        {
            diagnostics.push_back(
                {ImportDiagnostic::Severity::Info,
                 jsonPath.filename().string() + " entry " + std::to_string(i) + ": delete entry, skipping."});
            continue;
        }

        auto merged = mergeDefaults(defaults, entries[i]);
        auto ts = parseEntry(merged, modDir, diagnostics);
        if (!ts.matchTexture.empty())
        {
            results.push_back(std::move(ts));
        }
    }

    return results;
}

ModScanResult ModImporter::scanForJsonFiles(const fs::path& modDir)
{
    ModScanResult result;

    // Validate mod directory
    if (!fs::is_directory(modDir))
    {
        result.errorMessage = "Not a valid directory: " + modDir.string();
        return result;
    }

    // Look for PBRNIFPatcher directory
    fs::path patcherDir = modDir / "PBRNIFPatcher";
    if (!fs::is_directory(patcherDir))
    {
        // Also try case-insensitive search
        bool found = false;
        for (const auto& dirEntry : fs::directory_iterator(modDir))
        {
            if (dirEntry.is_directory() && toLowerAscii(dirEntry.path().filename().string()) == "pbrnifpatcher")
            {
                patcherDir = dirEntry.path();
                found = true;
                break;
            }
        }

        if (!found)
        {
            result.errorMessage = "No PBRNIFPatcher directory found in: " + modDir.string() +
                                  "\n\nExpected structure:\n  <mod>/PBRNIFPatcher/*.json\n  <mod>/textures/pbr/...";
            return result;
        }
    }

    result.patcherDir = patcherDir;

    // Recursively find all JSON files in the patcher directory and subdirectories
    for (const auto& dirEntry : fs::recursive_directory_iterator(patcherDir))
    {
        if (dirEntry.is_regular_file())
        {
            auto ext = toLowerAscii(dirEntry.path().extension().string());
            if (ext == ".json")
            {
                result.jsonFilesAbsolute.push_back(dirEntry.path());
                // Store relative path from patcherDir for display
                result.jsonFiles.push_back(fs::relative(dirEntry.path(), patcherDir));
            }
        }
    }

    if (result.jsonFiles.empty())
    {
        result.errorMessage = "No JSON files found in: " + patcherDir.string();
        return result;
    }

    // Sort for deterministic ordering (by relative path)
    // Sort both vectors together by relative path
    std::vector<size_t> indices(result.jsonFiles.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(),
              [&](size_t a, size_t b) { return result.jsonFiles[a] < result.jsonFiles[b]; });

    std::vector<fs::path> sortedRel;
    std::vector<fs::path> sortedAbs;
    sortedRel.reserve(indices.size());
    sortedAbs.reserve(indices.size());
    for (size_t i : indices)
    {
        sortedRel.push_back(result.jsonFiles[i]);
        sortedAbs.push_back(result.jsonFilesAbsolute[i]);
    }
    result.jsonFiles = std::move(sortedRel);
    result.jsonFilesAbsolute = std::move(sortedAbs);

    result.success = true;

    spdlog::info("ModImporter: found {} JSON file(s) in {}", result.jsonFiles.size(), patcherDir.string());

    return result;
}

ModImportResult ModImporter::importJsonFile(const fs::path& jsonPath, const fs::path& modDir)
{
    ModImportResult result;

    if (!fs::exists(jsonPath))
    {
        result.diagnostics.push_back({ImportDiagnostic::Severity::Error, "JSON file not found: " + jsonPath.string()});
        return result;
    }

    result.diagnostics.push_back({ImportDiagnostic::Severity::Info, "Parsing: " + jsonPath.filename().string()});

    auto textureSets = parseJsonFile(jsonPath, modDir, result.diagnostics);
    for (auto& ts : textureSets)
    {
        result.project.textureSets.push_back(std::move(ts));
    }

    // Set project metadata — use the JSON filename (without extension) as project name
    result.project.name = jsonPath.stem().string();
    result.project.outputModFolder = modDir;

    int totalSets = static_cast<int>(result.project.textureSets.size());
    int totalTextures = 0;
    for (const auto& ts : result.project.textureSets)
    {
        totalTextures += static_cast<int>(ts.textures.size());
    }

    result.diagnostics.push_back(
        {ImportDiagnostic::Severity::Info, "Import complete: " + std::to_string(totalSets) + " texture set(s), " +
                                               std::to_string(totalTextures) + " texture(s) resolved."});

    result.success = totalSets > 0;

    spdlog::info("ModImporter: imported {} texture sets ({} textures) from {}", totalSets, totalTextures,
                 jsonPath.string());

    return result;
}

} // namespace tpbr
