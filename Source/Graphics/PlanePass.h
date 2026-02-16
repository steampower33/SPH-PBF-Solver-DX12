#pragma once
#include "IRenderPass.h"

struct Vertex;

class PlanePass : public IRenderPass
{
public:
    PlanePass() {}
    ~PlanePass() {}

    PlanePass(const PlanePass&) = delete;
    PlanePass& operator=(const PlanePass&) = delete;

    virtual void Render(const RenderContext& ctx) override;
    virtual void RenderDepthOnly(const RenderContext& ctx) override;

    virtual void OnGui(RenderContext& ctx) override;

private:
    struct Params {
        SM::Matrix View;
        SM::Matrix Proj;
        SM::Matrix ShadowTransform;
        SM::Vector3 LightPos;
        float TileScale = 20.0f;
        SM::Vector3 LightDir;
        float TileCount = 100.0f;
        float SpotAngleCos;
        float ShadowIntensity;
        float pad[2];
    } m_Params;

    ComPtr<ID3D12Resource> m_QuadVB;
    ComPtr<ID3D12Resource> m_QuadIB;

    D3D12_VERTEX_BUFFER_VIEW m_QuadVBView = {};
    D3D12_INDEX_BUFFER_VIEW  m_QuadIBView = {};

    ComPtr<IDxcBlob> m_PlaneVS;
    ComPtr<IDxcBlob> m_PlanePS;

    ComPtr<ID3D12RootSignature> m_PlaneRootSig;
    ComPtr<ID3D12PipelineState> m_PlanePSO;

    CD3DX12_CPU_DESCRIPTOR_HANDLE m_PlaneSRVHandleCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_PlaneSRVHandleGpu;

    virtual void CreateShaders(RenderContext& ctx) override;
    virtual void CreateRootSignatures(RenderContext& ctx) override;
    virtual void CreatePSOs(RenderContext& ctx) override;
    virtual void CreateResources(RenderContext& ctx, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps) override;
};