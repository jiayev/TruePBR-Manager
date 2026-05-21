#pragma once

#include "PBRTextureSet.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tpbr
{

/// Imports image files (PNG, DDS, TGA, etc.) and populates TextureEntry metadata.
class TextureImporter
{
  public:
    /// Import a texture file into a given slot. Fills width/height/channels/format.
    static TextureEntry importTexture(const std::filesystem::path& filePath, PBRTextureSlot slot);

    /// Import a single RMAOS channel source file. Fills width/height/channels/format.
    static ChannelMapEntry importChannelMap(const std::filesystem::path& filePath, ChannelMap channel);

    /// Detect whether a DDS file is a packed RMAOS texture (heuristic).
    static bool isPackedRMAOS(const std::filesystem::path& ddsPath);

    /// Get supported file filter string for Qt file dialogs.
    static const char* fileFilter();

    /// Result of a batch scan — maps slots to discovered files
    struct BatchScanResult
    {
        std::map<PBRTextureSlot, std::filesystem::path> slotFiles;
        std::map<ChannelMap, std::filesystem::path> channelFiles;
        std::vector<std::filesystem::path> unmatched; // Files that didn't match any known suffix
    };

    /// Scan a folder for image files and auto-detect PBR slots by suffix convention.
    /// Recognizes: _n, _g, _p, _rmaos, _cnr, _f, _sss, _roughness, _metallic, _ao, _specular, etc.
    static BatchScanResult scanFolder(const std::filesystem::path& folder);

    /// Detect PBR slot from a filename suffix. Returns nullopt if not recognized.
    static std::optional<PBRTextureSlot> detectSlotFromSuffix(const std::string& stem);

    /// Detect RMAOS channel from a filename suffix. Returns nullopt if not recognized.
    static std::optional<ChannelMap> detectChannelFromSuffix(const std::string& stem);

    // ─── Auto-Detect from Diffuse ──────────────────────────

    /// Result of auto-detecting a full texture set from a diffuse texture path.
    struct AutoDetectResult
    {
        std::map<PBRTextureSlot, std::filesystem::path> slotFiles;
        std::map<ChannelMap, std::filesystem::path> channelFiles;
        bool hasRmaos = false; ///< True if a packed _rmaos file was found (takes priority)
    };

    /// Given the path of an already-imported diffuse/albedo texture, scan the same
    /// directory for files sharing the same base name with recognized PBR suffixes.
    /// If RMAOS is found, individual channel files (_r, _m, _o, _s) are excluded.
    static AutoDetectResult autoDetectFromDiffuse(const std::filesystem::path& diffusePath);

    /// Return a human-readable reference table of recognized suffixes for display
    /// in tooltips. Each entry is "SlotName: _suffix1, _suffix2, ..."
    static std::string getSuffixReferenceTable();
};

} // namespace tpbr
