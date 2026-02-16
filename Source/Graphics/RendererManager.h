#pragma once

#include "SphSolver.h"
#include "GraphicsCore.h"
#include "RenderContext.h"
#include "IRenderPass.h"

class DescriptorHeapManager;

class RendererManager
{
public:
	RendererManager() {}
	~RendererManager() {}
	RendererManager(const RendererManager&) = delete;
	RendererManager& operator=(const RendererManager&) = delete;

	void Update(const SM::Matrix& view, const SM::Matrix& proj, const SM::Vector3 camPos);

	void Render(ID3D12GraphicsCommandList* cmdList);

	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, float width, float height, GraphicsCore* graphicsCore, SphSolver* sphSolver, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps, DescriptorHeapManager* heapManager);

	void OnGui();

private:
	RenderContext m_RenderContext{};

	std::vector<std::unique_ptr<IRenderPass>> m_RenderPasses;

	void CreateShadowResources(RenderContext& ctx);
	void CreateSceneResources(RenderContext& ctx);
	void CreateHdrResources(RenderContext& ctx);
};