#pragma once

#include "DescriptorHeap.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tpbr
{

using Microsoft::WRL::ComPtr;

/// GPU IBL processing result — resources ready for rendering.
struct IBLResult
{
    float zh3Data[5][4] = {}; // Pre-convolved ZH3: SH2[0..3] + ZH3 coefficient, each float4 (.xyz=RGB)

    ComPtr<ID3D12Resource> prefilteredCubemap; // 6-face cubemap with mip chain, RGBA32F
    int prefilteredSize = 0;
    int prefilteredMipLevels = 0;

    ComPtr<ID3D12Resource> skyboxCubemap; // Full-resolution intermediate cubemap for skybox display
    int skyboxCubemapSize = 0;

    ComPtr<ID3D12Resource> brdfLut; // 2D R32G32_FLOAT
    int brdfLutSize = 0;

    bool valid = false;
};

/// GPU compute pipeline for IBL processing.
/// Runs equirect-to-cubemap, cubemap SH3 projection + ZH3 extraction,
/// specular prefiltering, and BRDF LUT generation on the GPU.
class IBLPipeline
{
  public:
    IBLPipeline() = default;
    ~IBLPipeline();

    IBLPipeline(const IBLPipeline&) = delete;
    IBLPipeline& operator=(const IBLPipeline&) = delete;

    /// Initialize: compile shaders, create root signatures + PSOs.
    bool init(ID3D12Device* device);

    /// Process equirectangular HDRI (float RGBA pixels already loaded on CPU).
    /// Uploads the equirect texture, runs GPU compute passes, returns
    /// IBL resources in PIXEL_SHADER_RESOURCE state.
    /// @param prefilterSamples  Number of GGX importance samples per texel for specular prefiltering.
    IBLResult process(ID3D12Device* device, ID3D12CommandQueue* directQueue, const float* equirectPixels, int equirectW,
                      int equirectH, int prefilteredSize = 256, int brdfLutSize = 256, int prefilterSamples = 256);

    /// CPU fallback: project equirect to ZH3 coefficients (for when GPU pipeline unavailable).
    static void computeZH3CPU(const float* pixels, int w, int h, float outZH3[5][4]);

    /// Load an HDRI file (.exr, .hdr, .dds) to equirectangular float RGBA pixels.
    static bool loadHDRI(const std::filesystem::path& path, int& width, int& height, std::vector<float>& rgbaPixels);

    /// List available HDRI files in a directory (*.exr, *.hdr, *.dds).
    static std::vector<std::filesystem::path> listHDRIs(const std::filesystem::path& directory);

    bool isInitialized() const
    {
        return m_initialized;
    }

  private:
    // ── Shader loading & PSO creation ───────────────────────
    bool loadCompiledShader(const std::filesystem::path& csoPath, std::vector<uint8_t>& outData);
    bool createRootSignatureAndPSO(ID3D12Device* device, const char* name, const void* csBytecode, size_t bytecodeSize,
                                   int numSRVs, int numUAVs, int numSamplers, ComPtr<ID3D12RootSignature>& outRS,
                                   ComPtr<ID3D12PipelineState>& outPSO, int numRootConstants = 4);

    // ── Helpers ────────────────────────────────────────────
    ComPtr<ID3D12Resource> createDefaultTexture2D(ID3D12Device* device, int w, int h, DXGI_FORMAT format,
                                                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState);
    ComPtr<ID3D12Resource> createDefaultTextureCube(ID3D12Device* device, int faceSize, int mipLevels,
                                                    DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                                                    D3D12_RESOURCE_STATES initialState);
    ComPtr<ID3D12Resource> createDefaultBuffer(ID3D12Device* device, UINT64 size, D3D12_RESOURCE_FLAGS flags,
                                               D3D12_RESOURCE_STATES initialState);

    // ── Individual compute passes ──────────────────────────
    void runEquirectToCubemap(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* equirectTex,
                              ID3D12Resource* outputCubemap, int faceSize, DescriptorHeap& heap);
    void runPrefilter(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* inputCubemap,
                      ID3D12Resource* outputPrefiltered, int inputSize, int outputSize, int mipLevels,
                      DescriptorHeap& heap, int sampleCount = 256, int inputMipLevels = 1);
    void runCubemapMipGen(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cubemap,
                          int faceSize, int mipLevels, DescriptorHeap& heap);
    void runBrdfLut(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* outputLut, int lutSize,
                    DescriptorHeap& heap);
    void runDiffuseIrradiance(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* cubemap,
                              ID3D12Resource* zh3OutBuf, int cubeSize, int cubeMipLevels, DescriptorHeap& heap);

    /// Compute roughness for a given prefilter mip level.
    static float computeRoughnessFromMip(int mip, int maxMip);

    bool m_initialized = false;

    // Per-pass root signature + PSO
    ComPtr<ID3D12RootSignature> m_equirectRS;
    ComPtr<ID3D12PipelineState> m_equirectPSO;

    ComPtr<ID3D12RootSignature> m_prefilterRS;
    ComPtr<ID3D12PipelineState> m_prefilterPSO;

    ComPtr<ID3D12RootSignature> m_brdfLutRS;
    ComPtr<ID3D12PipelineState> m_brdfLutPSO;

    ComPtr<ID3D12RootSignature> m_diffuseIrradianceRS;
    ComPtr<ID3D12PipelineState> m_diffuseIrradiancePSO;

    ComPtr<ID3D12RootSignature> m_mipGenRS;
    ComPtr<ID3D12PipelineState> m_mipGenPSO;

    // Transient command infrastructure (created in process(), destroyed after)
    ComPtr<ID3D12CommandAllocator> m_cmdAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    uint64_t m_fenceValue = 0;
};

} // namespace tpbr
