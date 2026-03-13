#include "IBLProcessor.h"
#include "utils/Log.h"

#include <DirectXTex.h>
#include <tinyexr.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace tpbr
{

static const float PI = 3.14159265359f;

// ─── HDRI Loading ──────────────────────────────────────────

static bool loadEXR(const std::filesystem::path& path, int& w, int& h, std::vector<float>& rgba)
{
    float* out = nullptr;
    const char* err = nullptr;
    int ret = LoadEXR(&out, &w, &h, path.string().c_str(), &err);
    if (ret != TINYEXR_SUCCESS)
    {
        spdlog::error("IBL: Failed to load EXR {}: {}", path.string(), err ? err : "unknown");
        FreeEXRErrorMessage(err);
        return false;
    }
    rgba.assign(out, out + w * h * 4);
    free(out);
    return true;
}

static bool loadHDR_DXTex(const std::filesystem::path& path, int& w, int& h, std::vector<float>& rgba)
{
    DirectX::TexMetadata meta{};
    DirectX::ScratchImage scratch;
    HRESULT hr = DirectX::LoadFromHDRFile(path.wstring().c_str(), &meta, scratch);
    if (FAILED(hr))
    {
        spdlog::error("IBL: Failed to load HDR {}: 0x{:08X}", path.string(), static_cast<unsigned>(hr));
        return false;
    }

    // Convert to RGBA32F
    DirectX::ScratchImage converted;
    if (meta.format != DXGI_FORMAT_R32G32B32A32_FLOAT)
    {
        hr = DirectX::Convert(scratch.GetImages(), scratch.GetImageCount(), meta, DXGI_FORMAT_R32G32B32A32_FLOAT,
                              DirectX::TEX_FILTER_DEFAULT, 0, converted);
        if (FAILED(hr))
        {
            spdlog::error("IBL: HDR format conversion failed");
            return false;
        }
    }
    else
    {
        converted = std::move(scratch);
    }

    const auto* img = converted.GetImage(0, 0, 0);
    w = static_cast<int>(img->width);
    h = static_cast<int>(img->height);
    const size_t pixelCount = static_cast<size_t>(w) * h;
    rgba.resize(pixelCount * 4);
    for (int y = 0; y < h; ++y)
    {
        memcpy(rgba.data() + y * w * 4, img->pixels + y * img->rowPitch, w * 4 * sizeof(float));
    }
    return true;
}

static bool loadDDS_float(const std::filesystem::path& path, int& w, int& h, std::vector<float>& rgba)
{
    DirectX::TexMetadata meta{};
    DirectX::ScratchImage scratch;
    HRESULT hr = DirectX::LoadFromDDSFile(path.wstring().c_str(), DirectX::DDS_FLAGS_NONE, &meta, scratch);
    if (FAILED(hr))
    {
        spdlog::error("IBL: Failed to load DDS {}: 0x{:08X}", path.string(), static_cast<unsigned>(hr));
        return false;
    }

    DirectX::ScratchImage decompressed;
    if (DirectX::IsCompressed(meta.format))
    {
        hr = DirectX::Decompress(scratch.GetImages(), scratch.GetImageCount(), meta, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                 decompressed);
        if (FAILED(hr))
            return false;
    }
    else
    {
        decompressed = std::move(scratch);
    }

    DirectX::ScratchImage converted;
    if (decompressed.GetMetadata().format != DXGI_FORMAT_R32G32B32A32_FLOAT)
    {
        hr = DirectX::Convert(decompressed.GetImages(), decompressed.GetImageCount(), decompressed.GetMetadata(),
                              DXGI_FORMAT_R32G32B32A32_FLOAT, DirectX::TEX_FILTER_DEFAULT, 0, converted);
        if (FAILED(hr))
            return false;
    }
    else
    {
        converted = std::move(decompressed);
    }

    const auto* img = converted.GetImage(0, 0, 0);
    w = static_cast<int>(img->width);
    h = static_cast<int>(img->height);
    rgba.resize(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
    {
        memcpy(rgba.data() + y * w * 4, img->pixels + y * img->rowPitch, w * 4 * sizeof(float));
    }
    return true;
}

bool IBLProcessor::loadHDRI(const std::filesystem::path& path, int& w, int& h, std::vector<float>& rgba)
{
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".exr")
        return loadEXR(path, w, h, rgba);
    if (ext == ".hdr")
        return loadHDR_DXTex(path, w, h, rgba);
    if (ext == ".dds")
        return loadDDS_float(path, w, h, rgba);

    spdlog::error("IBL: Unsupported HDRI format: {}", ext);
    return false;
}

// ─── Cubemap Utilities ─────────────────────────────────────

// Get direction vector for a cubemap texel.
// face: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
static DirectX::XMFLOAT3 cubemapDirection(int face, float u, float v)
{
    // u,v in [-1, 1]
    float x, y, z;
    switch (face)
    {
    case 0:
        x = 1;
        y = v;
        z = -u;
        break; // +X
    case 1:
        x = -1;
        y = v;
        z = u;
        break; // -X
    case 2:
        x = u;
        y = 1;
        z = -v;
        break; // +Y
    case 3:
        x = u;
        y = -1;
        z = v;
        break; // -Y
    case 4:
        x = u;
        y = v;
        z = 1;
        break; // +Z
    case 5:
        x = -u;
        y = v;
        z = -1;
        break; // -Z
    default:
        x = y = z = 0;
    }
    float len = std::sqrt(x * x + y * y + z * z);
    return {x / len, y / len, z / len};
}

// Sample equirect map at a direction
static void sampleEquirect(const float* eq, int eqW, int eqH, float dx, float dy, float dz, float& r, float& g,
                           float& b)
{
    float theta = std::atan2(dz, dx);                   // [-PI, PI]
    float phi = std::asin(std::clamp(dy, -1.0f, 1.0f)); // [-PI/2, PI/2]

    float u = (theta / PI + 1.0f) * 0.5f; // [0, 1]
    float v = (phi / PI + 0.5f);          // [0, 1], but flip
    v = 1.0f - v;                         // top of image = +Y

    int px = std::clamp(static_cast<int>(u * eqW), 0, eqW - 1);
    int py = std::clamp(static_cast<int>(v * eqH), 0, eqH - 1);

    const float* p = eq + (py * eqW + px) * 4;
    r = p[0];
    g = p[1];
    b = p[2];
}

void IBLProcessor::sampleCubemap(const std::vector<float> faces[6], int faceSize, float x, float y, float z, float& r,
                                 float& g, float& b)
{
    float ax = std::abs(x), ay = std::abs(y), az = std::abs(z);
    int face;
    float u, v;

    if (ax >= ay && ax >= az)
    {
        face = x > 0 ? 0 : 1;
        float ma = ax;
        u = (x > 0 ? -z : z) / ma;
        v = y / ma;
    }
    else if (ay >= ax && ay >= az)
    {
        face = y > 0 ? 2 : 3;
        float ma = ay;
        u = x / ma;
        v = (y > 0 ? -z : z) / ma;
    }
    else
    {
        face = z > 0 ? 4 : 5;
        float ma = az;
        u = (z > 0 ? x : -x) / ma;
        v = y / ma;
    }

    // Map from [-1,1] to [0, faceSize-1]
    int px = std::clamp(static_cast<int>((u * 0.5f + 0.5f) * faceSize), 0, faceSize - 1);
    int py = std::clamp(static_cast<int>((v * 0.5f + 0.5f) * faceSize), 0, faceSize - 1);

    const float* p = faces[face].data() + (py * faceSize + px) * 4;
    r = p[0];
    g = p[1];
    b = p[2];
}

// ─── Equirect → Cubemap ────────────────────────────────────

void IBLProcessor::equirectToCubemap(const float* equirect, int eqW, int eqH, int faceSize, std::vector<float> faces[6])
{
    for (int f = 0; f < 6; ++f)
    {
        faces[f].resize(static_cast<size_t>(faceSize) * faceSize * 4);
        for (int y = 0; y < faceSize; ++y)
        {
            for (int x = 0; x < faceSize; ++x)
            {
                float u = (static_cast<float>(x) + 0.5f) / faceSize * 2.0f - 1.0f;
                float v = (static_cast<float>(y) + 0.5f) / faceSize * 2.0f - 1.0f;
                auto dir = cubemapDirection(f, u, v);

                float r, g, b;
                sampleEquirect(equirect, eqW, eqH, dir.x, dir.y, dir.z, r, g, b);

                float* dst = faces[f].data() + (y * faceSize + x) * 4;
                dst[0] = r;
                dst[1] = g;
                dst[2] = b;
                dst[3] = 1.0f;
            }
        }
    }
}

// ─── Irradiance Convolution ────────────────────────────────

void IBLProcessor::convolveIrradiance(const std::vector<float> faces[6], int srcSize, int dstSize,
                                      std::vector<float> outFaces[6])
{
    const int sampleCount = 512;

    for (int f = 0; f < 6; ++f)
    {
        outFaces[f].resize(static_cast<size_t>(dstSize) * dstSize * 4);

        for (int y = 0; y < dstSize; ++y)
        {
            for (int x = 0; x < dstSize; ++x)
            {
                float u = (static_cast<float>(x) + 0.5f) / dstSize * 2.0f - 1.0f;
                float v = (static_cast<float>(y) + 0.5f) / dstSize * 2.0f - 1.0f;
                auto N = cubemapDirection(f, u, v);

                // Build TBN from normal
                DirectX::XMFLOAT3 up =
                    (std::abs(N.y) < 0.999f) ? DirectX::XMFLOAT3{0, 1, 0} : DirectX::XMFLOAT3{1, 0, 0};
                float tx = up.y * N.z - up.z * N.y;
                float ty = up.z * N.x - up.x * N.z;
                float tz = up.x * N.y - up.y * N.x;
                float tlen = std::sqrt(tx * tx + ty * ty + tz * tz);
                tx /= tlen;
                ty /= tlen;
                tz /= tlen;
                float bx = N.y * tz - N.z * ty;
                float by = N.z * tx - N.x * tz;
                float bz = N.x * ty - N.y * tx;

                float irR = 0, irG = 0, irB = 0;
                int count = 0;

                // Uniform hemisphere sampling
                for (int s = 0; s < sampleCount; ++s)
                {
                    // Stratified random
                    float xi1 = (static_cast<float>(s) + 0.5f) / sampleCount;
                    float xi2 = static_cast<float>((s * 7 + 3) % sampleCount) / sampleCount;

                    float phi = 2.0f * PI * xi1;
                    float cosTheta = std::sqrt(1.0f - xi2); // cosine-weighted
                    float sinTheta = std::sqrt(xi2);

                    // Tangent space to world
                    float hx = sinTheta * std::cos(phi);
                    float hy = sinTheta * std::sin(phi);
                    float hz = cosTheta;

                    float wx = hx * tx + hy * bx + hz * N.x;
                    float wy = hx * ty + hy * by + hz * N.y;
                    float wz = hx * tz + hy * bz + hz * N.z;

                    float sr, sg, sb;
                    sampleCubemap(faces, srcSize, wx, wy, wz, sr, sg, sb);

                    irR += sr;
                    irG += sg;
                    irB += sb;
                    ++count;
                }

                float* dst = outFaces[f].data() + (y * dstSize + x) * 4;
                dst[0] = irR / count;
                dst[1] = irG / count;
                dst[2] = irB / count;
                dst[3] = 1.0f;
            }
        }
    }
}

// ─── Specular Prefiltering ─────────────────────────────────

// Hammersley sequence
static float radicalInverse(uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

static void hammersley(uint32_t i, uint32_t N, float& xi1, float& xi2)
{
    xi1 = static_cast<float>(i) / static_cast<float>(N);
    xi2 = radicalInverse(i);
}

// GGX importance sampling
static DirectX::XMFLOAT3 importanceSampleGGX(float xi1, float xi2, float roughness, const DirectX::XMFLOAT3& N)
{
    float a = roughness * roughness;
    float phi = 2.0f * PI * xi1;
    float cosTheta = std::sqrt((1.0f - xi2) / (1.0f + (a * a - 1.0f) * xi2));
    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

    // Tangent space half vector
    float hx = sinTheta * std::cos(phi);
    float hy = sinTheta * std::sin(phi);
    float hz = cosTheta;

    // Build TBN
    DirectX::XMFLOAT3 up = (std::abs(N.y) < 0.999f) ? DirectX::XMFLOAT3{0, 1, 0} : DirectX::XMFLOAT3{1, 0, 0};
    float tx = up.y * N.z - up.z * N.y;
    float ty = up.z * N.x - up.x * N.z;
    float tz = up.x * N.y - up.y * N.x;
    float tlen = std::sqrt(tx * tx + ty * ty + tz * tz);
    tx /= tlen;
    ty /= tlen;
    tz /= tlen;
    float bx = N.y * tz - N.z * ty;
    float by = N.z * tx - N.x * tz;
    float bz = N.x * ty - N.y * tx;

    float wx = hx * tx + hy * bx + hz * N.x;
    float wy = hx * ty + hy * by + hz * N.y;
    float wz = hx * tz + hy * bz + hz * N.z;
    float wlen = std::sqrt(wx * wx + wy * wy + wz * wz);
    return {wx / wlen, wy / wlen, wz / wlen};
}

void IBLProcessor::prefilterSpecular(const std::vector<float> faces[6], int srcSize, int dstSize, float roughness,
                                     std::vector<float> outFaces[6])
{
    const uint32_t sampleCount = 256;

    for (int f = 0; f < 6; ++f)
    {
        outFaces[f].resize(static_cast<size_t>(dstSize) * dstSize * 4);

        for (int y = 0; y < dstSize; ++y)
        {
            for (int x = 0; x < dstSize; ++x)
            {
                float u = (static_cast<float>(x) + 0.5f) / dstSize * 2.0f - 1.0f;
                float v = (static_cast<float>(y) + 0.5f) / dstSize * 2.0f - 1.0f;
                auto N = cubemapDirection(f, u, v);
                // V = N (view = reflection direction for prefiltering)

                float totalWeight = 0;
                float prefR = 0, prefG = 0, prefB = 0;

                for (uint32_t i = 0; i < sampleCount; ++i)
                {
                    float xi1, xi2;
                    hammersley(i, sampleCount, xi1, xi2);
                    auto H = importanceSampleGGX(xi1, xi2, roughness, N);

                    // Reflect V around H to get L
                    float VdotH = N.x * H.x + N.y * H.y + N.z * H.z;
                    float lx = 2.0f * VdotH * H.x - N.x;
                    float ly = 2.0f * VdotH * H.y - N.y;
                    float lz = 2.0f * VdotH * H.z - N.z;

                    float NdotL = N.x * lx + N.y * ly + N.z * lz;
                    if (NdotL > 0)
                    {
                        float sr, sg, sb;
                        sampleCubemap(faces, srcSize, lx, ly, lz, sr, sg, sb);
                        prefR += sr * NdotL;
                        prefG += sg * NdotL;
                        prefB += sb * NdotL;
                        totalWeight += NdotL;
                    }
                }

                float* dst = outFaces[f].data() + (y * dstSize + x) * 4;
                if (totalWeight > 0)
                {
                    dst[0] = prefR / totalWeight;
                    dst[1] = prefG / totalWeight;
                    dst[2] = prefB / totalWeight;
                }
                else
                {
                    dst[0] = dst[1] = dst[2] = 0;
                }
                dst[3] = 1.0f;
            }
        }
    }
}

// ─── BRDF LUT ──────────────────────────────────────────────

void IBLProcessor::generateBRDFLut(int size, std::vector<float>& outPixels)
{
    const uint32_t sampleCount = 1024;
    outPixels.resize(static_cast<size_t>(size) * size * 2); // RG only

    for (int y = 0; y < size; ++y)
    {
        float roughness = (static_cast<float>(y) + 0.5f) / size;
        roughness = std::max(roughness, 0.01f);

        for (int x = 0; x < size; ++x)
        {
            float NdotV = (static_cast<float>(x) + 0.5f) / size;
            NdotV = std::max(NdotV, 0.001f);

            DirectX::XMFLOAT3 V = {std::sqrt(1.0f - NdotV * NdotV), 0, NdotV};
            DirectX::XMFLOAT3 N = {0, 0, 1};

            float A = 0, B = 0;

            for (uint32_t i = 0; i < sampleCount; ++i)
            {
                float xi1, xi2;
                hammersley(i, sampleCount, xi1, xi2);
                auto H = importanceSampleGGX(xi1, xi2, roughness, N);

                float VdotH = V.x * H.x + V.y * H.y + V.z * H.z;
                float lx = 2.0f * VdotH * H.x - V.x;
                float ly = 2.0f * VdotH * H.y - V.y;
                float lz = 2.0f * VdotH * H.z - V.z;

                float NdotL = std::max(lz, 0.0f);
                float NdotH = std::max(H.z, 0.0f);
                VdotH = std::max(VdotH, 0.0f);

                if (NdotL > 0)
                {
                    float r2 = roughness * roughness;
                    float k = (r2) / 2.0f;

                    float G_V = NdotV / (NdotV * (1.0f - k) + k);
                    float G_L = NdotL / (NdotL * (1.0f - k) + k);
                    float G = G_V * G_L;

                    float G_Vis = (G * VdotH) / (NdotH * NdotV + 0.0001f);
                    float Fc = std::pow(1.0f - VdotH, 5.0f);

                    A += (1.0f - Fc) * G_Vis;
                    B += Fc * G_Vis;
                }
            }

            float* dst = outPixels.data() + (y * size + x) * 2;
            dst[0] = A / sampleCount;
            dst[1] = B / sampleCount;
        }
    }
}

// ─── Main Processing Pipeline ──────────────────────────────

IBLData IBLProcessor::process(const std::filesystem::path& hdriPath, int irradianceSize, int prefilteredSize,
                              int brdfLutSize)
{
    IBLData data;
    data.irradianceSize = irradianceSize;
    data.prefilteredSize = prefilteredSize;
    data.brdfLutSize = brdfLutSize;

    spdlog::info("IBL: Processing {}", hdriPath.filename().string());

    // 1. Load HDRI
    int eqW, eqH;
    std::vector<float> equirect;
    if (!loadHDRI(hdriPath, eqW, eqH, equirect))
    {
        return data;
    }
    spdlog::info("IBL: Loaded {}x{} equirect", eqW, eqH);

    // 2. Equirect → Cubemap
    int cubemapSize = std::min(prefilteredSize * 2, 1024);
    std::vector<float> cubeFaces[6];
    equirectToCubemap(equirect.data(), eqW, eqH, cubemapSize, cubeFaces);
    spdlog::info("IBL: Cubemap generated ({}x{})", cubemapSize, cubemapSize);

    // 3. Irradiance convolution
    convolveIrradiance(cubeFaces, cubemapSize, irradianceSize, data.irradianceFaces);
    spdlog::info("IBL: Irradiance map generated ({}x{})", irradianceSize, irradianceSize);

    // 4. Prefiltered specular (mip chain)
    int maxMips = static_cast<int>(std::log2(prefilteredSize)) + 1;
    data.prefilteredMipLevels = maxMips;

    for (int f = 0; f < 6; ++f)
    {
        data.prefilteredFaces[f].resize(maxMips);
    }

    for (int mip = 0; mip < maxMips; ++mip)
    {
        float roughness = static_cast<float>(mip) / static_cast<float>(maxMips - 1);
        int mipSize = std::max(prefilteredSize >> mip, 1);

        std::vector<float> mipFaces[6];
        prefilterSpecular(cubeFaces, cubemapSize, mipSize, roughness, mipFaces);

        for (int f = 0; f < 6; ++f)
        {
            data.prefilteredFaces[f][mip].size = mipSize;
            data.prefilteredFaces[f][mip].pixels = std::move(mipFaces[f]);
        }
    }
    spdlog::info("IBL: Prefiltered specular generated ({} mips)", maxMips);

    // 5. BRDF LUT
    generateBRDFLut(brdfLutSize, data.brdfLutPixels);
    spdlog::info("IBL: BRDF LUT generated ({}x{})", brdfLutSize, brdfLutSize);

    data.valid = true;
    return data;
}

// ─── List HDRIs ────────────────────────────────────────────

std::vector<std::filesystem::path> IBLProcessor::listHDRIs(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::is_directory(directory))
        return result;

    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
            continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".exr" || ext == ".hdr" || ext == ".dds")
        {
            result.push_back(entry.path());
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

} // namespace tpbr
