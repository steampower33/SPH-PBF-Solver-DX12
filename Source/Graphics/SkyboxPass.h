#pragma once
#include "IRenderPass.h"

struct Vertex;

class SkyboxPass : public IRenderPass
{
public:
    SkyboxPass() {}
    ~SkyboxPass() {}

    SkyboxPass(const SkyboxPass&) = delete;
    SkyboxPass& operator=(const SkyboxPass&) = delete;

    virtual void Render(const RenderContext& ctx) override;
    virtual void RenderDepthOnly(const RenderContext& ctx) override;
    virtual void OnGui(RenderContext& ctx) override;

    ID3D12Resource* GetCubemapResource() const { return m_EnvCubeMap.Get(); }
private:
    struct Params {
        SM::Matrix View;
        SM::Matrix Proj;
    } m_Params;

    ComPtr<ID3D12Resource> m_VB;
    ComPtr<ID3D12Resource> m_IB;

    D3D12_VERTEX_BUFFER_VIEW m_VBView = {};
    D3D12_INDEX_BUFFER_VIEW  m_IBView = {};

    ComPtr<ID3D12Resource> m_EnvCubeMap;
    ComPtr<ID3D12Resource> m_SpecularCubeMap;
    ComPtr<ID3D12Resource> m_DiffuseCubeMap;
    ComPtr<ID3D12Resource> m_BrdfMap;

    ComPtr<IDxcBlob> m_SkyboxVS;
    ComPtr<IDxcBlob> m_SkyboxPS;

    ComPtr<ID3D12RootSignature> m_SkyboxRootSig;
    ComPtr<ID3D12PipelineState> m_SkyboxPSO;

    CD3DX12_CPU_DESCRIPTOR_HANDLE m_EnvCubeSRVHandleCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_EnvCubeSRVHandleGpu;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_SpecularCubeSRVHandleCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_SpecularCubeSRVHandleGpu;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_DiffuseCubeSRVHandleCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_DiffuseCubeSRVHandleGpu;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_BrdfSRVHandleCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_BrdfSRVHandleGpu;

    virtual void CreateShaders(RenderContext& ctx) override;
    virtual void CreateRootSignatures(RenderContext& ctx) override;
    virtual void CreatePSOs(RenderContext& ctx) override;
    virtual void CreateResources(RenderContext& ctx, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps) override;

    ComPtr<ID3D12Resource> LoadTextureAndCreateSRV(
        ID3D12Device* device,
        ID3D12CommandQueue* cmdQueue,
        std::wstring filepath,
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle,
        D3D12_SRV_DIMENSION dim = D3D12_SRV_DIMENSION_TEXTURECUBE);
};