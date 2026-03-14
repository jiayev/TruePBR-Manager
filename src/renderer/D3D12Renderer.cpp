#include "D3D12Renderer.h"
#include "IBLPipeline.h"
#include "utils/Log.h"

#include <DirectXMath.h>

#include <fstream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

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

DXGI_FORMAT D3D12Renderer::swapChainFormat() const
{
    return m_hdrEnabled ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
}

float D3D12Renderer::effectivePeakNits() const
{
    if (m_peakBrightnessNits > 0.0f)
        return m_peakBrightnessNits;
    if (m_hdrInfo.maxLuminance > 0.0f)
        return m_hdrInfo.maxLuminance;
    return 1000.0f; // sensible default for unknown displays
}

bool D3D12Renderer::init(HWND hwnd, uint32_t width, uint32_t height)
{
    m_width = width;
    m_height = height;
    m_hwnd = hwnd;

    spdlog::info("D3D12Renderer::init starting ({}x{}, hwnd=0x{:X})", width, height, reinterpret_cast<uintptr_t>(hwnd));

    if (!createDevice())
        return false;
    if (!createCommandInfrastructure())
        return false;

    // Query tearing support (DXGI_FEATURE_PRESENT_ALLOW_TEARING)
    {
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(m_factory.As(&factory5)))
        {
            BOOL allowTearing = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing,
                                                       sizeof(allowTearing))))
            {
                m_tearingSupported = (allowTearing == TRUE);
            }
        }
        spdlog::info("DXGI tearing support: {}", m_tearingSupported ? "yes" : "no");
    }

    m_swapChainFlags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    if (!createSwapChain(hwnd, width, height))
        return false;

    // Query HDR display capability (after swap chain so GetContainingOutput works)
    m_hdrInfo = queryHDRSupport();
    spdlog::info("HDR display support: {} (max={:.0f} nits, min={:.4f} nits)", m_hdrInfo.hdrSupported ? "yes" : "no",
                 m_hdrInfo.maxLuminance, m_hdrInfo.minLuminance);

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

    // --- Prefer discrete (high-performance) GPU ---
    // Try IDXGIFactory6::EnumAdapterByGpuPreference first (Windows 10 1803+).
    ComPtr<IDXGIFactory6> factory6;
    ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(m_factory.As(&factory6)))
    {
        for (UINT i = 0; factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                              IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++i)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;

            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
            {
                char adapterName[256] = {};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapterName, sizeof(adapterName), nullptr,
                                    nullptr);
                spdlog::info("D3D12 device created on adapter (high-perf): {}", adapterName);
                break;
            }
        }
    }

    // Fallback: enumerate all adapters and pick the one with the most dedicated VRAM.
    if (!m_device)
    {
        ComPtr<IDXGIAdapter1> bestAdapter;
        SIZE_T bestVRAM = 0;
        for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;
            if (desc.DedicatedVideoMemory > bestVRAM)
            {
                bestVRAM = desc.DedicatedVideoMemory;
                bestAdapter = adapter;
            }
        }
        if (bestAdapter)
        {
            if (SUCCEEDED(D3D12CreateDevice(bestAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
            {
                DXGI_ADAPTER_DESC1 desc;
                bestAdapter->GetDesc1(&desc);
                char adapterName[256] = {};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapterName, sizeof(adapterName), nullptr,
                                    nullptr);
                spdlog::info("D3D12 device created on adapter (best VRAM): {}", adapterName);
            }
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
    desc.Format = swapChainFormat();
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = FrameCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = m_swapChainFlags;

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

    // Set color space for HDR (scRGB: linear Rec.709 primaries)
    if (m_hdrEnabled)
    {
        UINT colorSpaceSupport = 0;
        hr = m_swapChain->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &colorSpaceSupport);
        if (SUCCEEDED(hr) && (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
        {
            m_swapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
            spdlog::info("Swap chain color space set to scRGB (linear Rec.709)");
        }
        else
        {
            spdlog::warn("scRGB color space not supported by swap chain, HDR may not display correctly");
        }
    }

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

    // Locate shader directory (pre-compiled .cso files)
    std::filesystem::path shaderDir;
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        auto exeDir = std::filesystem::path(exePath).parent_path();
        auto distDir = exeDir / "shaders";
        if (std::filesystem::exists(distDir / "PBRShader_VS.cso"))
            shaderDir = distDir;
        else
        {
            // Development fallback: check build output next to source
            auto srcDir = std::filesystem::path(__FILE__).parent_path();
            shaderDir = srcDir;
        }
    }

    // Load pre-compiled shader bytecode (.cso)
    auto loadCSO = [](const std::filesystem::path& path, std::vector<uint8_t>& outData) -> bool
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            spdlog::error("Failed to open compiled shader: {}", path.string());
            return false;
        }
        auto size = static_cast<size_t>(file.tellg());
        outData.resize(size);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(outData.data()), size);
        if (!file.good())
        {
            spdlog::error("Failed to read compiled shader: {}", path.string());
            return false;
        }
        return true;
    };

    std::vector<uint8_t> vsData, psData;
    if (!loadCSO(shaderDir / "PBRShader_VS.cso", vsData))
        return false;
    if (!loadCSO(shaderDir / "PBRShader_PS.cso", psData))
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
    psoDesc.VS = {vsData.data(), vsData.size()};
    psoDesc.PS = {psData.data(), psData.size()};
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
    psoDesc.RTVFormats[0] = swapChainFormat();
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState))))
    {
        spdlog::error("Failed to create PSO");
        return false;
    }

    // ── Skybox PSO (full-screen triangle, no depth write, no input layout) ──
    {
        std::vector<uint8_t> skyVSData, skyPSData;
        if (!loadCSO(shaderDir / "SkyboxShader_VS.cso", skyVSData))
            return false;
        if (!loadCSO(shaderDir / "SkyboxShader_PS.cso", skyPSData))
            return false;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPso{};
        skyPso.pRootSignature = m_rootSignature.Get();
        skyPso.VS = {skyVSData.data(), skyVSData.size()};
        skyPso.PS = {skyPSData.data(), skyPSData.size()};
        skyPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        skyPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        skyPso.RasterizerState.DepthClipEnable = FALSE;
        skyPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        skyPso.DepthStencilState.DepthEnable = FALSE;
        skyPso.SampleMask = UINT_MAX;
        skyPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        skyPso.NumRenderTargets = 1;
        skyPso.RTVFormats[0] = swapChainFormat();
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

bool D3D12Renderer::recreatePSOs()
{
    // Locate shader directory (pre-compiled .cso files)
    std::filesystem::path shaderDir;
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        auto exeDir = std::filesystem::path(exePath).parent_path();
        auto distDir = exeDir / "shaders";
        if (std::filesystem::exists(distDir / "PBRShader_VS.cso"))
            shaderDir = distDir;
        else
        {
            auto srcDir = std::filesystem::path(__FILE__).parent_path();
            shaderDir = srcDir;
        }
    }

    auto loadCSO = [](const std::filesystem::path& path, std::vector<uint8_t>& outData) -> bool
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            spdlog::error("Failed to open compiled shader: {}", path.string());
            return false;
        }
        auto size = static_cast<size_t>(file.tellg());
        outData.resize(size);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(outData.data()), size);
        return file.good();
    };

    // PBR PSO
    {
        std::vector<uint8_t> vsData, psData;
        if (!loadCSO(shaderDir / "PBRShader_VS.cso", vsData))
            return false;
        if (!loadCSO(shaderDir / "PBRShader_PS.cso", psData))
            return false;

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = {vsData.data(), vsData.size()};
        psoDesc.PS = {psData.data(), psData.size()};
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
        psoDesc.RTVFormats[0] = swapChainFormat();
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;

        if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState))))
        {
            spdlog::error("Failed to recreate PBR PSO");
            return false;
        }
    }

    // Skybox PSO
    {
        std::vector<uint8_t> skyVSData, skyPSData;
        if (!loadCSO(shaderDir / "SkyboxShader_VS.cso", skyVSData))
            return false;
        if (!loadCSO(shaderDir / "SkyboxShader_PS.cso", skyPSData))
            return false;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPso{};
        skyPso.pRootSignature = m_rootSignature.Get();
        skyPso.VS = {skyVSData.data(), skyVSData.size()};
        skyPso.PS = {skyPSData.data(), skyPSData.size()};
        skyPso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        skyPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        skyPso.RasterizerState.DepthClipEnable = FALSE;
        skyPso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        skyPso.DepthStencilState.DepthEnable = FALSE;
        skyPso.SampleMask = UINT_MAX;
        skyPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        skyPso.NumRenderTargets = 1;
        skyPso.RTVFormats[0] = swapChainFormat();
        skyPso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        skyPso.SampleDesc.Count = 1;

        if (FAILED(m_device->CreateGraphicsPipelineState(&skyPso, IID_PPV_ARGS(&m_skyboxPSO))))
        {
            spdlog::error("Failed to recreate skybox PSO");
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
    uploadTexture(0, 0, whiteDiffuse, 1, 1, true);

    // 1x1 flat normal (128, 128, 255) = (0, 0, 1) in tangent space
    uint8_t flatNormal[] = {128, 128, 255, 255};
    uploadTexture(1, 1, flatNormal, 1, 1, false);

    // 1x1 default RMAOS: roughness=128, metallic=0, ao=255, specular=255
    uint8_t defaultRMAOS[] = {128, 0, 255, 255};
    uploadTexture(2, 2, defaultRMAOS, 1, 1, false);

    // 1x1 black emissive (SRV slot 5, tex index 3)
    uint8_t blackEmissive[] = {0, 0, 0, 255};
    uploadTexture(3, 5, blackEmissive, 1, 1, true);

    // 1x1 default feature tex0 — flat coat normal, roughness=128 (SRV slot 6, tex index 4)
    uint8_t defaultFeat0[] = {128, 128, 255, 128};
    uploadTexture(4, 6, defaultFeat0, 1, 1, false);

    // 1x1 default feature tex1 — white subsurface/coat color, full opacity (SRV slot 7, tex index 5)
    uint8_t defaultFeat1[] = {255, 255, 255, 255};
    uploadTexture(5, 7, defaultFeat1, 1, 1, false);

    // Generate 128x128 glint noise texture (SRV slot 8, tex index 6)
    // Each texel packs 4 pairs of (uniform, gaussian) as half-floats into 4 floats.
    // Matches CS noisegen.cs.hlsl output format.
    {
        constexpr int noiseSize = 128;
        constexpr int offset = noiseSize * noiseSize * 0x69420;

        auto wangHash = [](uint32_t seed) -> uint32_t
        {
            seed = (seed ^ 61) ^ (seed >> 16);
            seed *= 9;
            seed = seed ^ (seed >> 4);
            seed *= 0x27d4eb2d;
            seed = seed ^ (seed >> 15);
            return seed;
        };

        auto xorshift = [](uint32_t& state)
        {
            state ^= (state << 13);
            state ^= (state >> 17);
            state ^= (state << 5);
        };

        auto xorshiftFloat = [&xorshift](uint32_t& state) -> float
        {
            xorshift(state);
            return static_cast<float>(state) * (1.0f / 4294967296.0f);
        };

        auto erfinvf = [](float x) -> float
        {
            float w, p;
            w = -logf((1.0f - x) * (1.0f + x));
            if (w < 5.0f)
            {
                w = w - 2.5f;
                p = 2.81022636e-08f;
                p = 3.43273939e-07f + p * w;
                p = -3.5233877e-06f + p * w;
                p = -4.39150654e-06f + p * w;
                p = 0.00021858087f + p * w;
                p = -0.00125372503f + p * w;
                p = -0.00417768164f + p * w;
                p = 0.246640727f + p * w;
                p = 1.50140941f + p * w;
            }
            else
            {
                w = sqrtf(w) - 3.0f;
                p = -0.000200214257f;
                p = 0.000100950558f + p * w;
                p = 0.00134934322f + p * w;
                p = -0.00367342844f + p * w;
                p = 0.00573950773f + p * w;
                p = -0.0076224613f + p * w;
                p = 0.00943887047f + p * w;
                p = 1.00167406f + p * w;
                p = 2.83297682f + p * w;
            }
            return p * x;
        };

        auto invCDF = [&erfinvf](float U) -> float { return sqrtf(2.0f) * erfinvf(2.0f * U - 1.0f); };

        auto packHalves = [](float a, float b) -> float
        {
            // Convert float to IEEE 754 half-float (16-bit), matching HLSL f32tof16
            auto f32tof16 = [](float val) -> uint16_t
            {
                uint32_t f;
                std::memcpy(&f, &val, sizeof(float));
                uint32_t sign = (f >> 16) & 0x8000;
                int32_t exponent = ((f >> 23) & 0xFF) - 127 + 15;
                uint32_t mantissa = f & 0x7FFFFF;
                if (exponent <= 0)
                    return static_cast<uint16_t>(sign);
                if (exponent >= 31)
                    return static_cast<uint16_t>(sign | 0x7C00);
                return static_cast<uint16_t>(sign | (exponent << 10) | (mantissa >> 13));
            };
            uint32_t a16 = f32tof16(a);
            uint32_t b16 = f32tof16(b);
            uint32_t packed = (a16 << 16) | b16;
            float result;
            std::memcpy(&result, &packed, sizeof(float));
            return result;
        };

        std::vector<float> noiseData(noiseSize * noiseSize * 4);

        for (int y = 0; y < noiseSize; ++y)
        {
            for (int x = 0; x < noiseSize; ++x)
            {
                struct Sample
                {
                    float u, g;
                };
                Sample samples[4];

                int cx[4] = {x, x, (x + 1) % noiseSize, (x + 1) % noiseSize};
                int cy[4] = {y, (y + 1) % noiseSize, y, (y + 1) % noiseSize};

                for (int i = 0; i < 4; ++i)
                {
                    uint32_t flatId = static_cast<uint32_t>((cy[i] * 123) * noiseSize + (cx[i] * 123));
                    uint32_t state = wangHash(flatId * 123 + offset);
                    samples[i].u = xorshiftFloat(state);
                    samples[i].g = invCDF(xorshiftFloat(state));
                }

                int idx = (y * noiseSize + x) * 4;
                noiseData[idx + 0] = packHalves(samples[0].u, samples[0].g);
                noiseData[idx + 1] = packHalves(samples[1].u, samples[1].g);
                noiseData[idx + 2] = packHalves(samples[2].u, samples[2].g);
                noiseData[idx + 3] = packHalves(samples[3].u, samples[3].g);
            }
        }

        // Upload as DXGI_FORMAT_R32G32B32A32_FLOAT texture
        const DXGI_FORMAT noiseFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = noiseSize;
        texDesc.Height = noiseSize;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = noiseFormat;
        texDesc.SampleDesc.Count = 1;

        m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                          nullptr, IID_PPV_ARGS(&m_textures[6]));

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        UINT64 requiredSize = 0;
        m_device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &requiredSize);

        uint64_t ringOffset = 0;
        uint8_t* mapped = m_uploadQueue->allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, ringOffset);
        if (!mapped)
        {
            m_uploadQueue->flush();
            m_uploadQueue->resetRingBuffer();
            mapped = m_uploadQueue->allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, ringOffset);
        }

        footprint.Offset = ringOffset;
        const int rowBytes = noiseSize * 4 * sizeof(float);
        for (int row = 0; row < noiseSize; ++row)
        {
            memcpy(mapped + row * footprint.Footprint.RowPitch, noiseData.data() + row * noiseSize * 4, rowBytes);
        }

        m_uploadQueue->resetCommandList();
        auto* copyList = m_uploadQueue->commandList();

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = m_textures[6].Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = m_uploadQueue->ringBuffer();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;

        copyList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        uint64_t copyFence = m_uploadQueue->execute();
        m_uploadQueue->directQueueWaitForCopy(m_directQueue.Get(), copyFence);

        flushGPU();
        m_frames[0].commandAllocator->Reset();
        m_commandList->Reset(m_frames[0].commandAllocator.Get(), nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_textures[6].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_commandList->ResourceBarrier(1, &barrier);
        m_commandList->Close();
        ID3D12CommandList* lists[] = {m_commandList.Get()};
        m_directQueue->ExecuteCommandLists(1, lists);
        flushGPU();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = noiseFormat;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_textures[6].Get(), &srvDesc, m_srvHeap.cpuHandle(m_srvBaseIndex + 8));
    }
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

void D3D12Renderer::uploadTexture(int texIndex, int srvIndex, const uint8_t* rgba, int w, int h, bool srgb)
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
                                          nullptr, IID_PPV_ARGS(&m_textures[texIndex]));
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
    dst.pResource = m_textures[texIndex].Get();
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
    barrier.Transition.pResource = m_textures[texIndex].Get();
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

    m_device->CreateShaderResourceView(m_textures[texIndex].Get(), &srvDesc,
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
    flushGPU();
    uploadMesh(mesh);
}

void D3D12Renderer::setTextures(const uint8_t* diffuseRGBA, int diffuseW, int diffuseH, const uint8_t* normalRGBA,
                                int normalW, int normalH, const uint8_t* rmaosRGBA, int rmaosW, int rmaosH)
{
    if (diffuseRGBA && diffuseW > 0 && diffuseH > 0)
        uploadTexture(0, 0, diffuseRGBA, diffuseW, diffuseH, true);
    else
    {
        uint8_t whiteDiffuse[] = {255, 255, 255, 255};
        uploadTexture(0, 0, whiteDiffuse, 1, 1, true);
    }

    if (normalRGBA && normalW > 0 && normalH > 0)
        uploadTexture(1, 1, normalRGBA, normalW, normalH, false);
    else
    {
        uint8_t flatNormal[] = {128, 128, 255, 255};
        uploadTexture(1, 1, flatNormal, 1, 1, false);
    }

    if (rmaosRGBA && rmaosW > 0 && rmaosH > 0)
        uploadTexture(2, 2, rmaosRGBA, rmaosW, rmaosH, false);
    else
    {
        uint8_t defaultRMAOS[] = {128, 0, 255, 255};
        uploadTexture(2, 2, defaultRMAOS, 1, 1, false);
    }
}

void D3D12Renderer::setMaterialParams(float specularLevel, float roughnessScale)
{
    m_specularLevel = specularLevel;
    m_roughnessScale = roughnessScale;
}

void D3D12Renderer::setFeatureParams(const PBRFeatureFlags& features, const PBRParameters& params)
{
    // Build bitmask matching PBR::Flags in PBRMath.hlsli
    uint32_t flags = 0;
    if (features.emissive)
        flags |= (1 << 0); // HasEmissive
    if (features.parallax)
        flags |= (1 << 1); // HasDisplacement
    if (features.subsurface)
        flags |= (1 << 4); // Subsurface
    if (features.multilayer)
        flags |= (1 << 5); // TwoLayer
    if (features.coatDiffuse)
        flags |= (1 << 6); // ColoredCoat
    if (features.coatNormal)
        flags |= (1 << 8); // CoatNormal
    if (features.fuzz)
        flags |= (1 << 9); // Fuzz
    if (features.hair)
        flags |= (1 << 10); // HairMarschner
    if (features.glint)
        flags |= (1 << 11); // Glint

    // Preserve HasFeatureTexture0/1 bits (2,3) set by setFeatureTextures()
    m_featureFlags = flags | (m_featureFlags & ((1 << 2) | (1 << 3)));

    m_subsurfaceColor = {params.subsurfaceColor[0], params.subsurfaceColor[1], params.subsurfaceColor[2]};
    m_subsurfaceOpacity = params.subsurfaceOpacity;
    m_coatStrength = params.coatStrength;
    m_coatRoughness = params.coatRoughness;
    m_coatSpecularLevel = params.coatSpecularLevel;
    m_emissiveScale = params.emissiveScale;
    m_fuzzColor = {params.fuzzColor[0], params.fuzzColor[1], params.fuzzColor[2]};
    m_fuzzWeight = params.fuzzWeight;
    m_glintScreenSpaceScale = params.glintScreenSpaceScale;
    m_glintLogMicrofacetDensity = params.glintLogMicrofacetDensity;
    m_glintMicrofacetRoughness = params.glintMicrofacetRoughness;
    m_glintDensityRandomization = params.glintDensityRandomization;
}

void D3D12Renderer::setFeatureTextures(const uint8_t* emissiveRGBA, int ew, int eh, const uint8_t* feat0RGBA, int f0w,
                                       int f0h, const uint8_t* feat1RGBA, int f1w, int f1h)
{
    if (emissiveRGBA && ew > 0 && eh > 0)
        uploadTexture(3, 5, emissiveRGBA, ew, eh, true);
    else
    {
        uint8_t black[] = {0, 0, 0, 255};
        uploadTexture(3, 5, black, 1, 1, true);
    }

    if (feat0RGBA && f0w > 0 && f0h > 0)
    {
        uploadTexture(4, 6, feat0RGBA, f0w, f0h, false);
        m_featureFlags |= (1 << 2); // HasFeatureTexture0
    }
    else
    {
        uint8_t defaultFeat0[] = {128, 128, 255, 128};
        uploadTexture(4, 6, defaultFeat0, 1, 1, false);
        m_featureFlags &= ~(1 << 2);
    }

    if (feat1RGBA && f1w > 0 && f1h > 0)
    {
        uploadTexture(5, 7, feat1RGBA, f1w, f1h, false);
        m_featureFlags |= (1 << 3); // HasFeatureTexture1
    }
    else
    {
        uint8_t defaultFeat1[] = {255, 255, 255, 255};
        uploadTexture(5, 7, defaultFeat1, 1, 1, false);
        m_featureFlags &= ~(1 << 3);
    }
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

void D3D12Renderer::setEnvRotation(float angle)
{
    m_envRotation = angle;
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

    m_swapChain->ResizeBuffers(FrameCount, width, height, swapChainFormat(), m_swapChainFlags);
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
    frame.sceneCBMapped->frameCount = 0; // Fixed: no TAA, so keep glint noise stable across frames
    frame.sceneCBMapped->envRotation = m_envRotation;
    for (int i = 0; i < 5; i++)
    {
        frame.sceneCBMapped->zh3Data[i] = {m_zh3Data[i][0], m_zh3Data[i][1], m_zh3Data[i][2], m_zh3Data[i][3]};
    }
    frame.sceneCBMapped->hdrEnabled = m_hdrEnabled ? 1 : 0;
    frame.sceneCBMapped->paperWhiteNits = m_paperWhiteNits;
    frame.sceneCBMapped->peakBrightnessNits = effectivePeakNits();

    frame.materialCBMapped->specularLevel = m_specularLevel;
    frame.materialCBMapped->roughnessScale = m_roughnessScale;
    frame.materialCBMapped->renderFlags = m_renderFlags;
    frame.materialCBMapped->featureFlags = m_featureFlags;
    frame.materialCBMapped->subsurfaceColor = m_subsurfaceColor;
    frame.materialCBMapped->subsurfaceOpacity = m_subsurfaceOpacity;
    frame.materialCBMapped->coatStrength = m_coatStrength;
    frame.materialCBMapped->coatRoughness = m_coatRoughness;
    frame.materialCBMapped->coatSpecularLevel = m_coatSpecularLevel;
    frame.materialCBMapped->emissiveScale = m_emissiveScale;
    frame.materialCBMapped->fuzzColor = m_fuzzColor;
    frame.materialCBMapped->fuzzWeight = m_fuzzWeight;
    frame.materialCBMapped->glintScreenSpaceScale = m_glintScreenSpaceScale;
    frame.materialCBMapped->glintLogMicrofacetDensity = m_glintLogMicrofacetDensity;
    frame.materialCBMapped->glintMicrofacetRoughness = m_glintMicrofacetRoughness;
    frame.materialCBMapped->glintDensityRandomization = m_glintDensityRandomization;

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
    {
        const UINT syncInterval = m_vsyncEnabled ? 1 : 0;
        const UINT presentFlags = (!m_vsyncEnabled && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        m_swapChain->Present(syncInterval, presentFlags);
    }

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
    if (!IBLPipeline::loadHDRI(hdriPath, eqW, eqH, equirect))
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
        spdlog::error("D3D12Renderer: GPU IBL processing failed for {}", hdriPath.filename().string());
        return false;
    }

    spdlog::error("D3D12Renderer: IBL pipeline not initialized");
    return false;
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
    }
}

// ═══════════════════════════════════════════════════════════
// HDR & Frame Rate Control
// ═══════════════════════════════════════════════════════════

HDRDisplayInfo D3D12Renderer::queryHDRSupport() const
{
    HDRDisplayInfo info;

    if (!m_swapChain && !m_factory)
        return info;

    // Get the output the swap chain is currently on
    ComPtr<IDXGIOutput> output;
    HRESULT hr = E_FAIL;
    if (m_swapChain)
        hr = m_swapChain->GetContainingOutput(&output);

    if (FAILED(hr) || !output)
    {
        spdlog::debug("queryHDRSupport: GetContainingOutput failed (hr=0x{:08X}), falling back to enumeration",
                      static_cast<unsigned>(hr));
    }

    // Fallback: enumerate all outputs on all non-software adapters
    if (!output)
    {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT ai = 0; m_factory->EnumAdapters1(ai, &adapter) != DXGI_ERROR_NOT_FOUND; ++ai)
        {
            DXGI_ADAPTER_DESC1 adesc;
            adapter->GetDesc1(&adesc);
            if (adesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;
            ComPtr<IDXGIOutput> adapterOutput;
            for (UINT oi = 0; adapter->EnumOutputs(oi, &adapterOutput) != DXGI_ERROR_NOT_FOUND; ++oi)
            {
                // Prefer the first output that reports HDR
                ComPtr<IDXGIOutput6> out6;
                if (SUCCEEDED(adapterOutput.As(&out6)))
                {
                    DXGI_OUTPUT_DESC1 d1{};
                    if (SUCCEEDED(out6->GetDesc1(&d1)))
                    {
                        spdlog::debug("queryHDRSupport: adapter {} output {} colorSpace={} maxLum={:.0f}",
                                      ai, oi, static_cast<int>(d1.ColorSpace), d1.MaxLuminance);
                        if (d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
                        {
                            output = adapterOutput;
                            break;
                        }
                        if (!output)
                            output = adapterOutput; // keep first as fallback
                    }
                }
                adapterOutput.Reset();
            }
            if (output)
                break;
            adapter.Reset();
        }
    }

    if (!output)
    {
        spdlog::warn("queryHDRSupport: no DXGI output found");
        return info;
    }

    ComPtr<IDXGIOutput6> output6;
    if (FAILED(output.As(&output6)))
    {
        spdlog::warn("queryHDRSupport: IDXGIOutput6 not available (old driver?)");
        return info;
    }

    DXGI_OUTPUT_DESC1 desc1{};
    if (FAILED(output6->GetDesc1(&desc1)))
    {
        spdlog::warn("queryHDRSupport: GetDesc1 failed");
        return info;
    }

    spdlog::info("queryHDRSupport: ColorSpace={}, BitsPerColor={}, MaxLum={:.0f}, MinLum={:.4f}, MaxFullFrame={:.0f}",
                 static_cast<int>(desc1.ColorSpace), desc1.BitsPerColor,
                 desc1.MaxLuminance, desc1.MinLuminance, desc1.MaxFullFrameLuminance);

    info.minLuminance = desc1.MinLuminance;
    info.maxLuminance = desc1.MaxLuminance;
    info.maxFullFrameLuminance = desc1.MaxFullFrameLuminance;

    // HDR is considered supported only if the OS has HDR enabled on this display
    info.hdrSupported = (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);

    return info;
}

void D3D12Renderer::setVSync(bool enabled)
{
    m_vsyncEnabled = enabled;
}

void D3D12Renderer::setHDREnabled(bool enabled)
{
    if (!m_initialized || !m_hwnd)
        return;

    // Only allow HDR if the display supports it
    if (enabled && !m_hdrInfo.hdrSupported)
    {
        spdlog::warn("HDR requested but display does not support it — ignoring");
        return;
    }

    if (m_hdrEnabled == enabled)
        return;

    m_hdrEnabled = enabled;
    spdlog::info("HDR output {}", m_hdrEnabled ? "enabled" : "disabled");

    rebuildSwapChainAndPSO();
}

void D3D12Renderer::setPaperWhiteNits(float nits)
{
    m_paperWhiteNits = std::clamp(nits, 80.0f, 400.0f);
}

void D3D12Renderer::setPeakBrightnessNits(float nits)
{
    m_peakBrightnessNits = std::max(nits, 0.0f); // 0 = use display max
}

void D3D12Renderer::rebuildSwapChainAndPSO()
{
    if (!m_initialized || !m_hwnd)
        return;

    flushGPU();

    // Release swap-chain-dependent resources
    for (auto& rt : m_renderTargets)
        rt.Reset();
    m_depthStencilBuffer.Reset();
    m_pipelineState.Reset();
    m_skyboxPSO.Reset();
    m_swapChain.Reset();

    // Recreate swap chain with correct format
    if (!createSwapChain(m_hwnd, m_width, m_height))
    {
        spdlog::error("Failed to recreate swap chain during HDR toggle");
        return;
    }

    // Recreate RTVs
    for (UINT i = 0; i < FrameCount; ++i)
    {
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_rtvHeap.cpuHandle(i));
    }

    createDepthStencil(m_width, m_height);

    // Recreate PSOs with matching RTV format (preserves root signature + SRV heap)
    if (!recreatePSOs())
    {
        spdlog::error("Failed to recreate PSOs during HDR toggle");
        return;
    }

    spdlog::info("Swap chain rebuilt: format={}, HDR={}", m_hdrEnabled ? "R16G16B16A16_FLOAT" : "R8G8B8A8_UNORM",
                 m_hdrEnabled);
}

} // namespace tpbr
