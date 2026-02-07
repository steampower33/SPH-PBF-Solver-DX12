#pragma once
#include "IRenderPass.h"

struct Vertex;

class SSFRPass : public IRenderPass
{
public:
    SSFRPass() {}
    ~SSFRPass() {}

    SSFRPass(const SSFRPass&) = delete;
    SSFRPass& operator=(const SSFRPass&) = delete;

    virtual void Render(const RenderContext& ctx) override;
    virtual void OnGui(RenderContext& ctx) override;

private:
    struct BlurParams {
        SM::Vector2 InvScreenSize;
        float DirX;
        float DirY;
        float Radius = 4.0f;
        float SigmaSpatial = 1.0f;
        float SigmaRange = 0.5f;
        float pad;
    } m_BlurParams;

    struct CompositeParams {
        SM::Matrix InvView;
        SM::Matrix InvProj;
        SM::Vector3 LightDir = { 0.0f, 1.0f, 0.0f };
        float pad0;
        SM::Vector3 CamPos;
        float pad1;
        SM::Vector2 InvScreenSize;
        float pad2[2];
    } m_CompositeParams;

    std::wstring m_ShaderBaseName = L"./Shaders/Rendering/";

    bool m_bDebugDrawParticles = false;

    ComPtr<ID3D12DescriptorHeap> m_FluidRtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_FluidSrvHeap;

    ComPtr<ID3D12Resource> m_QuadVB;
    ComPtr<ID3D12Resource> m_QuadIB;
    ComPtr<ID3D12Resource> m_QuadVBUpload;
    ComPtr<ID3D12Resource> m_QuadIBUpload;

    D3D12_VERTEX_BUFFER_VIEW m_QuadVBView = {};
    D3D12_INDEX_BUFFER_VIEW  m_QuadIBView = {};

    ComPtr<IDxcBlob> m_ParticleVS;
    ComPtr<IDxcBlob> m_ParticlePS;
    ComPtr<IDxcBlob> m_FluidDepthPS;
    ComPtr<IDxcBlob> m_FluidSmoothPS;
    ComPtr<IDxcBlob> m_FluidThicknessPS;
    ComPtr<IDxcBlob> m_FluidCompositePS;

    // [Step 1] Particle Depth (Linear Depth)
    ComPtr<ID3D12Resource> m_FluidDepthTexture;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidDepthRtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidDepthSrvHandle;

    ComPtr<ID3D12RootSignature> m_RenderParticleRootSig;
    ComPtr<ID3D12PipelineState> m_RenderParticlePSO;

    ComPtr<ID3D12RootSignature> m_FluidDepthRootSig;
    ComPtr<ID3D12PipelineState> m_FluidDepthPSO;

    // [Step 2] Depth Blur (Smoothing)
    ComPtr<ID3D12Resource> m_BlurTempTexture;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_BlurTempRtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_BlurTempSrvHandle;

    ComPtr<ID3D12RootSignature> m_FluidSmoothRootSig;
    ComPtr<ID3D12PipelineState> m_FluidSmoothPSO;

    // [Step 3] Thickness
    ComPtr<ID3D12Resource> m_FluidThicknessTexture;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidThicknessRtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidThicknessSrvHandle;

    ComPtr<ID3D12RootSignature> m_FluidThicknessRootSig;
    ComPtr<ID3D12PipelineState> m_FluidThicknessPSO;

    // [Step 4] Final Composite
    ComPtr<ID3D12RootSignature> m_FluidCompositeRootSig;
    ComPtr<ID3D12PipelineState> m_FluidCompositePSO;

    virtual void CreateShaders(const RenderInitContext& ctx) override;
    virtual void CreateRootSignatures(const RenderInitContext& ctx)override;
    virtual void CreatePSOs(const RenderInitContext& ctx) override;
    virtual void CreateResources(const RenderInitContext& ctx) override;

    void RenderParticles(const RenderContext& ctx);
    void RenderFluidDepth(const RenderContext& ctx);
    void RenderFluidSmooth(const RenderContext& ctx);
    void RenderFluidThickness(const RenderContext& ctx);
    void RenderFluidComposite(const RenderContext& ctx);
};