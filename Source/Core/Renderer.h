#pragma once

#include "SphSolver.h"
#include "ShaderHelper.h"
struct Vertex;

class Renderer
{
public:
    Renderer() {}
    ~Renderer() {}
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Render(
        ID3D12GraphicsCommandList* cmdList,
        const SphSolver* solver,
        const SM::Matrix& view, const SM::Matrix& proj);

    void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper);

private:
    ID3D12Device* m_pDevice = nullptr;

    ComPtr<IDxcBlob> m_ParticleVS;
    ComPtr<IDxcBlob> m_ParticlePS;
    ComPtr<ID3D12RootSignature> m_RenderParticleRootSig;
    ComPtr<ID3D12PipelineState> m_RenderParticlePSO;

    ComPtr<ID3D12Resource> m_QuadVB;
    ComPtr<ID3D12Resource> m_QuadIB;
    ComPtr<ID3D12Resource> m_QuadVBUpload;
    ComPtr<ID3D12Resource> m_QuadIBUpload;
    D3D12_VERTEX_BUFFER_VIEW m_QuadVBView = {};
    D3D12_INDEX_BUFFER_VIEW  m_QuadIBView = {};

private:
    void InitShaders(ShaderHelper* helper);
    void InitRootSignatures();
    void InitPSOs();
    void InitQuadMesh(ID3D12GraphicsCommandList* cmdList);

    void CreateRenderParticleRootSignature();
    void CreateRenderParticlePSO();
};