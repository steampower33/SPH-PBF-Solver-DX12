#include "DescriptorHeapManager.h"

void DescriptorHeapManager::Initialize(ID3D12Device* device)
{
    m_Device = device;

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = MAX_SRV_COUNT;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_SrvHeap)));
    m_SrvHeap->SetName(L"Global SRV Heap");

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = MAX_RTV_COUNT;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_RtvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.NumDescriptors = MAX_DSV_COUNT;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_DsvHeap)));

    m_SrvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_RtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_DsvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapManager::AllocSRV(D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
{
    assert(m_SrvIndex < MAX_SRV_COUNT && "SRV Heap Full!");

    auto cpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_SrvHeap->GetCPUDescriptorHandleForHeapStart(), m_SrvIndex, m_SrvSize);

    if (outGpuHandle)
    {
        *outGpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_SrvHeap->GetGPUDescriptorHandleForHeapStart(), m_SrvIndex, m_SrvSize);
    }

    m_SrvIndex++;
    return cpu;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapManager::AllocRTV()
{
    assert(m_RtvIndex < MAX_RTV_COUNT && "RTV Heap Full!");
    auto cpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_RtvHeap->GetCPUDescriptorHandleForHeapStart(), m_RtvIndex, m_RtvSize);
    m_RtvIndex++;
    return cpu;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapManager::AllocDSV()
{
    assert(m_DsvIndex < MAX_DSV_COUNT && "DSV Heap Full!");
    auto cpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_DsvHeap->GetCPUDescriptorHandleForHeapStart(), m_DsvIndex, m_DsvSize);
    m_DsvIndex++;
    return cpu;
}