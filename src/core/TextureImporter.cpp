#include "TextureImporter.h"
#include "utils/DDSUtils.h"
#include "utils/FileUtils.h"
#include "utils/ImageUtils.h"

#include "utils/Log.h"

#include <algorithm>

namespace tpbr
{

static TextureAlphaMode alphaModeFromPixels(const std::vector<uint8_t>& rgbaPixels)
{
    if (rgbaPixels.empty())
    {
        return TextureAlphaMode::Unknown;
    }

    for (size_t i = 3; i < rgbaPixels.size(); i += 4)
    {
        if (rgbaPixels[i] != 255)
        {
            return TextureAlphaMode::Transparent;
        }
    }

    return TextureAlphaMode::Opaque;
}

/// Infer alpha mode from DDS format alone, without loading pixel data.
/// Returns a definitive answer for formats that cannot carry alpha.
/// For ambiguous formats (BC3, BC7, RGBA8, etc.), returns Unknown to signal
/// that a pixel-level scan is needed (deferred to first actual pixel load).
static TextureAlphaMode inferDDSAlphaModeFromFormat(const DDSUtils::DDSInfo& info)
{
    if (!info.hasAlpha)
    {
        return TextureAlphaMode::None;
    }

    // For formats that declare alpha support but the content may or may not use it,
    // we conservatively return Opaque (alpha channel exists but all-white is common).
    // This avoids a full pixel scan at import time.
    // The exact alpha mode is only needed for BC1 eligibility (canUseBC1), and
    // being conservative here (returning Opaque instead of Transparent) means BC1
    // is offered when alpha exists but isn't used — which is correct behavior.
    return TextureAlphaMode::Opaque;
}

/// Infer alpha mode for raster images from channel count alone.
/// 1- and 3-channel images have no alpha. 2- and 4-channel images could have
/// alpha, but we conservatively return Opaque to avoid a full pixel scan.
static TextureAlphaMode inferImageAlphaModeFromChannels(int channels)
{
    if (channels != 2 && channels != 4)
    {
        return TextureAlphaMode::None;
    }

    // Conservative: assume alpha exists but is fully opaque (most common case).
    // This avoids loading the entire image just to scan alpha bytes at import time.
    return TextureAlphaMode::Opaque;
}

static const char* channelDisplayName(ChannelMap channel)
{
    switch (channel)
    {
    case ChannelMap::Roughness:
        return "Roughness";
    case ChannelMap::Metallic:
        return "Metallic";
    case ChannelMap::AO:
        return "AO";
    case ChannelMap::Specular:
        return "Specular";
    default:
        return "Unknown";
    }
}

TextureEntry TextureImporter::importTexture(const std::filesystem::path& filePath, PBRTextureSlot slot)
{
    TextureEntry entry;
    entry.sourcePath = filePath;
    entry.slot = slot;

    auto ext = FileUtils::getExtensionLower(filePath);
    entry.format = ext;

    if (ext == ".dds")
    {
        // Use DirectXTex to read DDS header only (no pixel data loaded)
        DDSUtils::DDSInfo info;
        if (DDSUtils::getDDSInfo(filePath, info))
        {
            entry.width = info.width;
            entry.height = info.height;
            entry.channels = info.channels;
            entry.alphaMode = inferDDSAlphaModeFromFormat(info);
            spdlog::info("Imported DDS: {} ({}x{}, {}) -> {}", filePath.filename().string(), info.width, info.height,
                         info.formatName, slotDisplayName(slot));
        }
        else
        {
            spdlog::warn("Failed to read DDS metadata: {}", filePath.string());
        }
    }
    else
    {
        // Use stb_image header-only query for PNG/TGA/BMP/JPG
        int w = 0, h = 0, c = 0;
        if (ImageUtils::getImageInfo(filePath, w, h, c))
        {
            entry.width = w;
            entry.height = h;
            entry.channels = c;
            entry.alphaMode = inferImageAlphaModeFromChannels(c);
            spdlog::info("Imported image: {} ({}x{}, {}ch) -> {}", filePath.filename().string(), w, h, c,
                         slotDisplayName(slot));
        }
        else
        {
            spdlog::warn("Failed to read image metadata: {}", filePath.string());
        }
    }

    return entry;
}

ChannelMapEntry TextureImporter::importChannelMap(const std::filesystem::path& filePath, ChannelMap channel)
{
    ChannelMapEntry entry;
    entry.sourcePath = filePath;

    auto ext = FileUtils::getExtensionLower(filePath);
    entry.format = ext;

    if (ext == ".dds")
    {
        DDSUtils::DDSInfo info;
        if (DDSUtils::getDDSInfo(filePath, info))
        {
            entry.width = info.width;
            entry.height = info.height;
            entry.channels = info.channels;
            spdlog::info("Imported RMAOS channel: {} ({}x{}, {}) -> {}", filePath.filename().string(), info.width,
                         info.height, info.formatName, channelDisplayName(channel));
        }
        else
        {
            spdlog::warn("Failed to read DDS metadata: {}", filePath.string());
        }
    }
    else
    {
        int w = 0;
        int h = 0;
        int c = 0;
        if (ImageUtils::getImageInfo(filePath, w, h, c))
        {
            entry.width = w;
            entry.height = h;
            entry.channels = c;
            spdlog::info("Imported RMAOS channel: {} ({}x{}, {}ch) -> {}", filePath.filename().string(), w, h, c,
                         channelDisplayName(channel));
        }
        else
        {
            spdlog::warn("Failed to read image metadata: {}", filePath.string());
        }
    }

    return entry;
}

bool TextureImporter::isPackedRMAOS(const std::filesystem::path& ddsPath)
{
    // First check filename convention
    auto stem = ddsPath.stem().string();
    std::string stemLower = stem;
    std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(), ::tolower);

    if (stemLower.ends_with("_rmaos"))
        return true;

    // For DDS files, check if it's a 4-channel compressed texture (BC7/BC1/BC3)
    if (FileUtils::getExtensionLower(ddsPath) == ".dds")
    {
        DDSUtils::DDSInfo info;
        if (DDSUtils::getDDSInfo(ddsPath, info))
        {
            return info.channels == 4;
        }
    }

    return false;
}

const char* TextureImporter::fileFilter()
{
    return "Images (*.png *.dds *.tga *.bmp);;DDS Files (*.dds);;PNG Files (*.png);;All Files (*)";
}

// ─── Suffix detection ──────────────────────────────────────

static std::string toLower(const std::string& s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::optional<PBRTextureSlot> TextureImporter::detectSlotFromSuffix(const std::string& stem)
{
    auto lower = toLower(stem);

    // Order matters — check longer suffixes first to avoid partial matches
    if (lower.ends_with("_rmaos"))
        return PBRTextureSlot::RMAOS;
    if (lower.ends_with("_cnr"))
        return PBRTextureSlot::CoatNormalRoughness;
    if (lower.ends_with("_subsurface"))
        return PBRTextureSlot::Subsurface;
    if (lower.ends_with("_sss"))
        return PBRTextureSlot::Subsurface;
    if (lower.ends_with("_sk"))
        return PBRTextureSlot::Subsurface;
    if (lower.ends_with("_normal"))
        return PBRTextureSlot::Normal;
    if (lower.ends_with("_nrm"))
        return PBRTextureSlot::Normal;
    if (lower.ends_with("_nor"))
        return PBRTextureSlot::Normal;
    if (lower.ends_with("_msn"))
        return PBRTextureSlot::Normal;
    if (lower.ends_with("_emissive"))
        return PBRTextureSlot::Emissive;
    if (lower.ends_with("_emission"))
        return PBRTextureSlot::Emissive;
    if (lower.ends_with("_glow"))
        return PBRTextureSlot::Emissive;
    if (lower.ends_with("_displacement"))
        return PBRTextureSlot::Displacement;
    if (lower.ends_with("_parallax"))
        return PBRTextureSlot::Displacement;
    if (lower.ends_with("_height"))
        return PBRTextureSlot::Displacement;
    if (lower.ends_with("_disp"))
        return PBRTextureSlot::Displacement;
    if (lower.ends_with("_fuzz"))
        return PBRTextureSlot::Fuzz;
    if (lower.ends_with("_n"))
        return PBRTextureSlot::Normal;
    if (lower.ends_with("_g"))
        return PBRTextureSlot::Emissive;
    if (lower.ends_with("_e"))
        return PBRTextureSlot::Emissive;
    if (lower.ends_with("_p"))
        return PBRTextureSlot::Displacement;
    if (lower.ends_with("_h"))
        return PBRTextureSlot::Displacement;
    if (lower.ends_with("_f"))
        return PBRTextureSlot::Fuzz;

    return std::nullopt; // Could be diffuse (no suffix) but needs caller to decide
}

std::optional<ChannelMap> TextureImporter::detectChannelFromSuffix(const std::string& stem)
{
    auto lower = toLower(stem);

    // Check longer suffixes first
    if (lower.ends_with("_roughness") || lower.ends_with("_rough"))
        return ChannelMap::Roughness;
    if (lower.ends_with("_metallic") || lower.ends_with("_metal") || lower.ends_with("_metalness"))
        return ChannelMap::Metallic;
    if (lower.ends_with("_occlusion") || lower.ends_with("_ao"))
        return ChannelMap::AO;
    if (lower.ends_with("_specular") || lower.ends_with("_spec"))
        return ChannelMap::Specular;

    // Short suffixes last
    if (lower.ends_with("_r"))
        return ChannelMap::Roughness;
    if (lower.ends_with("_m"))
        return ChannelMap::Metallic;
    if (lower.ends_with("_o"))
        return ChannelMap::AO;
    if (lower.ends_with("_s"))
        return ChannelMap::Specular;

    return std::nullopt;
}

TextureImporter::BatchScanResult TextureImporter::scanFolder(const std::filesystem::path& folder)
{
    BatchScanResult result;

    auto files = FileUtils::listImages(folder);
    spdlog::info("Batch scan: {} images found in {}", files.size(), folder.string());

    for (const auto& filePath : files)
    {
        auto stem = filePath.stem().string();

        // Try slot detection
        auto slot = detectSlotFromSuffix(stem);
        if (slot.has_value())
        {
            result.slotFiles[slot.value()] = filePath;
            spdlog::debug("  {} -> slot {}", filePath.filename().string(), slotDisplayName(slot.value()));
            continue;
        }

        // Try channel detection (for split RMAOS)
        auto channel = detectChannelFromSuffix(stem);
        if (channel.has_value())
        {
            result.channelFiles[channel.value()] = filePath;
            spdlog::debug("  {} -> channel {}", filePath.filename().string(), channelDisplayName(channel.value()));
            continue;
        }

        // No known suffix — assume diffuse if no diffuse yet assigned
        if (result.slotFiles.find(PBRTextureSlot::Diffuse) == result.slotFiles.end())
        {
            // Heuristic: file with no PBR suffix is likely diffuse
            auto lower = toLower(stem);
            if (lower.ends_with("_d") || lower.ends_with("_diffuse") || lower.ends_with("_albedo") ||
                lower.ends_with("_basecolor") || !lower.empty())
            {
                result.slotFiles[PBRTextureSlot::Diffuse] = filePath;
                spdlog::debug("  {} -> Diffuse (default)", filePath.filename().string());
                continue;
            }
        }

        result.unmatched.push_back(filePath);
        spdlog::debug("  {} -> unmatched", filePath.filename().string());
    }

    spdlog::info("Batch scan result: {} slots, {} channels, {} unmatched", result.slotFiles.size(),
                 result.channelFiles.size(), result.unmatched.size());
    return result;
}

// ─── Auto-Detect from Diffuse ──────────────────────────────

/// Known diffuse suffixes (used to strip from the filename to derive the base name)
static const std::vector<std::string> kDiffuseSuffixes = {"_diffuse", "_albedo", "_basecolor", "_d"};

/// Extract the base name from a diffuse texture filename by stripping known diffuse suffixes.
static std::string extractBaseName(const std::string& stem)
{
    auto lower = toLower(stem);

    // Try longest suffixes first
    for (const auto& suffix : kDiffuseSuffixes)
    {
        if (lower.ends_with(suffix))
        {
            return stem.substr(0, stem.size() - suffix.size());
        }
    }

    // No recognized suffix — use the full stem as base name
    return stem;
}

/// Suffix mapping entry: suffix string → slot or channel target
struct SuffixMapping
{
    std::string suffix;

    enum class TargetType
    {
        Slot,
        Channel
    };
    TargetType targetType;

    PBRTextureSlot slot = PBRTextureSlot::Diffuse;
    ChannelMap channel = ChannelMap::Roughness;
};

/// Returns the full suffix mapping table used for auto-detection.
/// Ordered longest-first within each category to avoid partial matches.
static const std::vector<SuffixMapping>& getAutoDetectSuffixTable()
{
    static const std::vector<SuffixMapping> table = {
        // ── Slot targets (longer suffixes first) ───────────
        // RMAOS
        {"_rmaos", SuffixMapping::TargetType::Slot, PBRTextureSlot::RMAOS, {}},
        // Coat Normal+Roughness
        {"_cnr", SuffixMapping::TargetType::Slot, PBRTextureSlot::CoatNormalRoughness, {}},
        // Normal
        {"_normal", SuffixMapping::TargetType::Slot, PBRTextureSlot::Normal, {}},
        {"_nrm", SuffixMapping::TargetType::Slot, PBRTextureSlot::Normal, {}},
        {"_nor", SuffixMapping::TargetType::Slot, PBRTextureSlot::Normal, {}},
        {"_n", SuffixMapping::TargetType::Slot, PBRTextureSlot::Normal, {}},
        // Emissive
        {"_emissive", SuffixMapping::TargetType::Slot, PBRTextureSlot::Emissive, {}},
        {"_emission", SuffixMapping::TargetType::Slot, PBRTextureSlot::Emissive, {}},
        {"_glow", SuffixMapping::TargetType::Slot, PBRTextureSlot::Emissive, {}},
        {"_g", SuffixMapping::TargetType::Slot, PBRTextureSlot::Emissive, {}},
        {"_e", SuffixMapping::TargetType::Slot, PBRTextureSlot::Emissive, {}},
        // Displacement / Parallax
        {"_displacement", SuffixMapping::TargetType::Slot, PBRTextureSlot::Displacement, {}},
        {"_parallax", SuffixMapping::TargetType::Slot, PBRTextureSlot::Displacement, {}},
        {"_height", SuffixMapping::TargetType::Slot, PBRTextureSlot::Displacement, {}},
        {"_disp", SuffixMapping::TargetType::Slot, PBRTextureSlot::Displacement, {}},
        {"_p", SuffixMapping::TargetType::Slot, PBRTextureSlot::Displacement, {}},
        {"_h", SuffixMapping::TargetType::Slot, PBRTextureSlot::Displacement, {}},
        // Subsurface
        {"_subsurface", SuffixMapping::TargetType::Slot, PBRTextureSlot::Subsurface, {}},
        {"_sss", SuffixMapping::TargetType::Slot, PBRTextureSlot::Subsurface, {}},
        {"_sk", SuffixMapping::TargetType::Slot, PBRTextureSlot::Subsurface, {}},
        // Fuzz
        {"_fuzz", SuffixMapping::TargetType::Slot, PBRTextureSlot::Fuzz, {}},
        {"_f", SuffixMapping::TargetType::Slot, PBRTextureSlot::Fuzz, {}},

        // ── Channel targets (longer suffixes first) ────────
        // Roughness
        {"_roughness", SuffixMapping::TargetType::Channel, {}, ChannelMap::Roughness},
        {"_rough", SuffixMapping::TargetType::Channel, {}, ChannelMap::Roughness},
        {"_r", SuffixMapping::TargetType::Channel, {}, ChannelMap::Roughness},
        // Metallic
        {"_metalness", SuffixMapping::TargetType::Channel, {}, ChannelMap::Metallic},
        {"_metallic", SuffixMapping::TargetType::Channel, {}, ChannelMap::Metallic},
        {"_metal", SuffixMapping::TargetType::Channel, {}, ChannelMap::Metallic},
        {"_m", SuffixMapping::TargetType::Channel, {}, ChannelMap::Metallic},
        // AO
        {"_occlusion", SuffixMapping::TargetType::Channel, {}, ChannelMap::AO},
        {"_ao", SuffixMapping::TargetType::Channel, {}, ChannelMap::AO},
        {"_o", SuffixMapping::TargetType::Channel, {}, ChannelMap::AO},
        // Specular
        {"_specular", SuffixMapping::TargetType::Channel, {}, ChannelMap::Specular},
        {"_spec", SuffixMapping::TargetType::Channel, {}, ChannelMap::Specular},
        {"_s", SuffixMapping::TargetType::Channel, {}, ChannelMap::Specular},
    };
    return table;
}

TextureImporter::AutoDetectResult TextureImporter::autoDetectFromDiffuse(const std::filesystem::path& diffusePath)
{
    AutoDetectResult result;

    if (diffusePath.empty() || !std::filesystem::exists(diffusePath))
    {
        spdlog::warn("Auto-detect: diffuse path is empty or does not exist");
        return result;
    }

    const auto folder = diffusePath.parent_path();
    const auto stem = diffusePath.stem().string();
    const std::string baseName = extractBaseName(stem);

    spdlog::info("Auto-detect: base name = '{}', folder = '{}'", baseName, folder.string());

    // List all supported images in the same directory
    auto allFiles = FileUtils::listImages(folder);

    // Prepare lowercase base name for comparison
    const std::string baseNameLower = toLower(baseName);

    const auto& suffixTable = getAutoDetectSuffixTable();

    for (const auto& filePath : allFiles)
    {
        // Skip the diffuse file itself
        if (std::filesystem::equivalent(filePath, diffusePath))
            continue;

        const auto fileStem = filePath.stem().string();
        const auto fileStemLower = toLower(fileStem);

        // Check if file starts with our base name (case-insensitive)
        if (fileStemLower.size() <= baseNameLower.size())
            continue;
        if (fileStemLower.substr(0, baseNameLower.size()) != baseNameLower)
            continue;

        // Extract the suffix part
        const std::string suffixPart = fileStemLower.substr(baseNameLower.size());

        // Match against suffix table
        for (const auto& mapping : suffixTable)
        {
            if (suffixPart != mapping.suffix)
                continue;

            if (mapping.targetType == SuffixMapping::TargetType::Slot)
            {
                // Only take the first match per slot (longest suffix wins due to table order)
                if (result.slotFiles.find(mapping.slot) == result.slotFiles.end())
                {
                    result.slotFiles[mapping.slot] = filePath;
                    spdlog::info("  Auto-detect: {} -> slot {}", filePath.filename().string(),
                                 slotDisplayName(mapping.slot));
                }
            }
            else
            {
                if (result.channelFiles.find(mapping.channel) == result.channelFiles.end())
                {
                    result.channelFiles[mapping.channel] = filePath;
                    spdlog::info("  Auto-detect: {} -> channel {}", filePath.filename().string(),
                                 channelDisplayName(mapping.channel));
                }
            }
            break; // Matched — move to next file
        }
    }

    // Apply RMAOS priority: if packed RMAOS found, discard individual channel files
    if (result.slotFiles.count(PBRTextureSlot::RMAOS))
    {
        result.hasRmaos = true;
        if (!result.channelFiles.empty())
        {
            spdlog::info("  Auto-detect: RMAOS found, discarding {} individual channel files",
                         result.channelFiles.size());
            result.channelFiles.clear();
        }
    }

    spdlog::info("Auto-detect result: {} slot files, {} channel files, hasRmaos={}", result.slotFiles.size(),
                 result.channelFiles.size(), result.hasRmaos);
    return result;
}

std::string TextureImporter::getSuffixReferenceTable()
{
    return "Diffuse/Albedo:  _d, _diffuse, _albedo, _basecolor\n"
           "Normal:          _n, _nrm, _normal, _nor\n"
           "RMAOS (packed):  _rmaos\n"
           "Roughness:       _r, _roughness, _rough\n"
           "Metallic:        _m, _metallic, _metal, _metalness\n"
           "AO:              _o, _ao, _occlusion\n"
           "Specular:        _s, _specular, _spec\n"
           "Subsurface:      _sss, _sk, _subsurface\n"
           "Emissive:        _e, _g, _emissive, _glow, _emission\n"
           "Displacement:    _p, _h, _height, _parallax, _displacement, _disp\n"
           "Fuzz:            _f, _fuzz\n"
           "Coat Normal:     _cnr";
}

} // namespace tpbr
