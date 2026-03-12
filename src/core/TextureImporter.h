#pragma once

#include "PBRTextureSet.h"

#include <filesystem>

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
};

} // namespace tpbr
