#pragma once

#include "SphSolver.h"
#include "GraphicsCore.h"
#include "RenderContext.h"
#include "SSFRPass.h"

class RendererManager
{
public:
	RendererManager() {}
	~RendererManager() {}
	RendererManager(const RendererManager&) = delete;
	RendererManager& operator=(const RendererManager&) = delete;

	void Update(const SM::Matrix& view, const SM::Matrix& proj);

	void Render(ID3D12GraphicsCommandList* cmdList);

	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, float width, float height, GraphicsCore* graphicsCore, SphSolver* sphSolver);

	void OnGui();

private:
	RenderInitContext m_RenderInitContext{};
	RenderContext m_RenderContext{};

	SSFRPass m_SSFRPass{};
};