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

    virtual void OnGui(RenderContext& ctx) override;

private:
    struct Params {
        float Scale = 5.0f;
        float TileCount = 80.0f;
    } m_Params;

    ComPtr<ID3D12Resource> m_QuadVB;
    ComPtr<ID3D12Resource> m_QuadIB;
    ComPtr<ID3D12Resource> m_QuadVBUpload;
    ComPtr<ID3D12Resource> m_QuadIBUpload;

    D3D12_VERTEX_BUFFER_VIEW m_QuadVBView = {};
    D3D12_INDEX_BUFFER_VIEW  m_QuadIBView = {};

    ComPtr<IDxcBlob> m_PlaneVS;
    ComPtr<IDxcBlob> m_PlanePS;

    ComPtr<ID3D12RootSignature> m_PlaneRootSig;
    ComPtr<ID3D12PipelineState> m_PlanePSO;

    virtual void CreateShaders(const RenderInitContext& ctx) override;
    virtual void CreateRootSignatures(const RenderInitContext& ctx) override;
    virtual void CreatePSOs(const RenderInitContext& ctx) override;
    virtual void CreateResources(const RenderInitContext& ctx) override;

};