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
    virtual void RenderDepthOnly(const RenderContext& ctx) override;
    virtual void OnGui(RenderContext& ctx) override;

private:
    struct BlurParams {
        SM::Vector2 InvScreenSize;
        float DirX;
        float DirY;
        float BlurRadius = 8.0f;
        float SigmaSpatial = 4.0f;
        float SigmaRange = 1.0f;
        float pad;
    } m_BlurParams;

    struct CompositeParams {
        SM::Matrix InvView;
        SM::Matrix InvProj;
        SM::Matrix ShadowTransform;
        SM::Vector3 CamPos;
        float ShadowIntensity;
        SM::Vector2 InvScreenSize;
    } m_CompositeParams;

    struct LightParams {
        SM::Matrix LightView;
        SM::Matrix LightProj;
        float VisualRadius;
    } m_LightParams;

    struct DiffuseParams {
        SM::Matrix ViewProj;
        SM::Matrix InvView;
        SM::Matrix InvProj;
        
        float Scale = 0.01f;
        float BaseAlpha = 0.10f;
        SM::Vector2 InvScreenSize;
        
        float Turbidity = 1.0f;
    } m_DiffuseParams;

    std::wstring m_ShaderBaseName = L"./Shaders/Rendering/";

    bool m_bDebugDrawParticles = false;

    ComPtr<ID3D12Resource> m_QuadVB;
    ComPtr<ID3D12Resource> m_QuadIB;

    D3D12_VERTEX_BUFFER_VIEW m_QuadVBView = {};
    D3D12_INDEX_BUFFER_VIEW  m_QuadIBView = {};

    ComPtr<IDxcBlob> m_ParticleVS;
    ComPtr<IDxcBlob> m_ParticlePS;
    ComPtr<IDxcBlob> m_FluidDepthPS;
    ComPtr<IDxcBlob> m_FluidSmoothPS;
    ComPtr<IDxcBlob> m_FluidThicknessPS;
    ComPtr<IDxcBlob> m_FluidCompositePS;
    ComPtr<IDxcBlob> m_ShadowVS;
    ComPtr<IDxcBlob> m_ShadowPS;
    ComPtr<IDxcBlob> m_DiffuseVS;
    ComPtr<IDxcBlob> m_DiffusePS;

    ComPtr<ID3D12DescriptorHeap> m_FluidRtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_FluidSrvHeap;
    ComPtr<ID3D12Resource> m_FluidDepthTexture;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidDepthRtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidDepthSrvHandle;
    ComPtr<ID3D12Resource> m_BlurTempTexture;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_BlurTempRtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_BlurTempSrvHandle;
    ComPtr<ID3D12Resource> m_FluidThicknessTexture;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidThicknessRtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidThicknessSrvHandle;

    ComPtr<ID3D12RootSignature> m_ShadowRootSig;
    ComPtr<ID3D12RootSignature> m_RenderParticleRootSig;
    ComPtr<ID3D12RootSignature> m_FluidDepthRootSig;
    ComPtr<ID3D12RootSignature> m_FluidSmoothRootSig;
    ComPtr<ID3D12RootSignature> m_FluidThicknessRootSig;
    ComPtr<ID3D12RootSignature> m_FluidCompositeRootSig;
    ComPtr<ID3D12RootSignature> m_DiffuseRootSig;

    ComPtr<ID3D12PipelineState> m_ShadowPSO;
    ComPtr<ID3D12PipelineState> m_RenderParticlePSO;
    ComPtr<ID3D12PipelineState> m_FluidDepthPSO;
    ComPtr<ID3D12PipelineState> m_FluidSmoothPSO;
    ComPtr<ID3D12PipelineState> m_FluidThicknessPSO;
    ComPtr<ID3D12PipelineState> m_FluidCompositePSO;
    ComPtr<ID3D12PipelineState> m_DiffusePSO;

    virtual void CreateShaders(const RenderInitContext& ctx) override;
    virtual void CreateRootSignatures(const RenderInitContext& ctx)override;
    virtual void CreatePSOs(const RenderInitContext& ctx) override;
    virtual void CreateResources(const RenderInitContext& ctx, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps) override;

    void RenderParticles(const RenderContext& ctx);
    void RenderFluidDepth(const RenderContext& ctx);
    void RenderFluidSmooth(const RenderContext& ctx);
    void RenderFluidThickness(const RenderContext& ctx);
    void RenderFluidComposite(const RenderContext& ctx);
    void RenderDiffuse(const RenderContext& ctx);
};