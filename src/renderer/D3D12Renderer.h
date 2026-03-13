#pragma once

#include "MeshGenerator.h"

#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace tpbr
{

using Microsoft::WRL::ComPtr;

/// Scene constant buffer data (matches cbuffer SceneCB in shader)
struct SceneCBData
{
    DirectX::XMFLOAT4X4 worldViewProj;
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4X4 worldInvTranspose;
    DirectX::XMFLOAT3 cameraPos;
    float _pad0;
    DirectX::XMFLOAT3 lightDir;
    float _pad1;
    DirectX::XMFLOAT3 lightColor;
    float lightIntensity;
    float iblIntensity;
    float maxPrefilteredMip;
    float _pad2;
    float _pad3;
};

/// Material constant buffer data (matches cbuffer MaterialCB in shader)
struct MaterialCBData
{
    float specularLevel;
    float roughnessScale;
    float _pad0;
    float _pad1;
};

/// Minimal D3D12 renderer for PBR material preview.
/// Renders a single mesh with PBR textures under directional lighting.
class D3D12Renderer
{
  public:
    D3D12Renderer();
    ~D3D12Renderer();

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

    /// Load IBL environment from file. Processes HDRI and uploads cubemaps.
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

  private:
    static constexpr uint32_t FrameCount = 2;

    bool createDevice();
    bool createCommandQueue();
    bool createSwapChain(HWND hwnd, uint32_t width, uint32_t height);
    bool createRTVHeap();
    bool createDSV(uint32_t width, uint32_t height);
    bool createSRVHeap();
    bool createRootSignatureAndPSO();
    bool createConstantBuffers();
    void createDefaultTextures();
    void uploadMesh(const PreviewMesh& mesh);
    void uploadTexture(int srvIndex, const uint8_t* rgba, int w, int h, bool srgb);
    void uploadCubemap(int srvIndex, ComPtr<ID3D12Resource>& resource, const std::vector<float>* faces, int faceSize,
                       int mipLevels, const std::vector<std::vector<float>>* mipFaces);
    void uploadBRDFLut(int srvIndex, const float* rgPixels, int size);
    void createDefaultIBL();
    void waitForGPU();

    bool m_initialized = false;

    // Device
    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    // Swap chain
    ComPtr<IDXGISwapChain3> m_swapChain;
    std::array<ComPtr<ID3D12Resource>, FrameCount> m_renderTargets;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    uint32_t m_rtvDescriptorSize = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // Depth
    ComPtr<ID3D12Resource> m_depthStencilBuffer;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    // SRV heap: 6 slots
    // 0=diffuse, 1=normal, 2=rmaos, 3=irradiance cubemap, 4=prefiltered cubemap, 5=BRDF LUT
    static constexpr uint32_t SRVCount = 6;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    uint32_t m_srvDescriptorSize = 0;
    std::array<ComPtr<ID3D12Resource>, 3> m_textures;
    std::array<ComPtr<ID3D12Resource>, 3> m_textureUploadHeaps;

    // IBL resources
    ComPtr<ID3D12Resource> m_irradianceCubemap;
    ComPtr<ID3D12Resource> m_prefilteredCubemap;
    ComPtr<ID3D12Resource> m_brdfLut;
    bool m_iblLoaded = false;
    float m_iblIntensity = 1.0f;
    int m_maxPrefilteredMip = 0;

    // Pipeline
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    // Mesh
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView{};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView{};
    uint32_t m_indexCount = 0;

    // Constant buffers
    ComPtr<ID3D12Resource> m_sceneCB;
    ComPtr<ID3D12Resource> m_materialCB;
    SceneCBData* m_sceneCBMapped = nullptr;
    MaterialCBData* m_materialCBMapped = nullptr;

    // Sync
    ComPtr<ID3D12Fence> m_fence;
    uint64_t m_fenceValue = 0;
    HANDLE m_fenceEvent = nullptr;

    // Camera — start slightly elevated, looking at front face
    float m_azimuth = 0.8f;
    float m_elevation = 0.4f;
    float m_distance = 3.0f;

    // Light — from upper-front-left, toward the visible face
    DirectX::XMFLOAT3 m_lightDir = {0.4f, 0.7f, -0.5f};
    DirectX::XMFLOAT3 m_lightColor = {1.0f, 1.0f, 1.0f};
    float m_lightIntensity = 3.0f;

    // Material
    float m_specularLevel = 0.04f;
    float m_roughnessScale = 1.0f;
};

} // namespace tpbr
