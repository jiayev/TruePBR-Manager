#pragma once

#include "PBRTextureSet.h"

#include <filesystem>
#include <map>

namespace tpbr {

/// Merges individual channel maps (R, M, AO, S) into a single RMAOS DDS file.
class ChannelPacker {
public:
    /// Pack individual channel images into an RMAOS DDS.
    /// @param channels  Map of ChannelMap -> source image path
    /// @param outputPath  Where to write the packed DDS
    /// @param width  Target width (0 = auto from input)
    /// @param height Target height (0 = auto from input)
    /// @return true on success
    static bool packRMAOS(
        const std::map<ChannelMap, std::filesystem::path>& channels,
        const std::filesystem::path& outputPath,
        DDSCompressionMode compressionMode,
        int width  = 0,
        int height = 0);
};

} // namespace tpbr
