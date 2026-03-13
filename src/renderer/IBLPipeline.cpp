#include "IBLPipeline.h"
#include "utils/Log.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#pragma comment(lib, "d3dcompiler.lib")

namespace tpbr
{

// ── CB structs matching HLSL layouts ───────────────────────

struct EquirectToCubeCB
{
    uint32_t faceIndex;
    uint32_t faceSize;
    uint32_t _pad[2];
};

struct IrradianceCB
{
    uint32_t faceIndex;
    uint32_t outputSize;
    uint32_t sampleCount;
    uint32_t _pad;
};

struct PrefilterCB
{
    uint32_t faceIndex;
    uint32_t outputSize;
    uint32_t sampleCount;
    float roughness;
};

struct BrdfLutCB
{
    uint32_t lutSize;
    uint32_t sampleCount;
    uint32_t _pad[2];
};

// ═══════════════════════════════════════════════════════════

IBLPipeline::~IBLPipeline()
{
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════
// Initialization
// ═══════════════════════════════════════════════════════════

bool IBLPipeline::init(ID3D12Device* device)
{
    std::filesystem::path shaderDir;
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        auto exeDir = std::filesystem::path(exePath).parent_path();
        auto distDir = exeDir / "shaders";
        if (std::filesystem::exists(distDir / "IBLEquirectToCube.hlsl"))
            shaderDir = distDir;
        else
            shaderDir = std::filesystem::path(__FILE__).parent_path();
    }

    ComPtr<ID3DBlob> equirectBlob, irradianceBlob, prefilterBlob, brdfLutBlob;
    if (!compileComputeShader(shaderDir / "IBLEquirectToCube.hlsl", "CSMain", equirectBlob))
        return false;
    if (!compileComputeShader(shaderDir / "IBLIrradiance.hlsl", "CSMain", irradianceBlob))
        return false;
    if (!compileComputeShader(shaderDir / "IBLPrefilter.hlsl", "CSMain", prefilterBlob))
        return false;
    if (!compileComputeShader(shaderDir / "IBLBrdfLut.hlsl", "CSMain", brdfLutBlob))
        return false;

    // Root signature layout for passes with SRV+UAV:
    //   [0] CBV b0   [1] SRV table t0   [2] UAV table u0   + static sampler s0
    // For BRDF LUT (no SRV, no sampler):
    //   [0] CBV b0   [1] UAV table u0
    if (!createRootSignatureAndPSO(device, "EquirectToCube", equirectBlob, 1, 1, 1, m_equirectRS, m_equirectPSO))
        return false;
    if (!createRootSignatureAndPSO(device, "Irradiance", irradianceBlob, 1, 1, 1, m_irradianceRS, m_irradiancePSO))
        return false;
    if (!createRootSignatureAndPSO(device, "Prefilter", prefilterBlob, 1, 1, 1, m_prefilterRS, m_prefilterPSO))
        return false;
    if (!createRootSignatureAndPSO(device, "BrdfLut", brdfLutBlob, 0, 1, 0, m_brdfLutRS, m_brdfLutPSO))
        return false;

    // Create transient command infrastructure
    HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAllocator));
    if (FAILED(hr))
        return false;

    ComPtr<ID3D12Device4> device4;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device4))))
    {
        hr = device4->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE,
                                         IID_PPV_ARGS(&m_cmdList));
    }
    else
    {
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAllocator.Get(), nullptr,
                                       IID_PPV_ARGS(&m_cmdList));
        if (SUCCEEDED(hr))
            m_cmdList->Close();
    }
    if (FAILED(hr))
        return false;

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr))
        return false;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_fenceValue = 0;

    m_initialized = true;
    spdlog::info("IBLPipeline: initialized (4 compute shaders)");
    return true;
}

bool IBLPipeline::compileComputeShader(const std::filesystem::path& hlslPath, const char* entryPoint,
                                       ComPtr<ID3DBlob>& outBlob)
{
    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> errBlob;
    HRESULT hr = D3DCompileFromFile(hlslPath.wstring().c_str(), nullptr, nullptr, entryPoint, "cs_5_0", compileFlags, 0,
                                    &outBlob, &errBlob);
    if (FAILED(hr))
    {
        spdlog::error("IBLPipeline: Failed to compile {}: {}", hlslPath.filename().string(),
                      errBlob ? (char*)errBlob->GetBufferPointer() : "file not found");
        return false;
    }
    return true;
}

bool IBLPipeline::createRootSignatureAndPSO(ID3D12Device* device, const char* name, const ComPtr<ID3DBlob>& csBlob,
                                            int numSRVs, int numUAVs, int numSamplers,
                                            ComPtr<ID3D12RootSignature>& outRS, ComPtr<ID3D12PipelineState>& outPSO)
{
    std::vector<D3D12_ROOT_PARAMETER> params;
    D3D12_DESCRIPTOR_RANGE srvRange{};
    D3D12_DESCRIPTOR_RANGE uavRange{};

    // [0] root 32-bit constants b0 (4 DWORDs = 16 bytes, all CB structs are this size)
    {
        D3D12_ROOT_PARAMETER p{};
        p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p.Constants.ShaderRegister = 0;
        p.Constants.Num32BitValues = 4;
        p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params.push_back(p);
    }
    // [1] SRV table (if any)
    if (numSRVs > 0)
    {
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = static_cast<UINT>(numSRVs);
        srvRange.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER p{};
        p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p.DescriptorTable.NumDescriptorRanges = 1;
        p.DescriptorTable.pDescriptorRanges = &srvRange;
        p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params.push_back(p);
    }
    // [next] UAV table
    {
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = static_cast<UINT>(numUAVs);
        uavRange.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER p{};
        p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p.DescriptorTable.NumDescriptorRanges = 1;
        p.DescriptorTable.pDescriptorRanges = &uavRange;
        p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params.push_back(p);
    }

    D3D12_STATIC_SAMPLER_DESC sampler{};
    if (numSamplers > 0)
    {
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(params.size());
    rsDesc.pParameters = params.data();
    rsDesc.NumStaticSamplers = numSamplers > 0 ? 1u : 0u;
    rsDesc.pStaticSamplers = numSamplers > 0 ? &sampler : nullptr;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr))
    {
        spdlog::error("IBLPipeline: RS serialize failed for {}: {}", name,
                      errBlob ? (char*)errBlob->GetBufferPointer() : "unknown");
        return false;
    }
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&outRS));
    if (FAILED(hr))
        return false;

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = outRS.Get();
    psoDesc.CS = {csBlob->GetBufferPointer(), csBlob->GetBufferSize()};
    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&outPSO));
    if (FAILED(hr))
        return false;

    return true;
}

// ═══════════════════════════════════════════════════════════
// Resource helpers
// ═══════════════════════════════════════════════════════════

ComPtr<ID3D12Resource> IBLPipeline::createDefaultTexture2D(ID3D12Device* device, int w, int h, DXGI_FORMAT format,
                                                           D3D12_RESOURCE_FLAGS flags,
                                                           D3D12_RESOURCE_STATES initialState)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = format;
    rd.SampleDesc.Count = 1;
    rd.Flags = flags;
    ComPtr<ID3D12Resource> res;
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initialState, nullptr, IID_PPV_ARGS(&res));
    return res;
}

ComPtr<ID3D12Resource> IBLPipeline::createDefaultTextureCube(ID3D12Device* device, int faceSize, int mipLevels,
                                                             DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                                                             D3D12_RESOURCE_STATES initialState)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = faceSize;
    rd.Height = faceSize;
    rd.DepthOrArraySize = 6;
    rd.MipLevels = static_cast<UINT16>(mipLevels);
    rd.Format = format;
    rd.SampleDesc.Count = 1;
    rd.Flags = flags;
    ComPtr<ID3D12Resource> res;
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initialState, nullptr, IID_PPV_ARGS(&res));
    return res;
}

// ═══════════════════════════════════════════════════════════
// Execute helper
// ═══════════════════════════════════════════════════════════

static void executeAndWait(ID3D12GraphicsCommandList* cmdList, ID3D12CommandQueue* queue, ID3D12Fence* fence,
                           HANDLE event, uint64_t& fenceValue)
{
    cmdList->Close();
    ID3D12CommandList* lists[] = {cmdList};
    queue->ExecuteCommandLists(1, lists);
    ++fenceValue;
    queue->Signal(fence, fenceValue);
    if (fence->GetCompletedValue() < fenceValue)
    {
        fence->SetEventOnCompletion(fenceValue, event);
        WaitForSingleObject(event, 5000);
    }
}

// ═══════════════════════════════════════════════════════════
// Main process()
// ═══════════════════════════════════════════════════════════

IBLResult IBLPipeline::process(ID3D12Device* device, ID3D12CommandQueue* directQueue, const float* equirectPixels,
                               int equirectW, int equirectH, int irradianceSize, int prefilteredSize, int brdfLutSize)
{
    IBLResult result;
    if (!m_initialized)
    {
        spdlog::error("IBLPipeline: not initialized");
        return result;
    }

    spdlog::info("IBLPipeline: starting GPU IBL processing ({}x{} equirect)", equirectW, equirectH);

    // ── 1. Upload equirect texture to GPU ──────────────────
    // Create equirect texture (DEFAULT heap, for SRV use)
    auto equirectTex = createDefaultTexture2D(device, equirectW, equirectH, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                              D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

    // Create upload buffer
    D3D12_RESOURCE_DESC eqDesc = equirectTex->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&eqDesc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHP{};
    uploadHP.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC uploadBufDesc{};
    uploadBufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufDesc.Width = uploadSize;
    uploadBufDesc.Height = 1;
    uploadBufDesc.DepthOrArraySize = 1;
    uploadBufDesc.MipLevels = 1;
    uploadBufDesc.SampleDesc.Count = 1;
    uploadBufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> uploadBuf;
    device->CreateCommittedResource(&uploadHP, D3D12_HEAP_FLAG_NONE, &uploadBufDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                    nullptr, IID_PPV_ARGS(&uploadBuf));

    // Map and copy row-by-row
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    uploadBuf->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    for (int y = 0; y < equirectH; ++y)
    {
        memcpy(mapped + y * footprint.Footprint.RowPitch, equirectPixels + y * equirectW * 4,
               equirectW * 4 * sizeof(float));
    }
    uploadBuf->Unmap(0, nullptr);

    // Copy to GPU + transition
    m_cmdAllocator->Reset();
    m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = equirectTex.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = uploadBuf.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    m_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = equirectTex.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    m_cmdList->ResourceBarrier(1, &barrier);

    executeAndWait(m_cmdList.Get(), directQueue, m_fence.Get(), m_fenceEvent, m_fenceValue);

    // ── 2. Create output resources ─────────────────────────
    int cubemapSize = std::min(prefilteredSize * 2, 1024);

    // Intermediate cubemap (equirect → cubemap): UAV writable, then SRV readable
    auto cubemapTex =
        createDefaultTextureCube(device, cubemapSize, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Irradiance output
    result.irradianceSize = irradianceSize;
    result.irradianceCubemap =
        createDefaultTextureCube(device, irradianceSize, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Prefiltered output (with mip chain)
    int maxMips = static_cast<int>(std::log2(prefilteredSize)) + 1;
    result.prefilteredSize = prefilteredSize;
    result.prefilteredMipLevels = maxMips;
    result.prefilteredCubemap =
        createDefaultTextureCube(device, prefilteredSize, maxMips, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // BRDF LUT output
    result.brdfLutSize = brdfLutSize;
    result.brdfLut =
        createDefaultTexture2D(device, brdfLutSize, brdfLutSize, DXGI_FORMAT_R32G32_FLOAT,
                               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // ── 3. Create descriptor heap for compute passes ───────
    // We need descriptors for SRVs and UAVs. Estimate generously:
    //   EquirectToCube: 1 SRV + 1 UAV per face = 6 * 2 = 12
    //   Irradiance:     1 SRV + 1 UAV per face = 6 * 2 = 12
    //   Prefilter:      1 SRV + 1 UAV per face per mip = 6 * maxMips * 2
    //   BRDF LUT:       1 UAV = 1
    // Total: ~25 + 12*maxMips.  Reserve 256 to be safe.
    DescriptorHeap computeHeap;
    computeHeap.create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256, true);

    // ── 4. Run compute passes ──────────────────────────────

    // Pass 1: Equirect → Cubemap
    m_cmdAllocator->Reset();
    m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);
    runEquirectToCubemap(device, m_cmdList.Get(), equirectTex.Get(), cubemapTex.Get(), cubemapSize, computeHeap);

    // Barrier: cubemap UAV → SRV for subsequent passes
    barrier.Transition.pResource = cubemapTex.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &barrier);

    executeAndWait(m_cmdList.Get(), directQueue, m_fence.Get(), m_fenceEvent, m_fenceValue);
    spdlog::info("IBLPipeline: equirect → cubemap done ({}x{})", cubemapSize, cubemapSize);

    // Pass 2: Irradiance convolution
    m_cmdAllocator->Reset();
    m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);
    runIrradiance(device, m_cmdList.Get(), cubemapTex.Get(), result.irradianceCubemap.Get(), cubemapSize,
                  irradianceSize, computeHeap);

    // Barrier: irradiance UAV → SRV
    barrier.Transition.pResource = result.irradianceCubemap.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &barrier);

    executeAndWait(m_cmdList.Get(), directQueue, m_fence.Get(), m_fenceEvent, m_fenceValue);
    spdlog::info("IBLPipeline: irradiance done ({}x{})", irradianceSize, irradianceSize);

    // Pass 3: Specular prefilter (all mip levels)
    m_cmdAllocator->Reset();
    m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);
    runPrefilter(device, m_cmdList.Get(), cubemapTex.Get(), result.prefilteredCubemap.Get(), cubemapSize,
                 prefilteredSize, maxMips, computeHeap);

    // Barrier: prefiltered UAV → SRV
    barrier.Transition.pResource = result.prefilteredCubemap.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &barrier);

    executeAndWait(m_cmdList.Get(), directQueue, m_fence.Get(), m_fenceEvent, m_fenceValue);
    spdlog::info("IBLPipeline: prefilter done ({}x{}, {} mips)", prefilteredSize, prefilteredSize, maxMips);

    // Pass 4: BRDF LUT
    m_cmdAllocator->Reset();
    m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);
    runBrdfLut(device, m_cmdList.Get(), result.brdfLut.Get(), brdfLutSize, computeHeap);

    // Barrier: BRDF LUT UAV → SRV
    barrier.Transition.pResource = result.brdfLut.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &barrier);

    executeAndWait(m_cmdList.Get(), directQueue, m_fence.Get(), m_fenceEvent, m_fenceValue);
    spdlog::info("IBLPipeline: BRDF LUT done ({}x{})", brdfLutSize, brdfLutSize);

    result.valid = true;
    spdlog::info("IBLPipeline: GPU IBL processing complete");
    return result;
}

// ═══════════════════════════════════════════════════════════
// Pass 1: Equirect → Cubemap
// ═══════════════════════════════════════════════════════════

void IBLPipeline::runEquirectToCubemap(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                                       ID3D12Resource* equirectTex, ID3D12Resource* outputCubemap, int faceSize,
                                       DescriptorHeap& heap)
{
    UINT groups = (faceSize + 7) / 8;

    for (int face = 0; face < 6; ++face)
    {
        EquirectToCubeCB cbData{};
        cbData.faceIndex = face;
        cbData.faceSize = faceSize;

        // Allocate SRV + UAV descriptors
        uint32_t srvIdx = heap.allocate(1);
        uint32_t uavIdx = heap.allocate(1);

        // SRV for equirect 2D texture
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(equirectTex, &srvDesc, heap.cpuHandle(srvIdx));

        // UAV for cubemap array (write to all faces via RWTexture2DArray)
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.MipSlice = 0;
        uavDesc.Texture2DArray.FirstArraySlice = 0;
        uavDesc.Texture2DArray.ArraySize = 6;
        device->CreateUnorderedAccessView(outputCubemap, nullptr, &uavDesc, heap.cpuHandle(uavIdx));

        // Bind and dispatch
        cmdList->SetComputeRootSignature(m_equirectRS.Get());
        cmdList->SetPipelineState(m_equirectPSO.Get());
        ID3D12DescriptorHeap* heaps[] = {heap.heap()};
        cmdList->SetDescriptorHeaps(1, heaps);

        cmdList->SetComputeRoot32BitConstants(0, 4, &cbData, 0);
        cmdList->SetComputeRootDescriptorTable(1, heap.gpuHandle(srvIdx));
        cmdList->SetComputeRootDescriptorTable(2, heap.gpuHandle(uavIdx));

        cmdList->Dispatch(groups, groups, 1);

        // UAV barrier between faces (ensure writes are visible)
        D3D12_RESOURCE_BARRIER uavBarrier{};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = outputCubemap;
        cmdList->ResourceBarrier(1, &uavBarrier);
    }
}

// ═══════════════════════════════════════════════════════════
// Pass 2: Irradiance Convolution
// ═══════════════════════════════════════════════════════════

void IBLPipeline::runIrradiance(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* inputCubemap,
                                ID3D12Resource* outputIrradiance, int /*inputSize*/, int outputSize,
                                DescriptorHeap& heap)
{
    UINT groups = (outputSize + 7) / 8;

    for (int face = 0; face < 6; ++face)
    {
        IrradianceCB cbData{};
        cbData.faceIndex = face;
        cbData.outputSize = outputSize;
        cbData.sampleCount = 512;

        uint32_t srvIdx = heap.allocate(1);
        uint32_t uavIdx = heap.allocate(1);

        // SRV: TextureCube view of input cubemap
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MipLevels = 1;
        device->CreateShaderResourceView(inputCubemap, &srvDesc, heap.cpuHandle(srvIdx));

        // UAV: RWTexture2DArray for irradiance output
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.MipSlice = 0;
        uavDesc.Texture2DArray.FirstArraySlice = 0;
        uavDesc.Texture2DArray.ArraySize = 6;
        device->CreateUnorderedAccessView(outputIrradiance, nullptr, &uavDesc, heap.cpuHandle(uavIdx));

        cmdList->SetComputeRootSignature(m_irradianceRS.Get());
        cmdList->SetPipelineState(m_irradiancePSO.Get());
        ID3D12DescriptorHeap* heaps[] = {heap.heap()};
        cmdList->SetDescriptorHeaps(1, heaps);

        cmdList->SetComputeRoot32BitConstants(0, 4, &cbData, 0);
        cmdList->SetComputeRootDescriptorTable(1, heap.gpuHandle(srvIdx));
        cmdList->SetComputeRootDescriptorTable(2, heap.gpuHandle(uavIdx));

        cmdList->Dispatch(groups, groups, 1);

        D3D12_RESOURCE_BARRIER uavBarrier{};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = outputIrradiance;
        cmdList->ResourceBarrier(1, &uavBarrier);
    }
}

// ═══════════════════════════════════════════════════════════
// Pass 3: Specular Prefilter (per mip per face)
// ═══════════════════════════════════════════════════════════

void IBLPipeline::runPrefilter(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* inputCubemap,
                               ID3D12Resource* outputPrefiltered, int /*inputSize*/, int outputSize, int mipLevels,
                               DescriptorHeap& heap)
{
    for (int mip = 0; mip < mipLevels; ++mip)
    {
        int mipSize = std::max(outputSize >> mip, 1);
        float roughness = static_cast<float>(mip) / static_cast<float>(mipLevels - 1);
        UINT groups = (mipSize + 7) / 8;
        if (groups == 0)
            groups = 1;

        for (int face = 0; face < 6; ++face)
        {
            PrefilterCB cbData{};
            cbData.faceIndex = face;
            cbData.outputSize = mipSize;
            cbData.sampleCount = 256;
            cbData.roughness = roughness;

            uint32_t srvIdx = heap.allocate(1);
            uint32_t uavIdx = heap.allocate(1);

            // SRV: TextureCube of input cubemap
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.TextureCube.MipLevels = 1;
            device->CreateShaderResourceView(inputCubemap, &srvDesc, heap.cpuHandle(srvIdx));

            // UAV: RWTexture2DArray for this mip level
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            uavDesc.Texture2DArray.MipSlice = mip;
            uavDesc.Texture2DArray.FirstArraySlice = 0;
            uavDesc.Texture2DArray.ArraySize = 6;
            device->CreateUnorderedAccessView(outputPrefiltered, nullptr, &uavDesc, heap.cpuHandle(uavIdx));

            cmdList->SetComputeRootSignature(m_prefilterRS.Get());
            cmdList->SetPipelineState(m_prefilterPSO.Get());
            ID3D12DescriptorHeap* heaps[] = {heap.heap()};
            cmdList->SetDescriptorHeaps(1, heaps);

            cmdList->SetComputeRoot32BitConstants(0, 4, &cbData, 0);
            cmdList->SetComputeRootDescriptorTable(1, heap.gpuHandle(srvIdx));
            cmdList->SetComputeRootDescriptorTable(2, heap.gpuHandle(uavIdx));

            cmdList->Dispatch(groups, groups, 1);

            D3D12_RESOURCE_BARRIER uavBarrier{};
            uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource = outputPrefiltered;
            cmdList->ResourceBarrier(1, &uavBarrier);
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Pass 4: BRDF LUT
// ═══════════════════════════════════════════════════════════

void IBLPipeline::runBrdfLut(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* outputLut,
                             int lutSize, DescriptorHeap& heap)
{
    BrdfLutCB cbData{};
    cbData.lutSize = lutSize;
    cbData.sampleCount = 1024;

    uint32_t uavIdx = heap.allocate(1);

    // UAV for RWTexture2D<float2>
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    device->CreateUnorderedAccessView(outputLut, nullptr, &uavDesc, heap.cpuHandle(uavIdx));

    // BRDF LUT root sig: [0]=CBV, [1]=UAV table (no SRV)
    cmdList->SetComputeRootSignature(m_brdfLutRS.Get());
    cmdList->SetPipelineState(m_brdfLutPSO.Get());
    ID3D12DescriptorHeap* heaps[] = {heap.heap()};
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetComputeRoot32BitConstants(0, 4, &cbData, 0);
    cmdList->SetComputeRootDescriptorTable(1, heap.gpuHandle(uavIdx));

    UINT groups = (lutSize + 7) / 8;
    cmdList->Dispatch(groups, groups, 1);
}

} // namespace tpbr
