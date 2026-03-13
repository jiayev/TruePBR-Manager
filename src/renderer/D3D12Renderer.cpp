#include "D3D12Renderer.h"
#include "IBLProcessor.h"
#include "utils/Log.h"

#include <DirectXMath.h>
#include <d3dcompiler.h>

#include <chrono>

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
        waitForGPU();
    }
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
    }
}

bool D3D12Renderer::init(HWND hwnd, uint32_t width, uint32_t height)
{
    m_width = width;
    m_height = height;

    spdlog::info("D3D12Renderer::init starting ({}x{}, hwnd=0x{:X})", width, height, reinterpret_cast<uintptr_t>(hwnd));

    if (!createDevice())
    {
        spdlog::error("D3D12Renderer::init FAILED at createDevice");
        return false;
    }
    spdlog::debug("D3D12Renderer: createDevice OK");

    if (!createCommandQueue())
    {
        spdlog::error("D3D12Renderer::init FAILED at createCommandQueue");
        return false;
    }
    spdlog::debug("D3D12Renderer: createCommandQueue OK");

    if (!createSwapChain(hwnd, width, height))
    {
        spdlog::error("D3D12Renderer::init FAILED at createSwapChain");
        return false;
    }
    spdlog::debug("D3D12Renderer: createSwapChain OK");

    if (!createRTVHeap())
    {
        spdlog::error("D3D12Renderer::init FAILED at createRTVHeap");
        return false;
    }
    spdlog::debug("D3D12Renderer: createRTVHeap OK");

    if (!createDSV(width, height))
    {
        spdlog::error("D3D12Renderer::init FAILED at createDSV");
        return false;
    }
    spdlog::debug("D3D12Renderer: createDSV OK");

    if (!createSRVHeap())
    {
        spdlog::error("D3D12Renderer::init FAILED at createSRVHeap");
        return false;
    }
    spdlog::debug("D3D12Renderer: createSRVHeap OK");

    if (!createRootSignatureAndPSO())
    {
        spdlog::error("D3D12Renderer::init FAILED at createRootSignatureAndPSO");
        return false;
    }
    spdlog::debug("D3D12Renderer: createRootSignatureAndPSO OK");

    if (!createConstantBuffers())
    {
        spdlog::error("D3D12Renderer::init FAILED at createConstantBuffers");
        return false;
    }
    spdlog::debug("D3D12Renderer: createConstantBuffers OK");

    spdlog::debug("D3D12Renderer: creating default textures...");
    createDefaultTextures();
    spdlog::debug("D3D12Renderer: default textures OK");

    spdlog::debug("D3D12Renderer: creating default IBL...");
    createDefaultIBL();
    spdlog::debug("D3D12Renderer: default IBL OK");

    spdlog::debug("D3D12Renderer: uploading default mesh...");
    setMesh(PreviewShape::Sphere);
    spdlog::debug("D3D12Renderer: default mesh OK");

    m_initialized = true;
    spdlog::info("D3D12Renderer initialized ({}x{})", width, height);
    return true;
}

bool D3D12Renderer::createDevice()
{
    UINT dxgiFlags = 0;

#ifndef NDEBUG
    // Enable D3D12 debug layer in Debug builds
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
            // Convert WCHAR adapter description to narrow string for logging
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

    // Create fence
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_fenceValue = 1;

    return true;
}

bool D3D12Renderer::createCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_commandQueue))))
    {
        spdlog::error("Failed to create command queue");
        return false;
    }

    HRESULT hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator));
    if (FAILED(hr))
    {
        spdlog::error("Failed to create command allocator: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    // Use CreateCommandList1 (creates in closed state) to avoid the
    // Create-in-recording → immediate Close pattern that causes timing
    // issues on some drivers (notably RTX 5070).
    ComPtr<ID3D12Device4> device4;
    if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&device4))))
    {
        hr = device4->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE,
                                         IID_PPV_ARGS(&m_commandList));
    }
    else
    {
        // Fallback for older D3D12 runtime: create in recording state then close
        hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr,
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
    if (FAILED(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &desc, nullptr, nullptr, &swapChain)))
    {
        spdlog::error("Failed to create swap chain");
        return false;
    }

    swapChain.As(&m_swapChain);
    return true;
}

bool D3D12Renderer::createRTVHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.NumDescriptors = FrameCount;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_rtvHeap));
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    auto handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < FrameCount; ++i)
    {
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, handle);
        handle.ptr += m_rtvDescriptorSize;
    }
    return true;
}

bool D3D12Renderer::createDSV(uint32_t width, uint32_t height)
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_dsvHeap));

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

    m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                      &clearValue, IID_PPV_ARGS(&m_depthStencilBuffer));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc,
                                     m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

bool D3D12Renderer::createSRVHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.NumDescriptors = SRVCount; // diffuse, normal, rmaos, irradiance, prefiltered, brdfLut
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap));
    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

bool D3D12Renderer::createRootSignatureAndPSO()
{
    // Root signature: 2 CBVs (b0, b1) + 1 descriptor table (3 SRVs) + 1 static sampler
    D3D12_ROOT_PARAMETER rootParams[3] = {};

    // b0: SceneCB
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // b1: MaterialCB
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 1;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // SRV table: t0, t1, t2
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
    samplers[0].ShaderRegister = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: linear clamp (BRDF LUT)
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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

    // Compile shaders — look next to exe first, then source dir for dev builds
    std::filesystem::path shaderPath;
    {
        // Try shaders/ next to executable (dist layout)
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        auto exeDir = std::filesystem::path(exePath).parent_path();
        auto distPath = exeDir / "shaders" / "PBRShader.hlsl";
        if (std::filesystem::exists(distPath))
        {
            shaderPath = distPath;
        }
        else
        {
            // Fallback: source directory (for development)
            shaderPath = std::filesystem::path(__FILE__).parent_path() / "PBRShader.hlsl";
        }
    }
    std::wstring shaderPathW = shaderPath.wstring();

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

    return true;
}

bool D3D12Renderer::createConstantBuffers()
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

        if (FAILED(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&resource))))
            return false;

        D3D12_RANGE readRange{0, 0};
        resource->Map(0, &readRange, mapped);
        return true;
    };

    if (!createCB(m_sceneCB, reinterpret_cast<void**>(&m_sceneCBMapped), sizeof(SceneCBData)))
        return false;
    if (!createCB(m_materialCB, reinterpret_cast<void**>(&m_materialCBMapped), sizeof(MaterialCBData)))
        return false;

    return true;
}

void D3D12Renderer::createDefaultTextures()
{
    // 1x1 white diffuse
    spdlog::debug("D3D12Renderer: uploading default diffuse...");
    spdlog::default_logger()->flush();
    uint8_t whiteDiffuse[] = {255, 255, 255, 255};
    uploadTexture(0, whiteDiffuse, 1, 1, true);

    // 1x1 flat normal (128, 128, 255) = (0, 0, 1) in tangent space
    spdlog::debug("D3D12Renderer: uploading default normal...");
    spdlog::default_logger()->flush();
    uint8_t flatNormal[] = {128, 128, 255, 255};
    uploadTexture(1, flatNormal, 1, 1, false);

    // 1x1 default RMAOS: roughness=128, metallic=0, ao=255, specular=255
    spdlog::debug("D3D12Renderer: uploading default RMAOS...");
    spdlog::default_logger()->flush();
    uint8_t defaultRMAOS[] = {128, 0, 255, 255};
    uploadTexture(2, defaultRMAOS, 1, 1, false);

    spdlog::debug("D3D12Renderer: all default textures uploaded");
    spdlog::default_logger()->flush();
}

void D3D12Renderer::uploadTexture(int srvIndex, const uint8_t* rgba, int w, int h, bool srgb)
{
    spdlog::debug("D3D12Renderer::uploadTexture(slot={}, {}x{}, srgb={})", srvIndex, w, h, srgb);
    spdlog::default_logger()->flush();
    // sRGB textures (diffuse/albedo) use _SRGB format so the GPU automatically
    // converts from sRGB to linear on sampling. Linear data (normal, RMAOS) uses _UNORM.
    const DXGI_FORMAT texFormat = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

    // Create texture resource
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

    HRESULT hrTex =
        m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                          nullptr, IID_PPV_ARGS(&m_textures[srvIndex]));
    if (FAILED(hrTex))
    {
        spdlog::error("uploadTexture: CreateCommittedResource (texture) failed: 0x{:08X}",
                      static_cast<unsigned>(hrTex));
        spdlog::default_logger()->flush();
        return;
    }

    // Upload heap
    const UINT64 uploadSize = static_cast<UINT64>(w) * h * 4;
    D3D12_HEAP_PROPERTIES uploadProps{};
    uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width =
        (uploadSize + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    uploadDesc.Width = std::max(uploadDesc.Width, static_cast<UINT64>(uploadSize));
    // Align to upload requirements
    UINT64 requiredSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    m_device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &requiredSize);

    uploadDesc.Width = requiredSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hrUpload = m_device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                         IID_PPV_ARGS(&m_textureUploadHeaps[srvIndex]));
    if (FAILED(hrUpload))
    {
        spdlog::error("uploadTexture: CreateCommittedResource (upload) failed: 0x{:08X}",
                      static_cast<unsigned>(hrUpload));
        spdlog::default_logger()->flush();
        return;
    }

    // Copy data to upload heap
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    m_textureUploadHeaps[srvIndex]->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    for (int y = 0; y < h; ++y)
    {
        memcpy(mapped + y * footprint.Footprint.RowPitch, rgba + y * w * 4, w * 4);
    }
    m_textureUploadHeaps[srvIndex]->Unmap(0, nullptr);

    // Record copy command
    spdlog::debug("D3D12Renderer::uploadTexture: resetting command list...");
    spdlog::default_logger()->flush();
    HRESULT hrAllocReset = m_commandAllocator->Reset();
    HRESULT hrListReset = m_commandList->Reset(m_commandAllocator.Get(), nullptr);
    if (FAILED(hrAllocReset) || FAILED(hrListReset))
    {
        spdlog::error("uploadTexture: Reset failed (alloc=0x{:08X}, list=0x{:08X})",
                      static_cast<unsigned>(hrAllocReset), static_cast<unsigned>(hrListReset));
        spdlog::default_logger()->flush();
        return;
    }

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = m_textures[srvIndex].Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = m_textureUploadHeaps[srvIndex].Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    // Transition to SRV
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_textures[srvIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_commandList->ResourceBarrier(1, &barrier);

    HRESULT hrClose = m_commandList->Close();
    if (FAILED(hrClose))
    {
        spdlog::error("uploadTexture: CommandList Close failed: 0x{:08X}", static_cast<unsigned>(hrClose));
        spdlog::default_logger()->flush();
        return;
    }

    spdlog::debug("D3D12Renderer::uploadTexture: executing and waiting...");
    spdlog::default_logger()->flush();
    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, lists);
    waitForGPU();
    spdlog::debug("D3D12Renderer::uploadTexture: complete");
    spdlog::default_logger()->flush();

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = texFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    auto handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += srvIndex * m_srvDescriptorSize;
    m_device->CreateShaderResourceView(m_textures[srvIndex].Get(), &srvDesc, handle);
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
    if (normalRGBA && normalW > 0 && normalH > 0)
        uploadTexture(1, normalRGBA, normalW, normalH, false);
    if (rmaosRGBA && rmaosW > 0 && rmaosH > 0)
        uploadTexture(2, rmaosRGBA, rmaosW, rmaosH, false);
}

void D3D12Renderer::setMaterialParams(float specularLevel, float roughnessScale)
{
    m_specularLevel = specularLevel;
    m_roughnessScale = roughnessScale;
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
    {
        m_lightDir = {x / len, y / len, z / len};
    }
}

void D3D12Renderer::setLightColor(float r, float g, float b)
{
    m_lightColor = {r, g, b};
}

void D3D12Renderer::setLightIntensity(float intensity)
{
    m_lightIntensity = intensity;
}

void D3D12Renderer::resize(uint32_t width, uint32_t height)
{
    if (!m_initialized || width == 0 || height == 0)
        return;

    waitForGPU();

    for (auto& rt : m_renderTargets)
        rt.Reset();
    m_depthStencilBuffer.Reset();

    m_swapChain->ResizeBuffers(FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    m_width = width;
    m_height = height;

    // Recreate RTVs
    auto handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < FrameCount; ++i)
    {
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, handle);
        handle.ptr += m_rtvDescriptorSize;
    }

    createDSV(width, height);
}

void D3D12Renderer::render()
{
    if (!m_initialized || m_width == 0 || m_height == 0)
        return;

    // Update constant buffers
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

    XMStoreFloat4x4(&m_sceneCBMapped->worldViewProj, wvp);
    XMStoreFloat4x4(&m_sceneCBMapped->world, world);
    XMMATRIX worldInvTranspose = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
    XMStoreFloat4x4(&m_sceneCBMapped->worldInvTranspose, worldInvTranspose);
    m_sceneCBMapped->cameraPos = {camX, camY, camZ};
    m_sceneCBMapped->lightDir = m_lightDir;
    m_sceneCBMapped->lightColor = m_lightColor;
    m_sceneCBMapped->lightIntensity = m_lightIntensity;
    m_sceneCBMapped->iblIntensity = m_iblLoaded ? m_iblIntensity : 0.0f;
    m_sceneCBMapped->maxPrefilteredMip = static_cast<float>(m_maxPrefilteredMip);

    m_materialCBMapped->specularLevel = m_specularLevel;
    m_materialCBMapped->roughnessScale = m_roughnessScale;

    // Record commands
    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());

    UINT frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Transition RT to render target
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_renderTargets[frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &barrier);

    auto rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += frameIndex * m_rtvDescriptorSize;
    auto dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    // Clear
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
    ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get()};
    m_commandList->SetDescriptorHeaps(1, heaps);

    // Set CBVs
    m_commandList->SetGraphicsRootConstantBufferView(0, m_sceneCB->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(1, m_materialCB->GetGPUVirtualAddress());

    // Set SRV table
    m_commandList->SetGraphicsRootDescriptorTable(2, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    // Draw
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);
    m_commandList->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);

    // Transition RT to present
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();

    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, lists);

    m_swapChain->Present(1, 0);
    waitForGPU();
}

void D3D12Renderer::waitForGPU()
{
    // Check for device removed first
    HRESULT deviceStatus = m_device->GetDeviceRemovedReason();
    if (FAILED(deviceStatus))
    {
        spdlog::error("D3D12Renderer::waitForGPU: DEVICE REMOVED! Reason: 0x{:08X}",
                      static_cast<unsigned>(deviceStatus));
        spdlog::default_logger()->flush();
        return;
    }

    const UINT64 targetValue = m_fenceValue;
    m_commandQueue->Signal(m_fence.Get(), targetValue);
    ++m_fenceValue;

    // Spin-wait with Sleep instead of event-based wait.
    // Some drivers (notably RTX 5070 / Blackwell) have issues with
    // SetEventOnCompletion not firing reliably for the first few fences.
    if (m_fence->GetCompletedValue() < targetValue)
    {
        const auto start = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::seconds(10);

        while (m_fence->GetCompletedValue() < targetValue)
        {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > timeout)
            {
                spdlog::error("D3D12Renderer::waitForGPU TIMEOUT (spin) waiting for fence value {}", targetValue);
                HRESULT reason = m_device->GetDeviceRemovedReason();
                spdlog::error("D3D12Renderer::waitForGPU: device removed reason: 0x{:08X}",
                              static_cast<unsigned>(reason));
                spdlog::default_logger()->flush();
                return;
            }
            Sleep(0); // Yield CPU
        }
    }
}

void D3D12Renderer::setIBLIntensity(float intensity)
{
    m_iblIntensity = intensity;
}

void D3D12Renderer::createDefaultIBL()
{
    // Create 1x1 dummy cubemaps so the shader doesn't crash when IBL is not loaded
    // Irradiance: dark grey
    {
        float irr[] = {0.02f, 0.02f, 0.02f, 1.0f};
        std::vector<float> face(irr, irr + 4);
        std::vector<float> faces[6] = {face, face, face, face, face, face};
        uploadCubemap(3, m_irradianceCubemap, faces, 1, 1, nullptr);
    }
    // Prefiltered: dark grey
    {
        float pref[] = {0.02f, 0.02f, 0.02f, 1.0f};
        std::vector<float> face(pref, pref + 4);
        std::vector<float> faces[6] = {face, face, face, face, face, face};
        uploadCubemap(4, m_prefilteredCubemap, faces, 1, 1, nullptr);
    }
    // BRDF LUT: default (0.5, 0.0)
    {
        float lut[] = {0.5f, 0.0f};
        uploadBRDFLut(5, lut, 1);
    }
    m_maxPrefilteredMip = 0;
}

void D3D12Renderer::uploadCubemap(int srvIndex, ComPtr<ID3D12Resource>& resource, const std::vector<float>* faces,
                                  int faceSize, int mipLevels, const std::vector<std::vector<float>>* /*mipFaces*/)
{
    // Create cubemap texture resource
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

    // Upload each face
    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), nullptr);

    // Keep upload buffers alive until after GPU executes the copy commands
    std::vector<ComPtr<ID3D12Resource>> uploadBuffers;

    for (int f = 0; f < 6; ++f)
    {
        UINT subresource = 0 + static_cast<UINT>(f) * static_cast<UINT>(mipLevels); // mip 0, array slice f

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        UINT64 requiredSize = 0;
        D3D12_RESOURCE_DESC faceDesc = texDesc;
        faceDesc.DepthOrArraySize = 1;
        faceDesc.MipLevels = 1;
        m_device->GetCopyableFootprints(&faceDesc, 0, 1, 0, &footprint, nullptr, nullptr, &requiredSize);

        // Create upload buffer
        D3D12_HEAP_PROPERTIES uploadProps{};
        uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC uploadDesc{};
        uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Width = requiredSize;
        uploadDesc.Height = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels = 1;
        uploadDesc.SampleDesc.Count = 1;
        uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> uploadBuf;
        m_device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));

        uint8_t* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        uploadBuf->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        for (int y = 0; y < faceSize; ++y)
        {
            memcpy(mapped + y * footprint.Footprint.RowPitch, faces[f].data() + y * faceSize * 4,
                   faceSize * 4 * sizeof(float));
        }
        uploadBuf->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = resource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = subresource;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = uploadBuf.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;

        m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        uploadBuffers.push_back(std::move(uploadBuf)); // Keep alive until GPU finishes
    }

    // Transition to SRV
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, lists);
    waitForGPU();

    // Create cubemap SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MipLevels = mipLevels;

    auto handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += srvIndex * m_srvDescriptorSize;
    m_device->CreateShaderResourceView(resource.Get(), &srvDesc, handle);
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

    D3D12_HEAP_PROPERTIES uploadProps{};
    uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = requiredSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuf;
    m_device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));

    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    uploadBuf->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    for (int y = 0; y < size; ++y)
    {
        memcpy(mapped + y * footprint.Footprint.RowPitch, rgPixels + y * size * 2, size * 2 * sizeof(float));
    }
    uploadBuf->Unmap(0, nullptr);

    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = m_brdfLut.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = uploadBuf.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_brdfLut.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, lists);
    waitForGPU();

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    auto handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += srvIndex * m_srvDescriptorSize;
    m_device->CreateShaderResourceView(m_brdfLut.Get(), &srvDesc, handle);
}

bool D3D12Renderer::loadIBL(const std::filesystem::path& hdriPath)
{
    if (!m_initialized)
        return false;

    IBLData data = IBLProcessor::process(hdriPath, 64, 256, 256);
    if (!data.valid)
    {
        spdlog::error("D3D12Renderer: IBL processing failed for {}", hdriPath.string());
        return false;
    }

    // Upload irradiance cubemap (single mip)
    uploadCubemap(3, m_irradianceCubemap, data.irradianceFaces, data.irradianceSize, 1, nullptr);

    // Upload prefiltered cubemap (mip 0 only for now — full mip chain is complex)
    // Use mip 0 (roughness=0) as the base
    if (data.prefilteredMipLevels > 0)
    {
        std::vector<float> mip0Faces[6];
        for (int f = 0; f < 6; ++f)
        {
            mip0Faces[f] = data.prefilteredFaces[f][0].pixels;
        }
        uploadCubemap(4, m_prefilteredCubemap, mip0Faces, data.prefilteredSize, 1, nullptr);
        m_maxPrefilteredMip = 0; // Single mip for now
    }

    // Upload BRDF LUT
    uploadBRDFLut(5, data.brdfLutPixels.data(), data.brdfLutSize);

    m_iblLoaded = true;
    m_iblIntensity = 1.0f;
    spdlog::info("D3D12Renderer: IBL loaded from {}", hdriPath.filename().string());
    return true;
}

} // namespace tpbr
