#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

namespace tpbr
{

using Microsoft::WRL::ComPtr;

/// Simple linear descriptor allocator for a D3D12 descriptor heap.
/// Supports allocating contiguous ranges of descriptors.
/// Does NOT support freeing individual descriptors (reset the whole heap instead).
class DescriptorHeap
{
  public:
    DescriptorHeap() = default;
    ~DescriptorHeap() = default;

    // Non-copyable, movable
    DescriptorHeap(const DescriptorHeap&) = delete;
    DescriptorHeap& operator=(const DescriptorHeap&) = delete;
    DescriptorHeap(DescriptorHeap&&) noexcept = default;
    DescriptorHeap& operator=(DescriptorHeap&&) noexcept = default;

    /// Create the heap.
    bool create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible = false);

    /// Allocate a contiguous block of descriptors.
    /// Returns the index of the first descriptor in the block.
    /// Returns UINT32_MAX on failure (heap full).
    uint32_t allocate(uint32_t count = 1);

    /// Reset the allocator (does not destroy the heap, just resets the free pointer).
    void reset();

    /// Get CPU handle for descriptor at index.
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle(uint32_t index) const;

    /// Get GPU handle for descriptor at index (only valid for shader-visible heaps).
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle(uint32_t index) const;

    /// Get the underlying heap.
    ID3D12DescriptorHeap* heap() const
    {
        return m_heap.Get();
    }

    /// Get descriptor increment size.
    uint32_t descriptorSize() const
    {
        return m_descriptorSize;
    }

    /// Get capacity.
    uint32_t capacity() const
    {
        return m_capacity;
    }

    /// Get number of allocated descriptors.
    uint32_t allocated() const
    {
        return m_allocated;
    }

  private:
    ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t m_descriptorSize = 0;
    uint32_t m_capacity = 0;
    uint32_t m_allocated = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart{};
};

} // namespace tpbr
