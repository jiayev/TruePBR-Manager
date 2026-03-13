#pragma once

#include "DescriptorHeap.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace tpbr
{

using Microsoft::WRL::ComPtr;

/// GPU IBL processing result — resources ready for rendering.
struct IBLResult
{
    ComPtr<ID3D12Resource> irradianceCubemap; // 6-face cubemap, RGBA32F
    int irradianceSize = 0;

    ComPtr<ID3D12Resource> prefilteredCubemap; // 6-face cubemap with mip chain, RGBA32F
    int prefilteredSize = 0;
    int prefilteredMipLevels = 0;

    ComPtr<ID3D12Resource> brdfLut; // 2D R32G32_FLOAT
    int brdfLutSize = 0;

    bool valid = false;
};

/// GPU compute pipeline for IBL processing.
/// Runs equirect-to-cubemap, irradiance convolution, specular prefiltering,
/// and BRDF LUT generation entirely on the GPU via compute shaders.
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
    /// Uploads the equirect texture, runs 4 GPU compute passes, returns
    /// IBL resources in PIXEL_SHADER_RESOURCE state.
    IBLResult process(ID3D12Device* device, ID3D12CommandQueue* directQueue, const float* equirectPixels, int equirectW,
                      int equirectH, int irradianceSize = 64, int prefilteredSize = 256, int brdfLutSize = 256);

    bool isInitialized() const
    {
        return m_initialized;
    }

  private:
    // ── Shader compilation & PSO creation ──────────────────
    bool compileComputeShader(const std::filesystem::path& hlslPath, const char* entryPoint, ComPtr<ID3DBlob>& outBlob);
    bool createRootSignatureAndPSO(ID3D12Device* device, const char* name, const ComPtr<ID3DBlob>& csBlob, int numSRVs,
                                   int numUAVs, int numSamplers, ComPtr<ID3D12RootSignature>& outRS,
                                   ComPtr<ID3D12PipelineState>& outPSO);

    // ── Helpers ────────────────────────────────────────────
    ComPtr<ID3D12Resource> createDefaultTexture2D(ID3D12Device* device, int w, int h, DXGI_FORMAT format,
                                                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState);
    ComPtr<ID3D12Resource> createDefaultTextureCube(ID3D12Device* device, int faceSize, int mipLevels,
                                                    DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                                                    D3D12_RESOURCE_STATES initialState);

    // ── Individual compute passes ──────────────────────────
    void runEquirectToCubemap(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* equirectTex,
                              ID3D12Resource* outputCubemap, int faceSize, DescriptorHeap& heap);
    void runIrradiance(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* inputCubemap,
                       ID3D12Resource* outputIrradiance, int inputSize, int outputSize, DescriptorHeap& heap);
    void runPrefilter(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* inputCubemap,
                      ID3D12Resource* outputPrefiltered, int inputSize, int outputSize, int mipLevels,
                      DescriptorHeap& heap);
    void runBrdfLut(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* outputLut, int lutSize,
                    DescriptorHeap& heap);

    bool m_initialized = false;

    // Per-pass root signature + PSO
    ComPtr<ID3D12RootSignature> m_equirectRS;
    ComPtr<ID3D12PipelineState> m_equirectPSO;

    ComPtr<ID3D12RootSignature> m_irradianceRS;
    ComPtr<ID3D12PipelineState> m_irradiancePSO;

    ComPtr<ID3D12RootSignature> m_prefilterRS;
    ComPtr<ID3D12PipelineState> m_prefilterPSO;

    ComPtr<ID3D12RootSignature> m_brdfLutRS;
    ComPtr<ID3D12PipelineState> m_brdfLutPSO;

    // Transient command infrastructure (created in process(), destroyed after)
    ComPtr<ID3D12CommandAllocator> m_cmdAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    uint64_t m_fenceValue = 0;
};

} // namespace tpbr
