#include "ImageUtils.h"

#include <spdlog/spdlog.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace tpbr {

ImageUtils::ImageData ImageUtils::loadImage(const std::filesystem::path& path)
{
    ImageData data;

    int w, h, c;
    unsigned char* pixels = stbi_load(path.string().c_str(), &w, &h, &c, 4);
    if (!pixels) {
        spdlog::error("Failed to load image: {}", path.string());
        return data;
    }

    data.width    = w;
    data.height   = h;
    data.channels = 4; // forced RGBA
    data.pixels.assign(pixels, pixels + (w * h * 4));

    stbi_image_free(pixels);
    return data;
}

bool ImageUtils::getImageInfo(const std::filesystem::path& path, int& width, int& height, int& channels)
{
    return stbi_info(path.string().c_str(), &width, &height, &channels) != 0;
}

} // namespace tpbr
