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

    ComPtr<ID3DBlob> equirectBlob, prefilterBlob, brdfLutBlob;
    ComPtr<ID3DBlob> shProjectBlob, shReduceBlob;
    if (!compileComputeShader(shaderDir / "IBLEquirectToCube.hlsl", "CSMain", equirectBlob))
        return false;
    if (!compileComputeShader(shaderDir / "IBLPrefilter.hlsl", "CSMain", prefilterBlob))
        return false;
    if (!compileComputeShader(shaderDir / "IBLBrdfLut.hlsl", "CSMain", brdfLutBlob))
        return false;
    if (!compileComputeShader(shaderDir / "IBLProjectSH.hlsl", "CSMain", shProjectBlob))
        return false;
    if (!compileComputeShader(shaderDir / "IBLReduceSH.hlsl", "CSMain", shReduceBlob))
        return false;

    // Root signature layout for passes with SRV+UAV:
    //   [0] root constants b0   [1] SRV table t0   [2] UAV table u0   + static sampler s0
    // For BRDF LUT (no SRV, no sampler):
    //   [0] root constants b0   [1] UAV table u0
    if (!createRootSignatureAndPSO(device, "EquirectToCube", equirectBlob, 1, 1, 1, m_equirectRS, m_equirectPSO))
        return false;
    if (!createRootSignatureAndPSO(device, "Prefilter", prefilterBlob, 1, 1, 1, m_prefilterRS, m_prefilterPSO))
        return false;
    if (!createRootSignatureAndPSO(device, "BrdfLut", brdfLutBlob, 0, 1, 0, m_brdfLutRS, m_brdfLutPSO))
        return false;
    if (!createRootSignatureAndPSO(device, "SHProject", shProjectBlob, 1, 1, 0, m_shProjectRS, m_shProjectPSO))
        return false;
    if (!createRootSignatureAndPSO(device, "SHReduce", shReduceBlob, 1, 1, 0, m_shReduceRS, m_shReducePSO))
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
    spdlog::info("IBLPipeline: initialized (5 compute shaders)");
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
                                            ComPtr<ID3D12RootSignature>& outRS, ComPtr<ID3D12PipelineState>& outPSO,
                                            int numRootConstants)
{
    std::vector<D3D12_ROOT_PARAMETER> params;
    D3D12_DESCRIPTOR_RANGE srvRange{};
    D3D12_DESCRIPTOR_RANGE uavRange{};

    // [0] root 32-bit constants b0
    {
        D3D12_ROOT_PARAMETER p{};
        p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p.Constants.ShaderRegister = 0;
        p.Constants.Num32BitValues = static_cast<UINT>(numRootConstants);
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
                               int equirectW, int equirectH, int prefilteredSize, int brdfLutSize)
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
    //   EquirectToCube: 6×2=12, SHProject: 2, SHReduce: 2, Prefilter: up to ~108, BRDF: 1
    DescriptorHeap computeHeap;
    computeHeap.create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256, true);

    // ── 4. Create SH projection buffers ────────────────────
    int totalPixels = equirectW * equirectH;
    int numSHGroups = (totalPixels + 255) / 256;

    auto partialSumsBuf =
        createDefaultBuffer(device, static_cast<UINT64>(numSHGroups) * 9 * 16,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto zh3OutBuf = createDefaultBuffer(device, 5 * 16, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Readback buffer for ZH3 data
    D3D12_HEAP_PROPERTIES readbackHP{};
    readbackHP.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc{};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = 80;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> zh3ReadbackBuf;
    device->CreateCommittedResource(&readbackHP, D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                    nullptr, IID_PPV_ARGS(&zh3ReadbackBuf));

    // ── 5. Run compute passes ──────────────────────────────

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

    // Pass 2: SH3 projection + ZH3 extraction (GPU)
    m_cmdAllocator->Reset();
    m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);

    runSH3Projection(device, m_cmdList.Get(), equirectTex.Get(), partialSumsBuf.Get(), equirectW, equirectH,
                     numSHGroups, computeHeap);

    // Barrier: partial sums UAV → SRV
    barrier.Transition.pResource = partialSumsBuf.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &barrier);

    runSHReduction(device, m_cmdList.Get(), partialSumsBuf.Get(), zh3OutBuf.Get(), numSHGroups, computeHeap);

    // Barrier: ZH3 output UAV → COPY_SOURCE
    barrier.Transition.pResource = zh3OutBuf.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &barrier);

    m_cmdList->CopyBufferRegion(zh3ReadbackBuf.Get(), 0, zh3OutBuf.Get(), 0, 80);
    executeAndWait(m_cmdList.Get(), directQueue, m_fence.Get(), m_fenceEvent, m_fenceValue);

    // Readback ZH3 data
    {
        D3D12_RANGE readRange{0, 80};
        float* mapped = nullptr;
        zh3ReadbackBuf->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        std::memcpy(result.zh3Data, mapped, 80);
        D3D12_RANGE writeRange{0, 0};
        zh3ReadbackBuf->Unmap(0, &writeRange);
    }
    spdlog::info("IBLPipeline: ZH3 irradiance projection done (GPU, {} groups)", numSHGroups);

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

// ═════════════════════════════════════════════════════════════
// Buffer helper
// ═════════════════════════════════════════════════════════════

ComPtr<ID3D12Resource> IBLPipeline::createDefaultBuffer(ID3D12Device* device, UINT64 size, D3D12_RESOURCE_FLAGS flags,
                                                        D3D12_RESOURCE_STATES initialState)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;
    ComPtr<ID3D12Resource> res;
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initialState, nullptr, IID_PPV_ARGS(&res));
    return res;
}

// ═════════════════════════════════════════════════════════════
// Pass 2a: SH3 Projection (GPU parallel reduction)
// ═════════════════════════════════════════════════════════════

void IBLPipeline::runSH3Projection(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                                   ID3D12Resource* equirectTex, ID3D12Resource* partialSumsBuf, int equirectW,
                                   int equirectH, int numGroups, DescriptorHeap& heap)
{
    struct
    {
        uint32_t w, h, total, pad;
    } cb = {static_cast<uint32_t>(equirectW), static_cast<uint32_t>(equirectH),
            static_cast<uint32_t>(equirectW * equirectH), 0};

    uint32_t srvIdx = heap.allocate(1);
    uint32_t uavIdx = heap.allocate(1);

    // SRV for equirect 2D texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(equirectTex, &srvDesc, heap.cpuHandle(srvIdx));

    // UAV for structured buffer (partial sums)
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = static_cast<UINT>(numGroups * 9);
    uavDesc.Buffer.StructureByteStride = 16;
    device->CreateUnorderedAccessView(partialSumsBuf, nullptr, &uavDesc, heap.cpuHandle(uavIdx));

    cmdList->SetComputeRootSignature(m_shProjectRS.Get());
    cmdList->SetPipelineState(m_shProjectPSO.Get());
    ID3D12DescriptorHeap* heaps[] = {heap.heap()};
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetComputeRoot32BitConstants(0, 4, &cb, 0);
    cmdList->SetComputeRootDescriptorTable(1, heap.gpuHandle(srvIdx));
    cmdList->SetComputeRootDescriptorTable(2, heap.gpuHandle(uavIdx));

    cmdList->Dispatch(static_cast<UINT>(numGroups), 1, 1);
}

// ═════════════════════════════════════════════════════════════
// Pass 2b: SH Reduction + ZH3 Extraction (GPU)
// ═════════════════════════════════════════════════════════════

void IBLPipeline::runSHReduction(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                                 ID3D12Resource* partialSumsBuf, ID3D12Resource* zh3OutBuf, int numGroups,
                                 DescriptorHeap& heap)
{
    struct
    {
        uint32_t numGroups;
        uint32_t pad[3];
    } cb = {static_cast<uint32_t>(numGroups), {}};

    uint32_t srvIdx = heap.allocate(1);
    uint32_t uavIdx = heap.allocate(1);

    // SRV for structured buffer (partial sums — read)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = static_cast<UINT>(numGroups * 9);
    srvDesc.Buffer.StructureByteStride = 16;
    device->CreateShaderResourceView(partialSumsBuf, &srvDesc, heap.cpuHandle(srvIdx));

    // UAV for structured buffer (ZH3 output — write)
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 5;
    uavDesc.Buffer.StructureByteStride = 16;
    device->CreateUnorderedAccessView(zh3OutBuf, nullptr, &uavDesc, heap.cpuHandle(uavIdx));

    cmdList->SetComputeRootSignature(m_shReduceRS.Get());
    cmdList->SetPipelineState(m_shReducePSO.Get());
    ID3D12DescriptorHeap* heaps[] = {heap.heap()};
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetComputeRoot32BitConstants(0, 4, &cb, 0);
    cmdList->SetComputeRootDescriptorTable(1, heap.gpuHandle(srvIdx));
    cmdList->SetComputeRootDescriptorTable(2, heap.gpuHandle(uavIdx));

    cmdList->Dispatch(1, 1, 1);
}

// ═════════════════════════════════════════════════════════════
// CPU fallback: ZH3 from equirect (double-precision)
// ═════════════════════════════════════════════════════════════

void IBLPipeline::computeZH3CPU(const float* pixels, int w, int h, float outZH3[5][4])
{
    const double PI = 3.14159265358979323846;
    double accum[9][3] = {};

    for (int py = 0; py < h; py++)
    {
        double v = (static_cast<double>(py) + 0.5) / h;
        double elevation = (0.5 - v) * PI;
        double cosEl = std::cos(elevation);
        double sinEl = std::sin(elevation);
        double dOmega = cosEl * (2.0 * PI / w) * (PI / h);

        for (int px = 0; px < w; px++)
        {
            double u = (static_cast<double>(px) + 0.5) / w;
            double azimuth = (2.0 * u - 1.0) * PI;

            double x = cosEl * std::cos(azimuth);
            double y = sinEl;
            double z = cosEl * std::sin(azimuth);

            const float* pixel = pixels + (py * w + px) * 4;
            double r = pixel[0], g = pixel[1], b = pixel[2];

            double basis[9];
            basis[0] = 0.282095;
            basis[1] = 0.488603 * y;
            basis[2] = 0.488603 * z;
            basis[3] = 0.488603 * x;
            basis[4] = 1.092548 * x * y;
            basis[5] = 1.092548 * y * z;
            basis[6] = 0.315392 * (3.0 * z * z - 1.0);
            basis[7] = 1.092548 * x * z;
            basis[8] = 0.546274 * (x * x - y * y);

            for (int i = 0; i < 9; i++)
            {
                double bw = basis[i] * dOmega;
                accum[i][0] += r * bw;
                accum[i][1] += g * bw;
                accum[i][2] += b * bw;
            }
        }
    }

    // Extract ZH3: luminance zonal axis + stored coefficient
    double lumR = 0.2126, lumG = 0.7152, lumB = 0.0722;
    double axX = accum[3][0] * lumR + accum[3][1] * lumG + accum[3][2] * lumB;
    double axY = accum[1][0] * lumR + accum[1][1] * lumG + accum[1][2] * lumB;
    double axZ = accum[2][0] * lumR + accum[2][1] * lumG + accum[2][2] * lumB;
    double axLen = std::sqrt(axX * axX + axY * axY + axZ * axZ);
    if (axLen < 1e-12)
    {
        axX = 0;
        axY = 1;
        axZ = 0;
        axLen = 1;
    }
    axX /= axLen;
    axY /= axLen;
    axZ /= axLen;

    // q = Y2(axis)
    double q[5];
    q[0] = 1.092548 * axX * axY;
    q[1] = 1.092548 * axY * axZ;
    q[2] = 0.315392 * (3.0 * axZ * axZ - 1.0);
    q[3] = 1.092548 * axX * axZ;
    q[4] = 0.546274 * (axX * axX - axY * axY);

    // f_2^0 = K_2^0 × (4π/5) × dot(q, f2) per channel
    double factor = std::sqrt(5.0 / (4.0 * PI)) * (4.0 * PI / 5.0);
    double f20[3];
    for (int c = 0; c < 3; c++)
    {
        double dot = 0;
        for (int j = 0; j < 5; j++)
            dot += q[j] * accum[4 + j][c];
        f20[c] = factor * dot;
    }

    // Pre-convolve and output
    for (int c = 0; c < 3; c++)
    {
        outZH3[0][c] = static_cast<float>(PI * accum[0][c]);
        outZH3[1][c] = static_cast<float>(2.0 * PI / 3.0 * accum[1][c]);
        outZH3[2][c] = static_cast<float>(2.0 * PI / 3.0 * accum[2][c]);
        outZH3[3][c] = static_cast<float>(2.0 * PI / 3.0 * accum[3][c]);
        outZH3[4][c] = static_cast<float>(PI / 4.0 * f20[c]);
    }
    for (int i = 0; i < 5; i++)
        outZH3[i][3] = 0.0f;
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
// Pass 3: BRDF LUT
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
