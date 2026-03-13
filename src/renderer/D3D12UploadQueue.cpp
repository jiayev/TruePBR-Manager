#include "D3D12UploadQueue.h"
#include "utils/Log.h"

namespace tpbr
{

D3D12UploadQueue::~D3D12UploadQueue()
{
    flush();
    if (m_ringBufferMapped && m_ringBuffer)
    {
        m_ringBuffer->Unmap(0, nullptr);
        m_ringBufferMapped = nullptr;
    }
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

bool D3D12UploadQueue::init(ID3D12Device* device, uint64_t ringBufferSize)
{
    // Create copy command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_copyQueue));
    if (FAILED(hr))
    {
        spdlog::error("D3D12UploadQueue: Failed to create copy queue: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    // Create command allocator
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_commandAllocator));
    if (FAILED(hr))
    {
        spdlog::error("D3D12UploadQueue: Failed to create command allocator: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    // Create command list (closed state via CreateCommandList1 if available)
    ComPtr<ID3D12Device4> device4;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device4))))
    {
        hr = device4->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_COPY, D3D12_COMMAND_LIST_FLAG_NONE,
                                         IID_PPV_ARGS(&m_commandList));
    }
    else
    {
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, m_commandAllocator.Get(), nullptr,
                                       IID_PPV_ARGS(&m_commandList));
        if (SUCCEEDED(hr))
            m_commandList->Close();
    }
    if (FAILED(hr))
    {
        spdlog::error("D3D12UploadQueue: Failed to create command list: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    // Create fence
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr))
    {
        spdlog::error("D3D12UploadQueue: Failed to create fence: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_fenceValue = 0;

    // Create ring buffer (UPLOAD heap)
    m_ringBufferSize = ringBufferSize;
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = ringBufferSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                         nullptr, IID_PPV_ARGS(&m_ringBuffer));
    if (FAILED(hr))
    {
        spdlog::error("D3D12UploadQueue: Failed to create ring buffer: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    // Persistently map the ring buffer
    D3D12_RANGE readRange{0, 0};
    hr = m_ringBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_ringBufferMapped));
    if (FAILED(hr))
    {
        spdlog::error("D3D12UploadQueue: Failed to map ring buffer: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    m_ringBufferOffset = 0;
    spdlog::info("D3D12UploadQueue: initialized ({} MB ring buffer)", ringBufferSize / (1024 * 1024));
    return true;
}

uint8_t* D3D12UploadQueue::allocate(uint64_t sizeBytes, uint64_t alignment, uint64_t& outOffset)
{
    // Align the current offset
    uint64_t aligned = (m_ringBufferOffset + alignment - 1) & ~(alignment - 1);
    if (aligned + sizeBytes > m_ringBufferSize)
    {
        // Not enough space — caller should flush and reset
        return nullptr;
    }

    outOffset = aligned;
    m_ringBufferOffset = aligned + sizeBytes;
    return m_ringBufferMapped + aligned;
}

bool D3D12UploadQueue::resetCommandList()
{
    HRESULT hr = m_commandAllocator->Reset();
    if (FAILED(hr))
    {
        spdlog::error("D3D12UploadQueue: command allocator reset failed: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }
    hr = m_commandList->Reset(m_commandAllocator.Get(), nullptr);
    if (FAILED(hr))
    {
        spdlog::error("D3D12UploadQueue: command list reset failed: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

uint64_t D3D12UploadQueue::execute()
{
    m_commandList->Close();

    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_copyQueue->ExecuteCommandLists(1, lists);

    ++m_fenceValue;
    m_copyQueue->Signal(m_fence.Get(), m_fenceValue);

    return m_fenceValue;
}

void D3D12UploadQueue::directQueueWaitForCopy(ID3D12CommandQueue* directQueue, uint64_t copyFenceValue)
{
    // GPU-side wait: the direct queue will wait until the copy queue signals this fence value.
    // This does NOT block the CPU.
    directQueue->Wait(m_fence.Get(), copyFenceValue);
}

void D3D12UploadQueue::flush()
{
    if (!m_fence || m_fenceValue == 0)
        return;

    if (m_fence->GetCompletedValue() < m_fenceValue)
    {
        m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, 5000);
    }
}

void D3D12UploadQueue::resetRingBuffer()
{
    m_ringBufferOffset = 0;
}

} // namespace tpbr
