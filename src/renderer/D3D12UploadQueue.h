#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace tpbr
{

using Microsoft::WRL::ComPtr;

/// Async texture/buffer upload queue using a dedicated Copy command queue
/// and a ring-buffer upload heap.
///
/// Usage pattern:
///   1. Call beginUpload() to get a mapped pointer into the ring buffer
///   2. Copy data into the mapped region
///   3. Record copy commands via the returned command list
///   4. Call endUpload() to execute the copy queue
///   5. Before using the resource on the direct queue, call
///      directQueueWaitForCopy() to insert a GPU-side wait
class D3D12UploadQueue
{
  public:
    D3D12UploadQueue() = default;
    ~D3D12UploadQueue();

    // Non-copyable
    D3D12UploadQueue(const D3D12UploadQueue&) = delete;
    D3D12UploadQueue& operator=(const D3D12UploadQueue&) = delete;

    /// Initialize with the device and ring buffer size (default 64 MB).
    bool init(ID3D12Device* device, uint64_t ringBufferSize = 64 * 1024 * 1024);

    /// Allocate space in the ring buffer and return a mapped CPU pointer.
    /// Also returns the offset within the ring buffer resource.
    /// The allocation is aligned to the specified alignment.
    /// Returns nullptr if there isn't enough space (caller should flush and retry).
    uint8_t* allocate(uint64_t sizeBytes, uint64_t alignment, uint64_t& outOffset);

    /// Get the ring buffer resource (for use as copy source).
    ID3D12Resource* ringBuffer() const
    {
        return m_ringBuffer.Get();
    }

    /// Reset the copy command list for recording new copy commands.
    /// Must be called before recording any copy commands.
    bool resetCommandList();

    /// Get the copy command list for recording copy commands.
    ID3D12GraphicsCommandList* commandList() const
    {
        return m_commandList.Get();
    }

    /// Execute the recorded copy commands on the copy queue.
    /// Returns the fence value that will be signaled when the copy completes.
    uint64_t execute();

    /// Make the direct queue wait until the copy queue reaches the given fence value.
    void directQueueWaitForCopy(ID3D12CommandQueue* directQueue, uint64_t copyFenceValue);

    /// Flush: wait for all pending copy operations to complete on the CPU.
    void flush();

    /// Get the copy queue fence (for advanced synchronization).
    ID3D12Fence* fence() const
    {
        return m_fence.Get();
    }

    /// Get the last completed fence value.
    uint64_t completedFenceValue() const
    {
        return m_fence->GetCompletedValue();
    }

    /// Reset the ring buffer write head (call after flushing if you want to reclaim all space).
    void resetRingBuffer();

  private:
    ComPtr<ID3D12CommandQueue> m_copyQueue;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    uint64_t m_fenceValue = 0;

    // Ring buffer
    ComPtr<ID3D12Resource> m_ringBuffer;
    uint8_t* m_ringBufferMapped = nullptr;
    uint64_t m_ringBufferSize = 0;
    uint64_t m_ringBufferOffset = 0; // Current write head
};

} // namespace tpbr
