#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tpbr
{

/// Processed IBL data ready for GPU upload.
/// All maps are stored as float RGBA pixels.
struct IBLData
{
    // Irradiance cubemap (diffuse IBL) — 6 faces, low resolution
    int irradianceSize = 64;
    std::vector<float> irradianceFaces[6]; // RGBA float per face

    // Prefiltered specular cubemap — 6 faces x N mip levels
    int prefilteredSize = 256;
    int prefilteredMipLevels = 0;
    struct MipFace
    {
        int size;
        std::vector<float> pixels; // RGBA float
    };
    std::vector<MipFace> prefilteredFaces[6]; // [face][mip]

    // BRDF integration LUT — 2D texture (R16G16 or RGBA float)
    int brdfLutSize = 256;
    std::vector<float> brdfLutPixels; // RG float (2 channels per pixel)

    bool valid = false;
};

/// Processes HDRI environment maps into IBL data for PBR rendering.
///
/// Supports loading:
/// - .exr (OpenEXR via tinyexr)
/// - .hdr (Radiance HDR via DirectXTex)
/// - .dds (DirectX DDS via DirectXTex)
///
/// Processing pipeline (all CPU-side):
/// 1. Load equirectangular HDRI → float RGBA pixels
/// 2. Convert equirect → cubemap (6 faces)
/// 3. Convolve cubemap → irradiance map (diffuse IBL)
/// 4. Prefilter cubemap → specular mip chain (split-sum specular IBL)
/// 5. Compute BRDF integration LUT
class IBLProcessor
{
  public:
    /// Load an HDRI file and generate all IBL maps.
    static IBLData process(const std::filesystem::path& hdriPath, int irradianceSize = 64, int prefilteredSize = 256,
                           int brdfLutSize = 256);

    /// Generate only the BRDF LUT (can be done once and reused).
    static void generateBRDFLut(int size, std::vector<float>& outPixels);

    /// List available HDRI files in a directory (*.exr, *.hdr, *.dds).
    static std::vector<std::filesystem::path> listHDRIs(const std::filesystem::path& directory);

    /// Load HDRI to equirectangular float RGBA.
    /// Public so the GPU IBL pipeline can use it for file loading only.
    static bool loadHDRI(const std::filesystem::path& path, int& width, int& height, std::vector<float>& rgbaPixels);

  private:
    /// Convert equirectangular map to cubemap faces.
    static void equirectToCubemap(const float* equirect, int eqW, int eqH, int faceSize, std::vector<float> faces[6]);

    /// Convolve cubemap to irradiance map.
    static void convolveIrradiance(const std::vector<float> faces[6], int srcSize, int dstSize,
                                   std::vector<float> outFaces[6]);

    /// Prefilter cubemap for specular IBL at a given roughness level.
    static void prefilterSpecular(const std::vector<float> faces[6], int srcSize, int dstSize, float roughness,
                                  std::vector<float> outFaces[6]);

    /// Sample cubemap with a direction vector.
    static void sampleCubemap(const std::vector<float> faces[6], int faceSize, float x, float y, float z, float& outR,
                              float& outG, float& outB);
};

} // namespace tpbr
