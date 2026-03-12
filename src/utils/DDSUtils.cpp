#include "DDSUtils.h"

#include <DirectXTex.h>
#include "utils/Log.h"

#include <algorithm>

namespace tpbr {

// ─── Helpers ───────────────────────────────────────────────

/// Convert std::filesystem::path to wide string for DirectXTex APIs
static std::wstring toWide(const std::filesystem::path& p)
{
    return p.wstring();
}

/// Get a human-readable name for common DXGI formats
static std::string dxgiFormatName(DXGI_FORMAT fmt)
{
    switch (fmt) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:       return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:  return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8A8_UNORM:       return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:  return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_BC1_UNORM:            return "BC1_UNORM";
    case DXGI_FORMAT_BC1_UNORM_SRGB:       return "BC1_UNORM_SRGB";
    case DXGI_FORMAT_BC3_UNORM:            return "BC3_UNORM";
    case DXGI_FORMAT_BC3_UNORM_SRGB:       return "BC3_UNORM_SRGB";
    case DXGI_FORMAT_BC4_UNORM:            return "BC4_UNORM";
    case DXGI_FORMAT_BC5_UNORM:            return "BC5_UNORM";
    case DXGI_FORMAT_BC7_UNORM:            return "BC7_UNORM";
    case DXGI_FORMAT_BC7_UNORM_SRGB:       return "BC7_UNORM_SRGB";
    case DXGI_FORMAT_R8_UNORM:             return "R8_UNORM";
    default:                               return "DXGI_FORMAT(" + std::to_string(static_cast<int>(fmt)) + ")";
    }
}

/// Approximate channel count from DXGI format
static int dxgiFormatChannels(DXGI_FORMAT fmt)
{
    switch (fmt) {
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
        return 1;
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
        return 2;
    default:
        return 4;
    }
}

/// Create a ScratchImage from raw RGBA8 pixels (no copy — caller must keep pixels alive)
/// Returns a newly initialized ScratchImage with pixel data copied in.
static HRESULT createScratchFromRGBA(int width, int height, const uint8_t* rgbaPixels,
                                      DirectX::ScratchImage& scratch)
{
    HRESULT hr = scratch.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM,
                                       static_cast<size_t>(width),
                                       static_cast<size_t>(height),
                                       1, 1);
    if (FAILED(hr)) return hr;

    const DirectX::Image* img = scratch.GetImage(0, 0, 0);
    if (!img) return E_FAIL;

    const size_t srcRowPitch = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        const uint8_t* srcRow = rgbaPixels + y * srcRowPitch;
        uint8_t*       dstRow = img->pixels + y * img->rowPitch;
        std::memcpy(dstRow, srcRow, srcRowPitch);
    }

    return S_OK;
}

/// Compress a ScratchImage and save to DDS file.
static bool compressAndSave(const DirectX::ScratchImage& source,
                            DXGI_FORMAT targetFormat,
                            const std::filesystem::path& path)
{
    // Ensure parent directory exists
    std::filesystem::create_directories(path.parent_path());

    DirectX::ScratchImage compressed;
    HRESULT hr = DirectX::Compress(
        source.GetImages(), source.GetImageCount(), source.GetMetadata(),
        targetFormat,
        DirectX::TEX_COMPRESS_DEFAULT,
        DirectX::TEX_THRESHOLD_DEFAULT,
        compressed);

    if (FAILED(hr)) {
        spdlog::error("DDS compress failed: 0x{:08X} -> {}", static_cast<unsigned>(hr), path.string());
        return false;
    }

    hr = DirectX::SaveToDDSFile(
        compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(),
        DirectX::DDS_FLAGS_NONE,
        toWide(path).c_str());

    if (FAILED(hr)) {
        spdlog::error("DDS save failed: 0x{:08X} -> {}", static_cast<unsigned>(hr), path.string());
        return false;
    }

    return true;
}

// ─── getDDSInfo ────────────────────────────────────────────

bool DDSUtils::getDDSInfo(const std::filesystem::path& path, DDSInfo& info)
{
    DirectX::TexMetadata metadata{};
    DirectX::ScratchImage scratch;

    HRESULT hr = DirectX::LoadFromDDSFile(
        toWide(path).c_str(),
        DirectX::DDS_FLAGS_NONE,
        &metadata, scratch);

    if (FAILED(hr)) {
        spdlog::error("getDDSInfo: failed to load {} (0x{:08X})", path.string(), static_cast<unsigned>(hr));
        return false;
    }

    info.width      = static_cast<int>(metadata.width);
    info.height     = static_cast<int>(metadata.height);
    info.mipLevels  = metadata.mipLevels;
    info.dxgiFormat = static_cast<uint32_t>(metadata.format);
    info.formatName = dxgiFormatName(metadata.format);
    info.channels   = dxgiFormatChannels(metadata.format);

    return true;
}

// ─── loadDDS ───────────────────────────────────────────────

bool DDSUtils::loadDDS(const std::filesystem::path& path,
                       int& width, int& height,
                       std::vector<uint8_t>& rgbaPixels)
{
    DirectX::TexMetadata metadata{};
    DirectX::ScratchImage scratch;

    HRESULT hr = DirectX::LoadFromDDSFile(
        toWide(path).c_str(),
        DirectX::DDS_FLAGS_NONE,
        &metadata, scratch);

    if (FAILED(hr)) {
        spdlog::error("loadDDS: failed to load {} (0x{:08X})", path.string(), static_cast<unsigned>(hr));
        return false;
    }

    // If compressed, decompress first
    DirectX::ScratchImage decompressed;
    const DirectX::ScratchImage* source = &scratch;

    if (DirectX::IsCompressed(metadata.format)) {
        hr = DirectX::Decompress(
            scratch.GetImages(), scratch.GetImageCount(), scratch.GetMetadata(),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            decompressed);

        if (FAILED(hr)) {
            spdlog::error("loadDDS: decompress failed for {} (0x{:08X})", path.string(), static_cast<unsigned>(hr));
            return false;
        }
        source = &decompressed;
    }

    // Convert to R8G8B8A8_UNORM if not already
    DirectX::ScratchImage converted;
    const auto& srcMeta = source->GetMetadata();

    if (srcMeta.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        hr = DirectX::Convert(
            source->GetImages(), source->GetImageCount(), srcMeta,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT,
            converted);

        if (FAILED(hr)) {
            spdlog::error("loadDDS: convert failed for {} (0x{:08X})", path.string(), static_cast<unsigned>(hr));
            return false;
        }
        source = &converted;
    }

    // Extract mip 0
    const DirectX::Image* img = source->GetImage(0, 0, 0);
    if (!img || !img->pixels) {
        spdlog::error("loadDDS: no image data in {}", path.string());
        return false;
    }

    width  = static_cast<int>(img->width);
    height = static_cast<int>(img->height);

    const size_t dstRowPitch = static_cast<size_t>(width) * 4;
    rgbaPixels.resize(dstRowPitch * height);

    for (int y = 0; y < height; ++y) {
        const uint8_t* srcRow = img->pixels + y * img->rowPitch;
        uint8_t*       dstRow = rgbaPixels.data() + y * dstRowPitch;
        std::memcpy(dstRow, srcRow, dstRowPitch);
    }

    spdlog::debug("loadDDS: {} ({}x{}, {})", path.string(), width, height, dxgiFormatName(metadata.format));
    return true;
}

// ─── saveDDS_BC7 ───────────────────────────────────────────

bool DDSUtils::saveDDS_BC7(const std::filesystem::path& path,
                           int width, int height,
                           const uint8_t* rgbaPixels)
{
    DirectX::ScratchImage scratch;
    HRESULT hr = createScratchFromRGBA(width, height, rgbaPixels, scratch);
    if (FAILED(hr)) {
        spdlog::error("saveDDS_BC7: init failed (0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    bool ok = compressAndSave(scratch, DXGI_FORMAT_BC7_UNORM, path);
    if (ok) spdlog::debug("saveDDS_BC7: {} ({}x{})", path.string(), width, height);
    return ok;
}

// ─── saveDDS_BC4 ───────────────────────────────────────────

bool DDSUtils::saveDDS_BC4(const std::filesystem::path& path,
                           int width, int height,
                           const uint8_t* singleChannelPixels)
{
    // Create R8_UNORM scratch image
    DirectX::ScratchImage scratch;
    HRESULT hr = scratch.Initialize2D(DXGI_FORMAT_R8_UNORM,
                                       static_cast<size_t>(width),
                                       static_cast<size_t>(height),
                                       1, 1);
    if (FAILED(hr)) {
        spdlog::error("saveDDS_BC4: init failed (0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    const DirectX::Image* img = scratch.GetImage(0, 0, 0);
    if (!img) return false;

    const size_t srcRowPitch = static_cast<size_t>(width);
    for (int y = 0; y < height; ++y) {
        const uint8_t* srcRow = singleChannelPixels + y * srcRowPitch;
        uint8_t*       dstRow = img->pixels + y * img->rowPitch;
        std::memcpy(dstRow, srcRow, srcRowPitch);
    }

    bool ok = compressAndSave(scratch, DXGI_FORMAT_BC4_UNORM, path);
    if (ok) spdlog::debug("saveDDS_BC4: {} ({}x{})", path.string(), width, height);
    return ok;
}

// ─── saveDDS_BC1 ───────────────────────────────────────────

bool DDSUtils::saveDDS_BC1(const std::filesystem::path& path,
                           int width, int height,
                           const uint8_t* rgbaPixels)
{
    DirectX::ScratchImage scratch;
    HRESULT hr = createScratchFromRGBA(width, height, rgbaPixels, scratch);
    if (FAILED(hr)) {
        spdlog::error("saveDDS_BC1: init failed (0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    bool ok = compressAndSave(scratch, DXGI_FORMAT_BC1_UNORM, path);
    if (ok) spdlog::debug("saveDDS_BC1: {} ({}x{})", path.string(), width, height);
    return ok;
}

// ─── saveDDS_RGBA ──────────────────────────────────────────

bool DDSUtils::saveDDS_RGBA(const std::filesystem::path& path,
                            int width, int height,
                            const uint8_t* rgbaPixels)
{
    DirectX::ScratchImage scratch;
    HRESULT hr = createScratchFromRGBA(width, height, rgbaPixels, scratch);
    if (FAILED(hr)) {
        spdlog::error("saveDDS_RGBA: init failed (0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    std::filesystem::create_directories(path.parent_path());

    hr = DirectX::SaveToDDSFile(
        scratch.GetImages(), scratch.GetImageCount(), scratch.GetMetadata(),
        DirectX::DDS_FLAGS_NONE,
        toWide(path).c_str());

    if (FAILED(hr)) {
        spdlog::error("saveDDS_RGBA: save failed (0x{:08X}) -> {}", static_cast<unsigned>(hr), path.string());
        return false;
    }

    spdlog::debug("saveDDS_RGBA: {} ({}x{})", path.string(), width, height);
    return true;
}

// ─── copyDDS ───────────────────────────────────────────────

bool DDSUtils::copyDDS(const std::filesystem::path& src,
                       const std::filesystem::path& dst)
{
    if (!std::filesystem::exists(src)) {
        spdlog::error("copyDDS: source not found: {}", src.string());
        return false;
    }

    std::filesystem::create_directories(dst.parent_path());

    std::error_code ec;
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        spdlog::error("copyDDS: copy failed: {} -> {} ({})", src.string(), dst.string(), ec.message());
        return false;
    }

    spdlog::debug("copyDDS: {} -> {}", src.string(), dst.string());
    return true;
}

} // namespace tpbr
