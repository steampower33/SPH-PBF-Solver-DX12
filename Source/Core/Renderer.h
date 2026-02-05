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
	void RenderFluidSmooth(ID3D12GraphicsCommandList* cmdList);
	void RenderFluidThickness(ID3D12GraphicsCommandList* cmdList, const SphSolver* solver);
	void RenderFluidComposite(ID3D12GraphicsCommandList* cmdList);

	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, float width, float height, GraphicsCore* graphicsCore);

	struct Params {
		SM::Matrix View;
		SM::Matrix Proj;
		float VisualRadius = 0.02f;
		float ThicknessContribution = 0.05f;
	} m_Params;

	struct BlurParams {
		float InvScreenWidth;
		float InvScreenHeight;
		float DirX;
		float DirY;
		float Radius = 10.0f;
		float SigmaSpatial = 4.0f;
		float SigmaRange = 0.5f;
		float pad;
	} m_BlurParams;

	struct CompositeParams {
		float InvWidth;
		float InvHeight;
		float Pad[2];
		SM::Vector3 LightDir = { 0.0f, 1.0f, 0.0f };
		float Pad2;
	} m_CompositeParams;

private:
	ID3D12Device* m_pDevice = nullptr;
	GraphicsCore* m_pGraphicsCore = nullptr;

	D3D12_FEATURE_DATA_ROOT_SIGNATURE m_FeatureData = {};

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

// LinearDepthMap
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

// Blur Depth
private:
	ComPtr<ID3D12Resource> m_BlurTempTexture;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_BlurTempRtvHandle;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_BlurTempSrvHandle;

	ComPtr<IDxcBlob> m_FullScreenQuadVS;
	ComPtr<IDxcBlob> m_FluidSmoothPS;

	ComPtr<ID3D12RootSignature> m_FluidSmoothRootSig;
	ComPtr<ID3D12PipelineState> m_FluidSmoothPSO;

	void CreateFluidSmoothRootSignature();
	void CreateFluidSmoothPSO();

// Thickness, Use m_FluidDepthRootSig
private:
	ComPtr<ID3D12Resource> m_FluidThicknessTexture;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidThicknessRtvHandle;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_FluidThicknessSrvHandle;

	ComPtr<IDxcBlob> m_FluidThicknessPS;

	ComPtr<ID3D12RootSignature> m_FluidThicknessRootSig;
	ComPtr<ID3D12PipelineState> m_FluidThicknessPSO;

	void CreateFluidThicknessPSO();

// Final Fluid Shading
private:
	ComPtr<IDxcBlob> m_FluidCompositePS;

	ComPtr<ID3D12RootSignature> m_FluidCompositeRootSig;
	ComPtr<ID3D12PipelineState> m_FluidCompositePSO;

	void CreateFluidCompositeRootSignature();
	void CreateFluidCompositePSO();
};