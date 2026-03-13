#pragma once

#include "D3D12UploadQueue.h"
#include "DescriptorHeap.h"
#include "IBLProcessor.h"
#include "MeshGenerator.h"

#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace tpbr
{

using Microsoft::WRL::ComPtr;

class IBLPipeline;

/// Scene constant buffer data (matches cbuffer SceneCB in shader) — 256-byte aligned.
struct SceneCBData
{
    DirectX::XMFLOAT4X4 worldViewProj;     // 64 bytes
    DirectX::XMFLOAT4X4 world;             // 64 bytes
    DirectX::XMFLOAT4X4 worldInvTranspose; // 64 bytes
    DirectX::XMFLOAT3 cameraPos;           // 12 bytes
    float _pad0;                           // 4 bytes
    DirectX::XMFLOAT3 lightDir;            // 12 bytes
    float _pad1;                           // 4 bytes
    DirectX::XMFLOAT3 lightColor;          // 12 bytes
    float lightIntensity;                  // 4 bytes
    float iblIntensity;                    // 4 bytes
    float maxPrefilteredMip;               // 4 bytes
    float _pad2;                           // 4 bytes
    float _pad3;                           // 4 bytes
    // ZH3 irradiance: pre-convolved SH2 + ZH3 zonal coefficient
    DirectX::XMFLOAT4 zh3Data[5];    // 80 bytes
    DirectX::XMFLOAT4X4 invViewProj; // 64 bytes  => total 400 bytes
};

/// Material constant buffer data (matches cbuffer MaterialCB in shader).
struct MaterialCBData
{
    float specularLevel;
    float roughnessScale;
    float _pad0;
    float _pad1;
};

/// Per-frame resources for double-buffered rendering.
struct FrameContext
{
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    uint64_t fenceValue = 0;

    // Per-frame constant buffers (persistently mapped)
    ComPtr<ID3D12Resource> sceneCB;
    ComPtr<ID3D12Resource> materialCB;
    SceneCBData* sceneCBMapped = nullptr;
    MaterialCBData* materialCBMapped = nullptr;
};

/// D3D12 renderer for PBR material preview — rewritten with proper
/// double-buffered frame management, async copy queue uploads,
/// and GPU-based IBL processing.
class D3D12Renderer
{
  public:
    D3D12Renderer();
    ~D3D12Renderer();

    // Non-copyable
    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;

    /// Initialize the renderer for the given window handle and size.
    bool init(HWND hwnd, uint32_t width, uint32_t height);

    /// Resize the swap chain.
    void resize(uint32_t width, uint32_t height);

    /// Set the preview mesh shape.
    void setMesh(PreviewShape shape);

    /// Load PBR textures from RGBA pixel data.
    /// Pass nullptr/0 for any texture to use a default (white diffuse, flat normal, default RMAOS).
    void setTextures(const uint8_t* diffuseRGBA, int diffuseW, int diffuseH, const uint8_t* normalRGBA, int normalW,
                     int normalH, const uint8_t* rmaosRGBA, int rmaosW, int rmaosH);

    /// Set material parameters.
    void setMaterialParams(float specularLevel, float roughnessScale);

    /// Set camera orbit (spherical coordinates).
    void setCamera(float azimuth, float elevation, float distance);

    /// Set light direction (normalized).
    void setLightDirection(float x, float y, float z);

    /// Set light color (linear RGB, default 1,1,1).
    void setLightColor(float r, float g, float b);

    /// Set light intensity (default 3.0).
    void setLightIntensity(float intensity);

    /// Load IBL environment from file. Processes HDRI via GPU compute shaders.
    bool loadIBL(const std::filesystem::path& hdriPath);

    /// Set IBL intensity (0 = disabled, default 1.0 when IBL loaded).
    void setIBLIntensity(float intensity);

    /// Render one frame.
    void render();

    /// Check if initialized.
    bool isInitialized() const
    {
        return m_initialized;
    }

    /// Get the D3D12 device (needed by IBLPipeline).
    ID3D12Device* device() const
    {
        return m_device.Get();
    }

    /// Get the direct command queue (needed by IBLPipeline).
    ID3D12CommandQueue* directQueue() const
    {
        return m_directQueue.Get();
    }

  private:
    static constexpr uint32_t FrameCount = 2;

    // ── Initialization ─────────────────────────────────────
    bool createDevice();
    bool createCommandInfrastructure();
    bool createSwapChain(HWND hwnd, uint32_t width, uint32_t height);
    bool createRenderTargets();
    bool createDepthStencil(uint32_t width, uint32_t height);
    bool createRootSignatureAndPSO();
    bool createFrameContexts();
    void createDefaultTextures();
    void createDefaultIBL();

    // ── Resource upload ────────────────────────────────────
    void uploadTexture(int srvIndex, const uint8_t* rgba, int w, int h, bool srgb);
    void uploadCubemap(int srvIndex, ComPtr<ID3D12Resource>& resource, const std::vector<float>* faces, int faceSize,
                       int mipLevels);
    void uploadCubemapMipped(int srvIndex, ComPtr<ID3D12Resource>& resource,
                             const std::vector<IBLData::MipFace>* faceMips, int faceSize, int mipLevels);
    void uploadBRDFLut(int srvIndex, const float* rgPixels, int size);
    void uploadMesh(const PreviewMesh& mesh);

    // ── Synchronization ────────────────────────────────────
    void waitForFrame(uint32_t frameIndex);
    void flushGPU();

    // ── State ──────────────────────────────────────────────
    bool m_initialized = false;
    uint64_t m_frameNumber = 0; // Monotonically increasing frame counter

    // Device & Factory
    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<ID3D12Device> m_device;

    // Direct queue (rendering + compute)
    ComPtr<ID3D12CommandQueue> m_directQueue;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Fence> m_directFence;
    HANDLE m_directFenceEvent = nullptr;
    uint64_t m_directFenceValue = 0;

    // Copy queue (async uploads)
    std::unique_ptr<D3D12UploadQueue> m_uploadQueue;

    // Double-buffered frame contexts
    std::array<FrameContext, FrameCount> m_frames;

    // Swap chain
    ComPtr<IDXGISwapChain3> m_swapChain;
    std::array<ComPtr<ID3D12Resource>, FrameCount> m_renderTargets;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // Descriptor heaps
    DescriptorHeap m_rtvHeap; // Non-visible, RTV
    DescriptorHeap m_dsvHeap; // Non-visible, DSV
    DescriptorHeap m_srvHeap; // Shader-visible, CBV/SRV/UAV

    // SRV slot indices within m_srvHeap
    static constexpr uint32_t SRVCount = 5; // diffuse, normal, rmaos, prefiltered, brdfLut
    uint32_t m_srvBaseIndex = 0;            // First SRV slot index in heap

    // Depth
    ComPtr<ID3D12Resource> m_depthStencilBuffer;

    // Textures (material)
    std::array<ComPtr<ID3D12Resource>, 3> m_textures; // diffuse, normal, rmaos

    // IBL resources
    ComPtr<ID3D12Resource> m_prefilteredCubemap;
    ComPtr<ID3D12Resource> m_brdfLut;
    bool m_iblLoaded = false;
    float m_iblIntensity = 1.0f;
    int m_maxPrefilteredMip = 0;
    float m_zh3Data[5][4] = {}; // Pre-convolved ZH3: SH2[0..3] + ZH3 coefficient

    // IBL pipeline (GPU compute)
    std::unique_ptr<IBLPipeline> m_iblPipeline;

    // Pipeline state
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12PipelineState> m_skyboxPSO;

    // Mesh
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView{};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView{};
    uint32_t m_indexCount = 0;

    // Camera — start slightly elevated, looking at front face
    float m_azimuth = 0.8f;
    float m_elevation = 0.4f;
    float m_distance = 3.0f;

    // Light — from upper-front-left
    DirectX::XMFLOAT3 m_lightDir = {0.4f, 0.7f, -0.5f};
    DirectX::XMFLOAT3 m_lightColor = {1.0f, 1.0f, 1.0f};
    float m_lightIntensity = 3.0f;

    // Material
    float m_specularLevel = 0.04f;
    float m_roughnessScale = 1.0f;
};

} // namespace tpbr
