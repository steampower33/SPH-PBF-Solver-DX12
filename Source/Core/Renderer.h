#pragma once

#include "SphSolver.h"
#include "ShaderHelper.h"
#include "GraphicsCore.h"
struct Vertex;

class Renderer
{
public:
	Renderer() {}
	~Renderer() {}
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	void Update(const SM::Matrix& view, const SM::Matrix& proj);

	void RenderParticles(ID3D12GraphicsCommandList* cmdList, const SphSolver* solver);
	void RenderFluidDepth(ID3D12GraphicsCommandList* cmdList, const SphSolver* solver);

	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, float width, float height, GraphicsCore* graphicsCore);

	struct Params {
		SM::Matrix View;
		SM::Matrix Proj;
		float VisualRadius = 0.02f;
	} m_Params;
private:
	ID3D12Device* m_pDevice = nullptr;
	GraphicsCore* m_pGraphicsCore = nullptr;

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
	void CreateShaders(ShaderHelper* helper);
	void CreateRootSignatures();
	void CreatePSOs();
	void CreateQuadMesh(ID3D12GraphicsCommandList* cmdList);

	void CreateRenderParticleRootSignature();
	void CreateRenderParticlePSO();

private:
	ComPtr<ID3D12Resource> m_FluidDepthTexture;

	ComPtr<ID3D12DescriptorHeap> m_FluidRtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_FluidSrvHeap;

	CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidDepthRtvHandle;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidDepthSrvHandle;

	UINT m_Width = 0;
	UINT m_Height = 0;

	void CreateFluidDepthResources(ID3D12Device* device);

	ComPtr<ID3D12RootSignature> m_FluidDepthRootSig;
	ComPtr<ID3D12PipelineState> m_FluidDepthPSO;

	ComPtr<IDxcBlob> m_FluidDepthPS;

	void CreateFluidDepthRootSignature();
	void CreateFluidDepthPSO();
};