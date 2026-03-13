#include "DescriptorHeap.h"
#include "utils/Log.h"

namespace tpbr
{

bool DescriptorHeap::create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity,
                            bool shaderVisible)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.NumDescriptors = capacity;
    desc.Type = type;
    if (shaderVisible)
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    if (FAILED(hr))
    {
        spdlog::error("DescriptorHeap::create failed: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);
    m_capacity = capacity;
    m_allocated = 0;
    m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
    if (shaderVisible)
        m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
    else
        m_gpuStart = {};

    return true;
}

uint32_t DescriptorHeap::allocate(uint32_t count)
{
    if (m_allocated + count > m_capacity)
    {
        spdlog::error("DescriptorHeap: out of descriptors (allocated={}, requested={}, capacity={})", m_allocated,
                      count, m_capacity);
        return UINT32_MAX;
    }
    uint32_t index = m_allocated;
    m_allocated += count;
    return index;
}

void DescriptorHeap::reset()
{
    m_allocated = 0;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::cpuHandle(uint32_t index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cpuStart;
    handle.ptr += static_cast<SIZE_T>(index) * m_descriptorSize;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::gpuHandle(uint32_t index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_gpuStart;
    handle.ptr += static_cast<UINT64>(index) * m_descriptorSize;
    return handle;
}

} // namespace tpbr
