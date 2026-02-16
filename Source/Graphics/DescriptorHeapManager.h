#pragma once
#include "GraphicsCore.h" // ComPtr, D3D12 헤더 포함

class DescriptorHeapManager
{
public:
    void Initialize(ID3D12Device* device);

    D3D12_CPU_DESCRIPTOR_HANDLE AllocSRV(D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle = nullptr);
    D3D12_CPU_DESCRIPTOR_HANDLE AllocRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE AllocDSV();

    ID3D12DescriptorHeap* GetSRVHeap() const { return m_SrvHeap.Get(); }

private:
    ID3D12Device* m_Device = nullptr;

    ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
    ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_DsvHeap;

    UINT m_SrvSize = 0;
    UINT m_RtvSize = 0;
    UINT m_DsvSize = 0;

    UINT m_SrvIndex = 0;
    UINT m_RtvIndex = 0;
    UINT m_DsvIndex = 0;

    static const UINT MAX_SRV_COUNT = 2048;
    static const UINT MAX_RTV_COUNT = 64;
    static const UINT MAX_DSV_COUNT = 64;
};