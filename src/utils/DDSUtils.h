#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tpbr
{

/// DDS read/write utilities wrapping DirectXTex.
///
/// All compressed formats use software BC compression (no GPU device required).
class DDSUtils
{
  public:
    /// Information about a DDS file (without loading full pixel data)
    struct DDSInfo
    {
        int width = 0;
        int height = 0;
        int channels = 0; // Approximate: 4 for RGBA formats, 1 for BC4, etc.
        size_t mipLevels = 0;
        uint32_t dxgiFormat = 0; // DXGI_FORMAT value
        bool hasAlpha = false;
        bool isSRGB = false;
        std::string formatName; // Human-readable format name (e.g. "BC7_UNORM")
    };

    /// Get DDS file metadata without loading pixels.
    static bool getDDSInfo(const std::filesystem::path& path, DDSInfo& info);

    /// Load a DDS file and decompress to RGBA8 pixels (mip 0 only).
    static bool loadDDS(const std::filesystem::path& path, int& width, int& height, std::vector<uint8_t>& rgbaPixels,
                        bool* isSRGB = nullptr);

    /// Save RGBA8 pixels to a BC7_UNORM compressed DDS file.
    static bool saveDDS_BC7(const std::filesystem::path& path, int width, int height, const uint8_t* rgbaPixels,
                            bool srgb = false);

    /// Save RGBA8 pixels to a BC5_UNORM compressed DDS file.
    static bool saveDDS_BC5(const std::filesystem::path& path, int width, int height, const uint8_t* rgbaPixels);

    /// Save RGBA8 pixels to a BC3_UNORM compressed DDS file.
    static bool saveDDS_BC3(const std::filesystem::path& path, int width, int height, const uint8_t* rgbaPixels,
                            bool srgb = false);

    /// Save RGBA8 pixels to a BC6H_UF16 compressed DDS file.
    static bool saveDDS_BC6H(const std::filesystem::path& path, int width, int height, const uint8_t* rgbaPixels);

    /// Save single-channel (R8) pixels to a BC4_UNORM compressed DDS file.
    static bool saveDDS_BC4(const std::filesystem::path& path, int width, int height,
                            const uint8_t* singleChannelPixels);

    /// Save RGBA8 pixels to a BC1_UNORM compressed DDS file (no alpha).
    static bool saveDDS_BC1(const std::filesystem::path& path, int width, int height, const uint8_t* rgbaPixels,
                            bool srgb = false);

    /// Save RGBA8 pixels as an uncompressed R8G8B8A8_UNORM DDS (for debugging/preview).
    static bool saveDDS_RGBA(const std::filesystem::path& path, int width, int height, const uint8_t* rgbaPixels,
                             bool srgb = false);

    /// Copy a DDS file as-is (no re-encoding). Creates parent directories.
    static bool copyDDS(const std::filesystem::path& src, const std::filesystem::path& dst);
};

} // namespace tpbr
