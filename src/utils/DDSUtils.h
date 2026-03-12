#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace tpbr {

/// DDS read/write utilities wrapping DirectXTex
class DDSUtils {
public:
    /// Load a DDS file and decompress to RGBA8 pixels
    static bool loadDDS(const std::filesystem::path& path,
                        int& width, int& height,
                        std::vector<uint8_t>& rgbaPixels);

    /// Save RGBA8 pixels to a BC7-compressed DDS file
    static bool saveDDS_BC7(const std::filesystem::path& path,
                            int width, int height,
                            const uint8_t* rgbaPixels);

    /// Save single-channel pixels to a BC4-compressed DDS file
    static bool saveDDS_BC4(const std::filesystem::path& path,
                            int width, int height,
                            const uint8_t* singleChannelPixels);

    /// Save RGBA8 pixels to a BC1-compressed DDS file
    static bool saveDDS_BC1(const std::filesystem::path& path,
                            int width, int height,
                            const uint8_t* rgbaPixels);
};

} // namespace tpbr
