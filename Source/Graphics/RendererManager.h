#pragma once

#include "SphSolver.h"
#include "GraphicsCore.h"
#include "RenderContext.h"
#include "IRenderPass.h"

class RendererManager
{
public:
	RendererManager() {}
	~RendererManager() {}
	RendererManager(const RendererManager&) = delete;
	RendererManager& operator=(const RendererManager&) = delete;

	void Update(const SM::Matrix& view, const SM::Matrix& proj);

	void Render(ID3D12GraphicsCommandList* cmdList);

	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, float width, float height, GraphicsCore* graphicsCore, SphSolver* sphSolver, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps);

	void OnGui();

private:
	RenderInitContext m_RenderInitContext{};
	RenderContext m_RenderContext{};

	std::vector<std::unique_ptr<IRenderPass>> m_RenderPasses;

	ComPtr<ID3D12Resource> m_SceneColorTex;
	ComPtr<ID3D12Resource> m_SceneDepthTex;

	ComPtr<ID3D12DescriptorHeap> m_SceneRTVHeap;
	ComPtr<ID3D12DescriptorHeap> m_SceneSRVHeap;
	ComPtr<ID3D12DescriptorHeap> m_SceneDSVHeap;

	D3D12_CPU_DESCRIPTOR_HANDLE m_SceneColorRTVHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SceneColorSRVHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SceneDepthSRVHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SceneDSVHandle;

	void CreateSceneResources(const RenderInitContext& ctx);
};