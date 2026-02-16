#include "SSFRPass.h"
#include "PlanePass.h"
#include "SkyboxPass.h"
#include "GraphicsCore.h"
#include "ShaderHelper.h"
#include "DescriptorHeapManager.h"

#include "RendererManager.h"

void RendererManager::Update(const SM::Matrix& view, const SM::Matrix& proj, const SM::Vector3 camPos)
{
	m_RenderContext.Globals.View = view.Transpose();
	m_RenderContext.Globals.Proj = proj.Transpose();
	m_RenderContext.InvView = view.Invert().Transpose();
	m_RenderContext.InvProj = proj.Invert().Transpose();
	m_RenderContext.CamPos = camPos;
	m_RenderContext.ViewProj = (view * proj).Transpose();
	m_RenderContext.InvScreenSize = { 1.0f / m_RenderContext.Width, 1.0f / m_RenderContext.Height };

	{
		SM::Vector3 targetPos = m_RenderContext.TargetPos;
		SM::Vector3 lightPos = m_RenderContext.LightPos;

		SM::Vector3 lightDir = targetPos - lightPos;
		float distToTarget = lightDir.Length();
		lightDir.Normalize();

		SM::Vector3 up = SM::Vector3(0.0f, 1.0f, 0.0f);
		if (abs(lightDir.y) > 0.99f) up = SM::Vector3(1.0f, 0.0f, 0.0f);

		auto view = SM::Matrix::CreateLookAt(lightPos, targetPos, up);

		float viewWidth = 40.0f;
		float viewHeight = 40.0f;

		float nearZ = 1.0f;
		float farZ = 100.0f;

		auto proj = SM::Matrix::CreateOrthographic(viewWidth, viewHeight, nearZ, farZ);

		SM::Matrix T(
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f
		);

		auto shadowT = view * proj * T;

		m_RenderContext.LightView = view.Transpose();
		m_RenderContext.LightProj = proj.Transpose();
		m_RenderContext.ShadowTransform = shadowT.Transpose();
		m_RenderContext.LightPos = lightPos;
		m_RenderContext.LightDir = lightDir;
	}
}

void RendererManager::Render(ID3D12GraphicsCommandList* cmdList)
{
	m_RenderContext.CmdList = cmdList;

	m_RenderContext.FrameIndex = m_RenderContext.GraphicsCore->m_FrameIndex;

	m_RenderContext.CurrentRTV = m_RenderContext.GraphicsCore->GetCurrentBackBufferRTV();
	m_RenderContext.CurrentDSV = m_RenderContext.GraphicsCore->GetDepthStencilView();

	ID3D12DescriptorHeap* heaps[] = { m_RenderContext.HeapManager->GetSRVHeap() };
	cmdList->SetDescriptorHeaps(1, heaps);

	for (auto& pass : m_RenderPasses)
		pass->RenderDepthOnly(m_RenderContext);

	D3D12_RESOURCE_BARRIER b1[] = {
	CD3DX12_RESOURCE_BARRIER::Transition(m_RenderContext.ShadowMapTex.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	};
	cmdList->ResourceBarrier(1, b1);

	for (auto& pass : m_RenderPasses)
		pass->Render(m_RenderContext);

	D3D12_RESOURCE_BARRIER b2[] = {
	CD3DX12_RESOURCE_BARRIER::Transition(m_RenderContext.ShadowMapTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
	};
	cmdList->ResourceBarrier(1, b2);
}

void RendererManager::OnGui()
{
	for (auto& pass : m_RenderPasses)
		pass->OnGui(m_RenderContext);

	if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::DragFloat("LightIntensity", &m_RenderContext.LightIntensity, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat3("Light Pos", &m_RenderContext.LightPos.x, 0.1f, -100.0f, 100.0f);
		ImGui::DragFloat3("Target Pos", &m_RenderContext.TargetPos.x, 0.1f, -100.0f, 100.0f);
		ImGui::DragFloat("ShadowIntensity", &m_RenderContext.ShadowIntensity, 0.01f, 0.0f, 1.0f);
	}
}

void RendererManager::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, float width, float height, GraphicsCore* graphicsCore, SphSolver* sphSolver, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps, DescriptorHeapManager* heapManager)
{
	m_RenderContext.Device = device;
	m_RenderContext.CmdList = cmdList;
	m_RenderContext.ShaderHelper = shaderHelper;
	m_RenderContext.GraphicsCore = graphicsCore;
	m_RenderContext.Width = (int)width;
	m_RenderContext.Height = (int)height;
	m_RenderContext.Queue = graphicsCore->GetCommandQueue();
	m_RenderContext.HeapManager = heapManager;
	m_RenderContext.Solver = sphSolver;

	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
	D3D12_RECT scissorRect = { 0, 0, (LONG)width, (LONG)height };
	m_RenderContext.Viewport = viewport;
	m_RenderContext.ScissorRect = scissorRect;

	CreateSceneResources(m_RenderContext);
	CreateShadowResources(m_RenderContext);
	CreateHdrResources(m_RenderContext);

	m_RenderPasses.emplace_back(std::make_unique<SkyboxPass>());
	m_RenderPasses.emplace_back(std::make_unique<PlanePass>());
	m_RenderPasses.emplace_back(std::make_unique<SSFRPass>()); // Render Last
	for (auto& pass : m_RenderPasses)
		pass->Initialize(m_RenderContext, uploadHeaps);
}


void RendererManager::CreateShadowResources(RenderContext& ctx)
{
	auto device = ctx.Device;

	UINT shadowMapSize = m_RenderContext.res;

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = shadowMapSize;
	texDesc.Height = shadowMapSize;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_D32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearVal = {};
	clearVal.Format = DXGI_FORMAT_D32_FLOAT;
	clearVal.DepthStencil.Depth = 1.0f;
	clearVal.DepthStencil.Stencil = 0;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearVal, IID_PPV_ARGS(&m_RenderContext.ShadowMapTex)
	));
	Helpers::SetDebugName(m_RenderContext.ShadowMapTex.Get(), "Shadow Map Texture");

	ctx.ShadowDSVHandle = ctx.HeapManager->AllocDSV();
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;
	device->CreateDepthStencilView(m_RenderContext.ShadowMapTex.Get(), &dsvDesc, ctx.ShadowDSVHandle);

	ctx.ShadowSRVHandleCpu = ctx.HeapManager->AllocSRV(&ctx.ShadowSRVHandleGpu);
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(m_RenderContext.ShadowMapTex.Get(), &srvDesc, ctx.ShadowSRVHandleCpu);

}

void RendererManager::CreateSceneResources(RenderContext& ctx)
{
	auto device = ctx.Device;
	UINT width = ctx.Width;
	UINT height = ctx.Height;

	// [Color] FP16 For HDR
	DXGI_FORMAT hdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

	// [Depth] TYPELESS 
	// R32_TYPELESS -> DSV: D32_FLOAT -> SRV: R32_FLOAT
	DXGI_FORMAT depthResourceFormat = DXGI_FORMAT_R32_TYPELESS;
	DXGI_FORMAT depthDSVFormat = DXGI_FORMAT_D32_FLOAT;
	DXGI_FORMAT depthSRVFormat = DXGI_FORMAT_R32_FLOAT;

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	// Scene Color
	{
		texDesc.Format = hdrFormat;
		texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_CLEAR_VALUE clearVal = {};
		clearVal.Format = hdrFormat;
		clearVal.Color[0] = 0.0f; clearVal.Color[1] = 0.0f; clearVal.Color[2] = 0.0f; clearVal.Color[3] = 1.0f;

		// Initial State: RENDER_TARGET
		ThrowIfFailed(device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			&clearVal, IID_PPV_ARGS(&ctx.SceneColorTex)
		));

		Helpers::SetDebugName(ctx.SceneColorTex.Get(), "Scene Color Texture");
	}

	// Scene Depth Tex
	{
		texDesc.Format = depthResourceFormat; // TYPELESS!
		texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE clearVal = {};
		clearVal.Format = depthDSVFormat;
		clearVal.DepthStencil.Depth = 1.0f;
		clearVal.DepthStencil.Stencil = 0;

		// Initial State : DEPTH_WRITE
		ThrowIfFailed(device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&clearVal, IID_PPV_ARGS(&ctx.SceneDepthTex)
		));

		Helpers::SetDebugName(ctx.SceneDepthTex.Get(), "Scene Depth Texture");
	}

	// RTV
	ctx.SceneRTVHandle = ctx.HeapManager->AllocRTV();
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = hdrFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device->CreateRenderTargetView(ctx.SceneColorTex.Get(), &rtvDesc, ctx.SceneRTVHandle);

	// DSV
	ctx.SceneDSVHandle = ctx.HeapManager->AllocDSV();
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = depthDSVFormat; // D32_FLOAT
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device->CreateDepthStencilView(ctx.SceneDepthTex.Get(), &dsvDesc, ctx.SceneDSVHandle);

	// SRV
	ctx.SceneColorSRVHandleCpu = ctx.HeapManager->AllocSRV(&ctx.SceneColorSRVHandleGpu);
	D3D12_SHADER_RESOURCE_VIEW_DESC colorSrvDesc = {};
	colorSrvDesc.Format = hdrFormat;
	colorSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	colorSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	colorSrvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(ctx.SceneColorTex.Get(), &colorSrvDesc, ctx.SceneColorSRVHandleCpu);

	ctx.SceneDepthSRVHandleCpu = ctx.HeapManager->AllocSRV(&ctx.SceneDepthSRVHandleGpu);
	D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
	depthSrvDesc.Format = depthSRVFormat; // R32_FLOAT
	depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depthSrvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(ctx.SceneDepthTex.Get(), &depthSrvDesc, ctx.SceneDepthSRVHandleCpu);
}

void RendererManager::CreateHdrResources(RenderContext& ctx)
{
	auto device = ctx.Device;
	UINT width = ctx.Width;
	UINT height = ctx.Height;

	// [Color] FP16 For HDR
	DXGI_FORMAT hdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	// HdrTex
	{
		texDesc.Format = hdrFormat;
		texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_CLEAR_VALUE clearVal = {};
		clearVal.Format = hdrFormat;
		clearVal.Color[0] = 0.0f; clearVal.Color[1] = 0.0f; clearVal.Color[2] = 0.0f; clearVal.Color[3] = 1.0f;

		// Initial State: RENDER_TARGET
		ThrowIfFailed(device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			&clearVal, IID_PPV_ARGS(&m_RenderContext.HdrTex)
		));

		Helpers::SetDebugName(m_RenderContext.HdrTex.Get(), "m_HdrTex");
	}

	// RTV
	ctx.HdrRTVHandle = ctx.HeapManager->AllocRTV();
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = hdrFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device->CreateRenderTargetView(m_RenderContext.HdrTex.Get(), &rtvDesc, ctx.HdrRTVHandle);

	// SRV
	ctx.HdrSRVHandleCpu = ctx.HeapManager->AllocSRV(&ctx.HdrSRVHandleGpu);
	D3D12_SHADER_RESOURCE_VIEW_DESC colorSrvDesc = {};
	colorSrvDesc.Format = hdrFormat;
	colorSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	colorSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	colorSrvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(m_RenderContext.HdrTex.Get(), &colorSrvDesc, ctx.HdrSRVHandleCpu);
}
