#include "D3D12Renderer.h"
#include "IBLPipeline.h"
#include "IBLProcessor.h"
#include "utils/Log.h"

#include <DirectXMath.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace tpbr
{

using namespace DirectX;

D3D12Renderer::D3D12Renderer() = default;

D3D12Renderer::~D3D12Renderer()
{
    if (m_initialized)
    {
        flushGPU();
    }
    // Unmap persistent CB mappings
    for (auto& frame : m_frames)
    {
        if (frame.sceneCBMapped && frame.sceneCB)
        {
            frame.sceneCB->Unmap(0, nullptr);
            frame.sceneCBMapped = nullptr;
        }
        if (frame.materialCBMapped && frame.materialCB)
        {
            frame.materialCB->Unmap(0, nullptr);
            frame.materialCBMapped = nullptr;
        }
    }
    if (m_directFenceEvent)
    {
        CloseHandle(m_directFenceEvent);
        m_directFenceEvent = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════
// Initialization
// ═══════════════════════════════════════════════════════════

bool D3D12Renderer::init(HWND hwnd, uint32_t width, uint32_t height)
{
    m_width = width;
    m_height = height;

    spdlog::info("D3D12Renderer::init starting ({}x{}, hwnd=0x{:X})", width, height, reinterpret_cast<uintptr_t>(hwnd));

    if (!createDevice())
        return false;
    if (!createCommandInfrastructure())
        return false;
    if (!createSwapChain(hwnd, width, height))
        return false;
    if (!createRenderTargets())
        return false;
    if (!createDepthStencil(width, height))
        return false;
    if (!createRootSignatureAndPSO())
        return false;
    if (!createFrameContexts())
        return false;

    createDefaultTextures();
    createDefaultIBL();
    setMesh(PreviewShape::Sphere);

    // Initialize GPU IBL pipeline (non-fatal if it fails — CPU fallback exists)
    m_iblPipeline = std::make_unique<IBLPipeline>();
    if (!m_iblPipeline->init(m_device.Get()))
    {
        spdlog::warn("D3D12Renderer: GPU IBL pipeline init failed, will use CPU fallback");
        m_iblPipeline.reset();
    }

    m_initialized = true;
    spdlog::info("D3D12Renderer initialized ({}x{})", width, height);
    return true;
}

bool D3D12Renderer::createDevice()
{
    UINT dxgiFlags = 0;

#ifndef NDEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
        debug->EnableDebugLayer();
        dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;
        spdlog::info("D3D12 debug layer enabled");
    }
#endif

    if (FAILED(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&m_factory))))
    {
        spdlog::error("Failed to create DXGI factory");
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
        {
            char adapterName[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapterName, sizeof(adapterName), nullptr, nullptr);
            spdlog::info("D3D12 device created on adapter: {}", adapterName);
            break;
        }
    }

    if (!m_device)
    {
        spdlog::error("Failed to create D3D12 device");
        return false;
    }

    return true;
}

bool D3D12Renderer::createCommandInfrastructure()
{
    // Direct command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_directQueue))))
    {
        spdlog::error("Failed to create direct command queue");
        return false;
    }

    // Direct queue fence
    if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_directFence))))
    {
        spdlog::error("Failed to create direct fence");
        return false;
    }
    m_directFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_directFenceValue = 0;

    // Command list (created in closed state)
    // We don't create a shared allocator — each FrameContext has its own.
    // The command list will be reset with the current frame's allocator each frame.
    // For initial uploads (before any frame), we'll temporarily use frame 0's allocator.

    // Upload queue (copy queue + ring buffer)
    m_uploadQueue = std::make_unique<D3D12UploadQueue>();
    if (!m_uploadQueue->init(m_device.Get()))
    {
        spdlog::error("Failed to initialize upload queue");
        return false;
    }

    spdlog::debug("D3D12Renderer: command infrastructure created");
    return true;
}

bool D3D12Renderer::createSwapChain(HWND hwnd, uint32_t width, uint32_t height)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = FrameCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapChain;
    HRESULT hr = m_factory->CreateSwapChainForHwnd(m_directQueue.Get(), hwnd, &desc, nullptr, nullptr, &swapChain);
    if (FAILED(hr))
    {
        spdlog::error("Failed to create swap chain: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    if (FAILED(swapChain.As(&m_swapChain)))
    {
        spdlog::error("Failed to get IDXGISwapChain3");
        return false;
    }

    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    return true;
}

bool D3D12Renderer::createRenderTargets()
{
    // RTV heap
    if (!m_rtvHeap.create(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount))
    {
        spdlog::error("Failed to create RTV heap");
        return false;
    }

    for (UINT i = 0; i < FrameCount; ++i)
    {
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_rtvHeap.cpuHandle(i));
    }
    return true;
}

bool D3D12Renderer::createDepthStencil(uint32_t width, uint32_t height)
{
    // DSV heap
    if (m_dsvHeap.capacity() == 0)
    {
        if (!m_dsvHeap.create(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1))
        {
            spdlog::error("Failed to create DSV heap");
            return false;
        }
    }

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = width;
    resDesc.Height = height;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_D32_FLOAT;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr =
        m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                          &clearValue, IID_PPV_ARGS(&m_depthStencilBuffer));
    if (FAILED(hr))
    {
        spdlog::error("Failed to create depth stencil buffer: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc, m_dsvHeap.cpuHandle(0));
    return true;
}

bool D3D12Renderer::createRootSignatureAndPSO()
{
    // SRV heap: shader-visible, enough for material SRVs + IBL compute UAVs later
    // Reserve 64 slots for flexibility (6 for PBR + extras for IBL compute)
    if (!m_srvHeap.create(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64, true))
    {
        spdlog::error("Failed to create SRV heap");
        return false;
    }
    m_srvBaseIndex = m_srvHeap.allocate(SRVCount);

    // Root signature: 2 inline CBVs + 1 descriptor table (6 SRVs) + 2 static samplers
    D3D12_ROOT_PARAMETER rootParams[3] = {};

    // b0: SceneCB
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // b1: MaterialCB
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 1;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // SRV table: t0..t5
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = SRVCount;
    srvRange.BaseShaderRegister = 0;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static samplers
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};

    // s0: linear wrap (material textures + cubemaps)
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: linear clamp (BRDF LUT)
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 3;
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 2;
    rsDesc.pStaticSamplers = samplers;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob)))
    {
        spdlog::error("Root signature serialization failed: {}",
                      errBlob ? (char*)errBlob->GetBufferPointer() : "unknown");
        return false;
    }
    m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                  IID_PPV_ARGS(&m_rootSignature));

    // Locate shader directory
    std::filesystem::path shaderDir;
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        auto exeDir = std::filesystem::path(exePath).parent_path();
        auto distDir = exeDir / "shaders";
        if (std::filesystem::exists(distDir / "PBRShader.hlsl"))
            shaderDir = distDir;
        else
            shaderDir = std::filesystem::path(__FILE__).parent_path();
    }

    // Compile PBR shaders
    std::wstring shaderPathW = (shaderDir / "PBRShader.hlsl").wstring();

    ComPtr<ID3DBlob> vsBlob, psBlob;
    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    auto compileShader = [&](const wchar_t* path, const char* entry, const char* target, ComPtr<ID3DBlob>& blob) -> bool
    {
        ComPtr<ID3DBlob> err;
        HRESULT hr = D3DCompileFromFile(path, nullptr, nullptr, entry, target, compileFlags, 0, &blob, &err);
        if (FAILED(hr))
        {
            spdlog::error("Shader compile failed ({}): {}", entry,
                          err ? (char*)err->GetBufferPointer() : "file not found");
            return false;
        }
        return true;
    };

    if (!compileShader(shaderPathW.c_str(), "VSMain", "vs_5_0", vsBlob))
        return false;
    if (!compileShader(shaderPathW.c_str(), "PSMain", "ps_5_0", psBlob))
        return false;

    // Input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
    psoDesc.InputLayout = {inputLayout, _countof(inputLayout)};
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState))))
    {
        spdlog::error("Failed to create PSO");
        return false;
    }

    // ── Skybox PSO (full-screen triangle, no depth write, no input layout) ──
    {
        std::wstring skyboxPathW = (shaderDir / "SkyboxShader.hlsl").wstring();
        ComPtr<ID3DBlob> skyVS, skyPS;
        if (!compileShader(skyboxPathW.c_str(), "SkyboxVS", "vs_5_0", skyVS))
            return false;
        if (!compileShader(skyboxPathW.c_str(), "SkyboxPS", "ps_5_0", skyPS))
            return false;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPso{};
        skyPso.pRootSignature = m_rootSignature.Get();
        skyPso.VS = {skyVS->GetBufferPointer(), skyVS->GetBufferSize()};
        skyPso.PS = {skyPS->GetBufferPointer(), skyPS->GetBufferSize()};
        skyPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        skyPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        skyPso.RasterizerState.DepthClipEnable = FALSE;
        skyPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        skyPso.DepthStencilState.DepthEnable = FALSE;
        skyPso.SampleMask = UINT_MAX;
        skyPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        skyPso.NumRenderTargets = 1;
        skyPso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        skyPso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        skyPso.SampleDesc.Count = 1;

        if (FAILED(m_device->CreateGraphicsPipelineState(&skyPso, IID_PPV_ARGS(&m_skyboxPSO))))
        {
            spdlog::error("Failed to create skybox PSO");
            return false;
        }
    }

    return true;
}

bool D3D12Renderer::createFrameContexts()
{
    auto createCB = [&](ComPtr<ID3D12Resource>& resource, void** mapped, uint32_t size) -> bool
    {
        uint32_t alignedSize = (size + 255) & ~255;
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = alignedSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr =
            m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
        if (FAILED(hr))
            return false;

        D3D12_RANGE readRange{0, 0};
        resource->Map(0, &readRange, mapped);
        return true;
    };

    for (uint32_t i = 0; i < FrameCount; ++i)
    {
        auto& frame = m_frames[i];

        // Per-frame command allocator
        HRESULT hr =
            m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.commandAllocator));
        if (FAILED(hr))
        {
            spdlog::error("Failed to create command allocator for frame {}: 0x{:08X}", i, static_cast<unsigned>(hr));
            return false;
        }

        // Per-frame constant buffers
        if (!createCB(frame.sceneCB, reinterpret_cast<void**>(&frame.sceneCBMapped), sizeof(SceneCBData)))
        {
            spdlog::error("Failed to create scene CB for frame {}", i);
            return false;
        }
        if (!createCB(frame.materialCB, reinterpret_cast<void**>(&frame.materialCBMapped), sizeof(MaterialCBData)))
        {
            spdlog::error("Failed to create material CB for frame {}", i);
            return false;
        }

        frame.fenceValue = 0;
    }

    // Create the shared command list using frame 0's allocator
    ComPtr<ID3D12Device4> device4;
    HRESULT hr;
    if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&device4))))
    {
        hr = device4->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE,
                                         IID_PPV_ARGS(&m_commandList));
    }
    else
    {
        hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frames[0].commandAllocator.Get(), nullptr,
                                         IID_PPV_ARGS(&m_commandList));
        if (SUCCEEDED(hr))
            m_commandList->Close();
    }
    if (FAILED(hr))
    {
        spdlog::error("Failed to create command list: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    return true;
}

// ═══════════════════════════════════════════════════════════
// Default Resources
// ═══════════════════════════════════════════════════════════

void D3D12Renderer::createDefaultTextures()
{
    // 1x1 white diffuse
    uint8_t whiteDiffuse[] = {255, 255, 255, 255};
    uploadTexture(0, whiteDiffuse, 1, 1, true);

    // 1x1 flat normal (128, 128, 255) = (0, 0, 1) in tangent space
    uint8_t flatNormal[] = {128, 128, 255, 255};
    uploadTexture(1, flatNormal, 1, 1, false);

    // 1x1 default RMAOS: roughness=128, metallic=0, ao=255, specular=255
    uint8_t defaultRMAOS[] = {128, 0, 255, 255};
    uploadTexture(2, defaultRMAOS, 1, 1, false);
}

void D3D12Renderer::createDefaultIBL()
{
    // Default ZH3: dark ambient
    std::memset(m_zh3Data, 0, sizeof(m_zh3Data));
    m_zh3Data[0][0] = m_zh3Data[0][1] = m_zh3Data[0][2] = 0.02f;

    // Prefiltered: 1x1 dark grey dummy cubemap
    {
        float pref[] = {0.02f, 0.02f, 0.02f, 1.0f};
        std::vector<float> face(pref, pref + 4);
        std::vector<float> faces[6] = {face, face, face, face, face, face};
        uploadCubemap(3, m_prefilteredCubemap, faces, 1, 1);
    }
    // BRDF LUT: default (0.5, 0.0)
    {
        float lut[] = {0.5f, 0.0f};
        uploadBRDFLut(4, lut, 1);
    }
    m_maxPrefilteredMip = 0;
}

// ═══════════════════════════════════════════════════════════
// Resource Upload (using copy queue)
// ═══════════════════════════════════════════════════════════

void D3D12Renderer::uploadTexture(int srvIndex, const uint8_t* rgba, int w, int h, bool srgb)
{
    const DXGI_FORMAT texFormat = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

    // Create texture resource on DEFAULT heap
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = w;
    texDesc.Height = h;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = texFormat;
    texDesc.SampleDesc.Count = 1;

    HRESULT hr =
        m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                          nullptr, IID_PPV_ARGS(&m_textures[srvIndex]));
    if (FAILED(hr))
    {
        spdlog::error("uploadTexture: CreateCommittedResource failed: 0x{:08X}", static_cast<unsigned>(hr));
        return;
    }

    // Get upload footprint
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 requiredSize = 0;
    m_device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &requiredSize);

    // Allocate from ring buffer
    uint64_t ringOffset = 0;
    uint8_t* mapped = m_uploadQueue->allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, ringOffset);
    if (!mapped)
    {
        // Ring buffer full — flush and retry
        m_uploadQueue->flush();
        m_uploadQueue->resetRingBuffer();
        mapped = m_uploadQueue->allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, ringOffset);
        if (!mapped)
        {
            spdlog::error("uploadTexture: ring buffer allocation failed after flush");
            return;
        }
    }

    // Copy data into ring buffer, respecting row pitch
    footprint.Offset = ringOffset;
    for (int y = 0; y < h; ++y)
    {
        memcpy(mapped + y * footprint.Footprint.RowPitch, rgba + y * w * 4, w * 4);
    }

    // Record copy command
    m_uploadQueue->resetCommandList();
    auto* copyList = m_uploadQueue->commandList();

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = m_textures[srvIndex].Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = m_uploadQueue->ringBuffer();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    copyList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    uint64_t copyFence = m_uploadQueue->execute();

    // We need to transition the resource from COPY_DEST to PIXEL_SHADER_RESOURCE
    // on the direct queue after the copy completes. Use a one-shot direct queue submission.
    m_uploadQueue->directQueueWaitForCopy(m_directQueue.Get(), copyFence);

    // Use frame 0's allocator for this transition (we're during init or blocking upload)
    flushGPU(); // Ensure no pending work
    m_frames[0].commandAllocator->Reset();
    m_commandList->Reset(m_frames[0].commandAllocator.Get(), nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_textures[srvIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_directQueue->ExecuteCommandLists(1, lists);
    flushGPU();

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = texFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_device->CreateShaderResourceView(m_textures[srvIndex].Get(), &srvDesc,
                                       m_srvHeap.cpuHandle(m_srvBaseIndex + srvIndex));
}

void D3D12Renderer::uploadCubemap(int srvIndex, ComPtr<ID3D12Resource>& resource, const std::vector<float>* faces,
                                  int faceSize, int mipLevels)
{
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = faceSize;
    texDesc.Height = faceSize;
    texDesc.DepthOrArraySize = 6;
    texDesc.MipLevels = static_cast<UINT16>(mipLevels);
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.SampleDesc.Count = 1;

    m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                      nullptr, IID_PPV_ARGS(&resource));

    // Upload each face using copy queue
    m_uploadQueue->flush();
    m_uploadQueue->resetRingBuffer();
    m_uploadQueue->resetCommandList();
    auto* copyList = m_uploadQueue->commandList();

    for (int f = 0; f < 6; ++f)
    {
        UINT subresource = static_cast<UINT>(f) * static_cast<UINT>(mipLevels); // mip 0, array slice f

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        UINT64 requiredSize = 0;
        D3D12_RESOURCE_DESC faceDesc = texDesc;
        faceDesc.DepthOrArraySize = 1;
        faceDesc.MipLevels = 1;
        m_device->GetCopyableFootprints(&faceDesc, 0, 1, 0, &footprint, nullptr, nullptr, &requiredSize);

        uint64_t ringOffset = 0;
        uint8_t* mapped = m_uploadQueue->allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, ringOffset);
        if (!mapped)
        {
            // Need more space — execute current batch and reset
            uint64_t fence = m_uploadQueue->execute();
            m_uploadQueue->flush();
            m_uploadQueue->resetRingBuffer();
            m_uploadQueue->resetCommandList();
            copyList = m_uploadQueue->commandList();
            mapped = m_uploadQueue->allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, ringOffset);
        }

        footprint.Offset = ringOffset;
        for (int y = 0; y < faceSize; ++y)
        {
            memcpy(mapped + y * footprint.Footprint.RowPitch, faces[f].data() + y * faceSize * 4,
                   faceSize * 4 * sizeof(float));
        }

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = resource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = subresource;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = m_uploadQueue->ringBuffer();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;

        copyList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    uint64_t copyFence = m_uploadQueue->execute();
    m_uploadQueue->directQueueWaitForCopy(m_directQueue.Get(), copyFence);

    // Transition to SRV on direct queue
    flushGPU();
    m_frames[0].commandAllocator->Reset();
    m_commandList->Reset(m_frames[0].commandAllocator.Get(), nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_directQueue->ExecuteCommandLists(1, lists);
    flushGPU();

    // Create cubemap SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MipLevels = mipLevels;

    m_device->CreateShaderResourceView(resource.Get(), &srvDesc, m_srvHeap.cpuHandle(m_srvBaseIndex + srvIndex));
}

void D3D12Renderer::uploadCubemapMipped(int srvIndex, ComPtr<ID3D12Resource>& resource,
                                        const std::vector<IBLData::MipFace>* faceMips, int faceSize, int mipLevels)
{
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = faceSize;
    texDesc.Height = faceSize;
    texDesc.DepthOrArraySize = 6;
    texDesc.MipLevels = static_cast<UINT16>(mipLevels);
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.SampleDesc.Count = 1;

    m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                      nullptr, IID_PPV_ARGS(&resource));

    m_uploadQueue->flush();
    m_uploadQueue->resetRingBuffer();
    m_uploadQueue->resetCommandList();
    auto* copyList = m_uploadQueue->commandList();

    for (int f = 0; f < 6; ++f)
    {
        for (int m = 0; m < mipLevels; ++m)
        {
            const auto& mipFace = faceMips[f][m];
            int mipSize = mipFace.size;
            UINT subresource = static_cast<UINT>(f) * static_cast<UINT>(mipLevels) + static_cast<UINT>(m);

            D3D12_RESOURCE_DESC mipDesc{};
            mipDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            mipDesc.Width = mipSize;
            mipDesc.Height = mipSize;
            mipDesc.DepthOrArraySize = 1;
            mipDesc.MipLevels = 1;
            mipDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            mipDesc.SampleDesc.Count = 1;

            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
            UINT64 requiredSize = 0;
            m_device->GetCopyableFootprints(&mipDesc, 0, 1, 0, &footprint, nullptr, nullptr, &requiredSize);

            uint64_t ringOffset = 0;
            uint8_t* mapped = m_uploadQueue->allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, ringOffset);
            if (!mapped)
            {
                uint64_t fence = m_uploadQueue->execute();
                m_uploadQueue->flush();
                m_uploadQueue->resetRingBuffer();
                m_uploadQueue->resetCommandList();
                copyList = m_uploadQueue->commandList();
                mapped = m_uploadQueue->allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, ringOffset);
            }

            footprint.Offset = ringOffset;
            for (int y = 0; y < mipSize; ++y)
            {
                memcpy(mapped + y * footprint.Footprint.RowPitch, mipFace.pixels.data() + y * mipSize * 4,
                       mipSize * 4 * sizeof(float));
            }

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = resource.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = subresource;

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = m_uploadQueue->ringBuffer();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = footprint;

            copyList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
    }

    uint64_t copyFence = m_uploadQueue->execute();
    m_uploadQueue->directQueueWaitForCopy(m_directQueue.Get(), copyFence);

    flushGPU();
    m_frames[0].commandAllocator->Reset();
    m_commandList->Reset(m_frames[0].commandAllocator.Get(), nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_directQueue->ExecuteCommandLists(1, lists);
    flushGPU();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MipLevels = mipLevels;

    m_device->CreateShaderResourceView(resource.Get(), &srvDesc, m_srvHeap.cpuHandle(m_srvBaseIndex + srvIndex));
}

void D3D12Renderer::uploadBRDFLut(int srvIndex, const float* rgPixels, int size)
{
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = size;
    texDesc.Height = size;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    texDesc.SampleDesc.Count = 1;

    m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                      nullptr, IID_PPV_ARGS(&m_brdfLut));

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 requiredSize = 0;
    m_device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &requiredSize);

    m_uploadQueue->flush();
    m_uploadQueue->resetRingBuffer();

    uint64_t ringOffset = 0;
    uint8_t* mapped = m_uploadQueue->allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, ringOffset);
    if (!mapped)
    {
        spdlog::error("uploadBRDFLut: ring buffer allocation failed");
        return;
    }

    footprint.Offset = ringOffset;
    for (int y = 0; y < size; ++y)
    {
        memcpy(mapped + y * footprint.Footprint.RowPitch, rgPixels + y * size * 2, size * 2 * sizeof(float));
    }

    m_uploadQueue->resetCommandList();
    auto* copyList = m_uploadQueue->commandList();

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = m_brdfLut.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = m_uploadQueue->ringBuffer();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    copyList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    uint64_t copyFence = m_uploadQueue->execute();
    m_uploadQueue->directQueueWaitForCopy(m_directQueue.Get(), copyFence);

    // Transition on direct queue
    flushGPU();
    m_frames[0].commandAllocator->Reset();
    m_commandList->Reset(m_frames[0].commandAllocator.Get(), nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_brdfLut.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_directQueue->ExecuteCommandLists(1, lists);
    flushGPU();

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_device->CreateShaderResourceView(m_brdfLut.Get(), &srvDesc, m_srvHeap.cpuHandle(m_srvBaseIndex + srvIndex));
}

void D3D12Renderer::uploadMesh(const PreviewMesh& mesh)
{
    auto createUploadBuffer = [&](ComPtr<ID3D12Resource>& resource, const void* data, size_t size)
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = size;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&resource));
        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        resource->Map(0, &readRange, &mapped);
        memcpy(mapped, data, size);
        resource->Unmap(0, nullptr);
    };

    size_t vbSize = mesh.vertices.size() * sizeof(PreviewVertex);
    size_t ibSize = mesh.indices.size() * sizeof(uint32_t);

    createUploadBuffer(m_vertexBuffer, mesh.vertices.data(), vbSize);
    createUploadBuffer(m_indexBuffer, mesh.indices.data(), ibSize);

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = static_cast<UINT>(vbSize);
    m_vertexBufferView.StrideInBytes = sizeof(PreviewVertex);

    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.SizeInBytes = static_cast<UINT>(ibSize);
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;

    m_indexCount = static_cast<uint32_t>(mesh.indices.size());
}

// ═══════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════

void D3D12Renderer::setMesh(PreviewShape shape)
{
    auto mesh = MeshGenerator::generate(shape);
    uploadMesh(mesh);
}

void D3D12Renderer::setTextures(const uint8_t* diffuseRGBA, int diffuseW, int diffuseH, const uint8_t* normalRGBA,
                                int normalW, int normalH, const uint8_t* rmaosRGBA, int rmaosW, int rmaosH)
{
    if (diffuseRGBA && diffuseW > 0 && diffuseH > 0)
        uploadTexture(0, diffuseRGBA, diffuseW, diffuseH, true);
    else
    {
        uint8_t whiteDiffuse[] = {255, 255, 255, 255};
        uploadTexture(0, whiteDiffuse, 1, 1, true);
    }

    if (normalRGBA && normalW > 0 && normalH > 0)
        uploadTexture(1, normalRGBA, normalW, normalH, false);
    else
    {
        uint8_t flatNormal[] = {128, 128, 255, 255};
        uploadTexture(1, flatNormal, 1, 1, false);
    }

    if (rmaosRGBA && rmaosW > 0 && rmaosH > 0)
        uploadTexture(2, rmaosRGBA, rmaosW, rmaosH, false);
    else
    {
        uint8_t defaultRMAOS[] = {128, 0, 255, 255};
        uploadTexture(2, defaultRMAOS, 1, 1, false);
    }
}

void D3D12Renderer::setMaterialParams(float specularLevel, float roughnessScale)
{
    m_specularLevel = specularLevel;
    m_roughnessScale = roughnessScale;
}

void D3D12Renderer::setRenderFlags(uint32_t flags)
{
    m_renderFlags = flags;
}

void D3D12Renderer::setCamera(float azimuth, float elevation, float distance)
{
    m_azimuth = azimuth;
    m_elevation = elevation;
    m_distance = distance;
}

void D3D12Renderer::setLightDirection(float x, float y, float z)
{
    float len = std::sqrt(x * x + y * y + z * z);
    if (len > 0.0001f)
        m_lightDir = {x / len, y / len, z / len};
}

void D3D12Renderer::setLightColor(float r, float g, float b)
{
    m_lightColor = {r, g, b};
}

void D3D12Renderer::setLightIntensity(float intensity)
{
    m_lightIntensity = intensity;
}

void D3D12Renderer::setIBLIntensity(float intensity)
{
    m_iblIntensity = intensity;
}

// ═══════════════════════════════════════════════════════════
// Resize
// ═══════════════════════════════════════════════════════════

void D3D12Renderer::resize(uint32_t width, uint32_t height)
{
    if (!m_initialized || width == 0 || height == 0)
        return;

    flushGPU();

    for (auto& rt : m_renderTargets)
        rt.Reset();
    m_depthStencilBuffer.Reset();

    m_swapChain->ResizeBuffers(FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    m_width = width;
    m_height = height;

    // Recreate RTVs
    for (UINT i = 0; i < FrameCount; ++i)
    {
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_rtvHeap.cpuHandle(i));
    }

    createDepthStencil(width, height);
}

// ═══════════════════════════════════════════════════════════
// Render — proper double-buffered frame management
// ═══════════════════════════════════════════════════════════

void D3D12Renderer::render()
{
    if (!m_initialized || m_width == 0 || m_height == 0)
        return;

    // Determine current frame context (double-buffered)
    uint32_t frameIdx = static_cast<uint32_t>(m_frameNumber % FrameCount);
    auto& frame = m_frames[frameIdx];

    // 1. Wait for this frame context's previous work to complete
    waitForFrame(frameIdx);

    // 2. Reset this frame's command allocator (safe now that GPU is done with it)
    frame.commandAllocator->Reset();
    m_commandList->Reset(frame.commandAllocator.Get(), m_pipelineState.Get());

    // 3. Update this frame's constant buffers
    float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    float camX = m_distance * std::cos(m_elevation) * std::sin(m_azimuth);
    float camY = m_distance * std::sin(m_elevation);
    float camZ = m_distance * std::cos(m_elevation) * std::cos(m_azimuth);

    XMVECTOR eye = XMVectorSet(camX, camY, camZ, 1.0f);
    XMVECTOR at = XMVectorSet(0, 0, 0, 1.0f);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.01f, 100.0f);
    XMMATRIX wvp = world * view * proj;

    XMStoreFloat4x4(&frame.sceneCBMapped->worldViewProj, wvp);
    XMStoreFloat4x4(&frame.sceneCBMapped->world, world);
    XMMATRIX worldInvTranspose = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
    XMStoreFloat4x4(&frame.sceneCBMapped->worldInvTranspose, worldInvTranspose);
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);
    XMStoreFloat4x4(&frame.sceneCBMapped->invViewProj, invViewProj);
    frame.sceneCBMapped->cameraPos = {camX, camY, camZ};
    frame.sceneCBMapped->lightDir = m_lightDir;
    frame.sceneCBMapped->lightColor = m_lightColor;
    frame.sceneCBMapped->lightIntensity = m_lightIntensity;
    frame.sceneCBMapped->iblIntensity = m_iblLoaded ? m_iblIntensity : 0.0f;
    frame.sceneCBMapped->maxPrefilteredMip = static_cast<float>(m_maxPrefilteredMip);
    for (int i = 0; i < 5; i++)
    {
        frame.sceneCBMapped->zh3Data[i] = {m_zh3Data[i][0], m_zh3Data[i][1], m_zh3Data[i][2], m_zh3Data[i][3]};
    }

    frame.materialCBMapped->specularLevel = m_specularLevel;
    frame.materialCBMapped->roughnessScale = m_roughnessScale;
    frame.materialCBMapped->renderFlags = m_renderFlags;

    // 4. Record commands
    UINT backBufferIdx = m_swapChain->GetCurrentBackBufferIndex();

    // Transition RT to render target
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_renderTargets[backBufferIdx].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &barrier);

    auto rtvHandle = m_rtvHeap.cpuHandle(backBufferIdx);
    auto dsvHandle = m_dsvHeap.cpuHandle(0);

    float clearColor[] = {0.15f, 0.15f, 0.15f, 1.0f};
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    D3D12_VIEWPORT viewport{0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0, 1};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);

    // Set root signature and descriptor heap
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = {m_srvHeap.heap()};
    m_commandList->SetDescriptorHeaps(1, heaps);

    // Set this frame's CBVs
    m_commandList->SetGraphicsRootConstantBufferView(0, frame.sceneCB->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(1, frame.materialCB->GetGPUVirtualAddress());

    // Set SRV table
    m_commandList->SetGraphicsRootDescriptorTable(2, m_srvHeap.gpuHandle(m_srvBaseIndex));

    // Draw skybox background (before mesh, no depth write)
    if (m_iblLoaded)
    {
        m_commandList->SetPipelineState(m_skyboxPSO.Get());
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->DrawInstanced(3, 1, 0, 0);
    }

    // Draw PBR mesh
    m_commandList->SetPipelineState(m_pipelineState.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);
    m_commandList->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);

    // Transition RT to present
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();

    // 5. Execute
    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_directQueue->ExecuteCommandLists(1, lists);

    // 6. Signal fence for this frame context
    ++m_directFenceValue;
    frame.fenceValue = m_directFenceValue;
    m_directQueue->Signal(m_directFence.Get(), m_directFenceValue);

    // 7. Present
    m_swapChain->Present(1, 0);

    // Advance frame counter
    ++m_frameNumber;
}

// ═══════════════════════════════════════════════════════════
// Synchronization
// ═══════════════════════════════════════════════════════════

void D3D12Renderer::waitForFrame(uint32_t frameIndex)
{
    auto& frame = m_frames[frameIndex];
    if (frame.fenceValue == 0)
        return; // Never been submitted

    if (m_directFence->GetCompletedValue() < frame.fenceValue)
    {
        m_directFence->SetEventOnCompletion(frame.fenceValue, m_directFenceEvent);
        WaitForSingleObject(m_directFenceEvent, 5000);

        if (m_directFence->GetCompletedValue() < frame.fenceValue)
        {
            spdlog::warn("D3D12Renderer::waitForFrame: fence {} not reached (completed={})", frame.fenceValue,
                         m_directFence->GetCompletedValue());
        }
    }
}

void D3D12Renderer::flushGPU()
{
    if (!m_directFence)
        return;

    ++m_directFenceValue;
    m_directQueue->Signal(m_directFence.Get(), m_directFenceValue);

    if (m_directFence->GetCompletedValue() < m_directFenceValue)
    {
        m_directFence->SetEventOnCompletion(m_directFenceValue, m_directFenceEvent);
        WaitForSingleObject(m_directFenceEvent, 5000);
    }

    // Also flush upload queue
    if (m_uploadQueue)
        m_uploadQueue->flush();
}

// ═══════════════════════════════════════════════════════════
// IBL Loading (CPU fallback — IBLPipeline GPU compute is optional)
// ═══════════════════════════════════════════════════════════

bool D3D12Renderer::loadIBL(const std::filesystem::path& hdriPath)
{
    if (!m_initialized)
        return false;

    // Load HDRI file to CPU float RGBA pixels
    int eqW = 0, eqH = 0;
    std::vector<float> equirect;
    if (!IBLProcessor::loadHDRI(hdriPath, eqW, eqH, equirect))
    {
        spdlog::error("D3D12Renderer: failed to load HDRI {}", hdriPath.string());
        return false;
    }

    // Cache the loaded HDRI for reprocessing with different parameters
    m_hdriPath = hdriPath;
    m_hdriPixels = std::move(equirect);
    m_hdriW = eqW;
    m_hdriH = eqH;

    // Try GPU IBL pipeline
    if (m_iblPipeline && m_iblPipeline->isInitialized())
    {
        flushGPU();
        IBLResult ibl = m_iblPipeline->process(m_device.Get(), m_directQueue.Get(), m_hdriPixels.data(), m_hdriW,
                                               m_hdriH, m_iblPrefilteredSize, 256, m_iblPrefilterSamples);
        if (ibl.valid)
        {
            // Transfer ownership and create SRVs
            std::memcpy(m_zh3Data, ibl.zh3Data, sizeof(m_zh3Data));
            m_prefilteredCubemap = std::move(ibl.prefilteredCubemap);
            m_brdfLut = std::move(ibl.brdfLut);
            m_maxPrefilteredMip = ibl.prefilteredMipLevels - 1;

            // Create cubemap SRV for prefiltered (slot 3)
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.TextureCube.MipLevels = ibl.prefilteredMipLevels;
            m_device->CreateShaderResourceView(m_prefilteredCubemap.Get(), &srvDesc,
                                               m_srvHeap.cpuHandle(m_srvBaseIndex + 3));

            // Create 2D SRV for BRDF LUT (slot 4)
            D3D12_SHADER_RESOURCE_VIEW_DESC lutSrvDesc{};
            lutSrvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
            lutSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            lutSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            lutSrvDesc.Texture2D.MipLevels = 1;
            m_device->CreateShaderResourceView(m_brdfLut.Get(), &lutSrvDesc, m_srvHeap.cpuHandle(m_srvBaseIndex + 4));

            m_iblLoaded = true;
            m_iblIntensity = 1.0f;
            spdlog::info("D3D12Renderer: IBL loaded via GPU from {} (prefSize={}, samples={})",
                         hdriPath.filename().string(), m_iblPrefilteredSize, m_iblPrefilterSamples);
            return true;
        }
        spdlog::warn("D3D12Renderer: GPU IBL failed, falling back to CPU");
    }

    // CPU fallback
    IBLData data = IBLProcessor::process(hdriPath, 64, m_iblPrefilteredSize, 256);
    if (!data.valid)
    {
        spdlog::error("D3D12Renderer: IBL processing failed for {}", hdriPath.string());
        return false;
    }

    // Compute ZH3 from equirect pixels (CPU fallback)
    {
        IBLPipeline::computeZH3CPU(m_hdriPixels.data(), m_hdriW, m_hdriH, m_zh3Data);
    }

    if (data.prefilteredMipLevels > 0)
    {
        uploadCubemapMipped(3, m_prefilteredCubemap, data.prefilteredFaces, data.prefilteredSize,
                            data.prefilteredMipLevels);
        m_maxPrefilteredMip = data.prefilteredMipLevels - 1;
    }

    uploadBRDFLut(4, data.brdfLutPixels.data(), data.brdfLutSize);

    m_iblLoaded = true;
    m_iblIntensity = 1.0f;
    spdlog::info("D3D12Renderer: IBL loaded via CPU from {}", hdriPath.filename().string());
    return true;
}

void D3D12Renderer::setIBLParams(int prefilteredSize, int prefilterSamples)
{
    if (prefilteredSize == m_iblPrefilteredSize && prefilterSamples == m_iblPrefilterSamples)
        return;

    m_iblPrefilteredSize = prefilteredSize;
    m_iblPrefilterSamples = prefilterSamples;

    // Reprocess if we have cached HDRI data
    if (!m_hdriPixels.empty() && m_iblLoaded)
    {
        spdlog::info("D3D12Renderer: reprocessing IBL (prefSize={}, samples={})", prefilteredSize, prefilterSamples);

        if (m_iblPipeline && m_iblPipeline->isInitialized())
        {
            flushGPU();
            IBLResult ibl = m_iblPipeline->process(m_device.Get(), m_directQueue.Get(), m_hdriPixels.data(), m_hdriW,
                                                   m_hdriH, prefilteredSize, 256, prefilterSamples);
            if (ibl.valid)
            {
                std::memcpy(m_zh3Data, ibl.zh3Data, sizeof(m_zh3Data));
                m_prefilteredCubemap = std::move(ibl.prefilteredCubemap);
                m_brdfLut = std::move(ibl.brdfLut);
                m_maxPrefilteredMip = ibl.prefilteredMipLevels - 1;

                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.TextureCube.MipLevels = ibl.prefilteredMipLevels;
                m_device->CreateShaderResourceView(m_prefilteredCubemap.Get(), &srvDesc,
                                                   m_srvHeap.cpuHandle(m_srvBaseIndex + 3));

                D3D12_SHADER_RESOURCE_VIEW_DESC lutSrvDesc{};
                lutSrvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
                lutSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                lutSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                lutSrvDesc.Texture2D.MipLevels = 1;
                m_device->CreateShaderResourceView(m_brdfLut.Get(), &lutSrvDesc,
                                                   m_srvHeap.cpuHandle(m_srvBaseIndex + 4));

                spdlog::info("D3D12Renderer: IBL reprocessed (GPU, prefSize={}, samples={})", prefilteredSize,
                             prefilterSamples);
                return;
            }
        }

        // CPU fallback reprocess
        IBLData data = IBLProcessor::process(m_hdriPath, 64, prefilteredSize, 256);
        if (data.valid)
        {
            IBLPipeline::computeZH3CPU(m_hdriPixels.data(), m_hdriW, m_hdriH, m_zh3Data);
            if (data.prefilteredMipLevels > 0)
            {
                uploadCubemapMipped(3, m_prefilteredCubemap, data.prefilteredFaces, data.prefilteredSize,
                                    data.prefilteredMipLevels);
                m_maxPrefilteredMip = data.prefilteredMipLevels - 1;
            }
            uploadBRDFLut(4, data.brdfLutPixels.data(), data.brdfLutSize);
            spdlog::info("D3D12Renderer: IBL reprocessed (CPU, prefSize={})", prefilteredSize);
        }
    }
}

} // namespace tpbr
