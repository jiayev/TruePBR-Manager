#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace tpbr {

/// Image loading helpers wrapping stb_image
class ImageUtils {
public:
    struct ImageData {
        int width    = 0;
        int height   = 0;
        int channels = 0;
        std::vector<uint8_t> pixels;
    };

    /// Load an image file (PNG, TGA, BMP, JPG) into RGBA pixels
    static ImageData loadImage(const std::filesystem::path& path);

    /// Get image dimensions without loading full pixel data
    static bool getImageInfo(const std::filesystem::path& path, int& width, int& height, int& channels);
};

} // namespace tpbr
