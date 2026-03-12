#include "DDSUtils.h"

#include <spdlog/spdlog.h>

// TODO: Include DirectXTex headers and implement
// #include <DirectXTex.h>

namespace tpbr {

bool DDSUtils::loadDDS(const std::filesystem::path& path,
                       int& width, int& height,
                       std::vector<uint8_t>& rgbaPixels)
{
    // TODO: Implement with DirectXTex
    spdlog::info("DDSUtils::loadDDS — {}", path.string());
    return false;
}

bool DDSUtils::saveDDS_BC7(const std::filesystem::path& path,
                           int width, int height,
                           const uint8_t* rgbaPixels)
{
    // TODO: Implement with DirectXTex
    spdlog::info("DDSUtils::saveDDS_BC7 — {}", path.string());
    return false;
}

bool DDSUtils::saveDDS_BC4(const std::filesystem::path& path,
                           int width, int height,
                           const uint8_t* singleChannelPixels)
{
    // TODO: Implement with DirectXTex
    spdlog::info("DDSUtils::saveDDS_BC4 — {}", path.string());
    return false;
}

bool DDSUtils::saveDDS_BC1(const std::filesystem::path& path,
                           int width, int height,
                           const uint8_t* rgbaPixels)
{
    // TODO: Implement with DirectXTex
    spdlog::info("DDSUtils::saveDDS_BC1 — {}", path.string());
    return false;
}

} // namespace tpbr
