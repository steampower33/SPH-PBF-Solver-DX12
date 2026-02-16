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
        SM::Vector3 CamPos;
        float pad0;
        SM::Vector2 InvScreenSize;
        float pad1[2];
        SM::Vector3 LightDir;
        float LightIntensity = 1.0f;
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
        
        float Scale = 0.005f;
        float BaseAlpha = 0.10f;
        SM::Vector2 InvScreenSize;
        
        float Turbidity = 1.0f;
    } m_DiffuseParams;

    struct ToneMappingParams {
        float Gamma = 2.2f;
        float Exposure = 1.0f;
        float Pad[2];
    } m_ToneMappingParams;

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
    ComPtr<IDxcBlob> m_ToneMappingPS;
    ComPtr<IDxcBlob> m_ShadowVS;
    ComPtr<IDxcBlob> m_ShadowPS;
    ComPtr<IDxcBlob> m_DiffuseVS;
    ComPtr<IDxcBlob> m_DiffusePS;

    ComPtr<ID3D12Resource> m_FluidDepthTexture;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidDepthRtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidDepthSrvHandleCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_FluidDepthSrvHandleGpu;

    ComPtr<ID3D12Resource> m_BlurTempTexture;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_BlurTempRtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_BlurTempSrvHandleCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_BlurTempSrvHandleGpu;

    ComPtr<ID3D12Resource> m_FluidThicknessTexture;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidThicknessRtvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidThicknessSrvHandleCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_FluidThicknessSrvHandleGpu;

    D3D12_CPU_DESCRIPTOR_HANDLE m_DiffuseBufferSrvCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE m_DiffuseBufferSrvGpu;

    D3D12_CPU_DESCRIPTOR_HANDLE m_PosSrvCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE m_PosSrvGpu;

    D3D12_CPU_DESCRIPTOR_HANDLE m_DensitySrvCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE m_DensitySrvGpu;

    ComPtr<ID3D12RootSignature> m_ShadowRootSig;
    ComPtr<ID3D12RootSignature> m_RenderParticleRootSig;
    ComPtr<ID3D12RootSignature> m_FluidDepthRootSig;
    ComPtr<ID3D12RootSignature> m_FluidSmoothRootSig;
    ComPtr<ID3D12RootSignature> m_FluidThicknessRootSig;
    ComPtr<ID3D12RootSignature> m_FluidCompositeRootSig;
    ComPtr<ID3D12RootSignature> m_DiffuseRootSig;
    ComPtr<ID3D12RootSignature> m_ToneMappingRootSig;

    ComPtr<ID3D12PipelineState> m_ShadowPSO;
    ComPtr<ID3D12PipelineState> m_RenderParticlePSO;
    ComPtr<ID3D12PipelineState> m_FluidDepthPSO;
    ComPtr<ID3D12PipelineState> m_FluidSmoothPSO;
    ComPtr<ID3D12PipelineState> m_FluidThicknessPSO;
    ComPtr<ID3D12PipelineState> m_FluidCompositePSO;
    ComPtr<ID3D12PipelineState> m_DiffusePSO;
    ComPtr<ID3D12PipelineState> m_ToneMappingPSO;

    struct DiffuseParticle
    {
        SM::Vector4 PositionLife;
        SM::Vector4 VelocityScale;
    };

    virtual void CreateShaders(RenderContext& ctx) override;
    virtual void CreateRootSignatures(RenderContext& ctx)override;
    virtual void CreatePSOs(RenderContext& ctx) override;
    virtual void CreateResources(RenderContext& ctx, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps) override;

    void RenderParticles(const RenderContext& ctx);
    void RenderFluidDepth(const RenderContext& ctx);
    void RenderFluidSmooth(const RenderContext& ctx);
    void RenderFluidThickness(const RenderContext& ctx);
    void RenderFluidComposite(const RenderContext& ctx);
    void RenderDiffuse(const RenderContext& ctx);
    void ToneMapping(const RenderContext& ctx);
};