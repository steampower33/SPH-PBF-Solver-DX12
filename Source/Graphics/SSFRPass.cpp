#include "ShaderHelper.h"
#include "GraphicsCore.h"
#include "Vertex.h"
#include "SphSolver.h" 
#include "DescriptorHeapManager.h"

#include "SSFRPass.h"

void SSFRPass::Render(const RenderContext& ctx)
{
	{
		m_CompositeParams.InvView = ctx.InvView;
		m_CompositeParams.InvProj = ctx.InvProj;
		m_CompositeParams.CamPos = ctx.CamPos;
		m_CompositeParams.LightDir = ctx.LightDir;
		m_CompositeParams.LightIntensity = ctx.LightIntensity;

		m_DiffuseParams.ViewProj = ctx.ViewProj;
		m_DiffuseParams.InvView = ctx.InvView;
		m_DiffuseParams.InvProj = ctx.InvProj;

		m_DiffuseParams.InvScreenSize = ctx.InvScreenSize;
	}

	if (m_bDebugDrawParticles)
	{
		RenderParticles(ctx);
	}
	else
	{
		RenderFluidDepth(ctx);
		RenderFluidSmooth(ctx);
		RenderFluidThickness(ctx);
		RenderFluidComposite(ctx);

		if (ctx.Solver->m_bSolveDiffuseParticles)
			RenderDiffuse(ctx);

		ToneMapping(ctx);
	}
}

void SSFRPass::RenderDepthOnly(const RenderContext& ctx)
{
	auto cmdList = ctx.CmdList;
	auto solver = ctx.Solver;

	D3D12_RESOURCE_BARRIER b0 = CD3DX12_RESOURCE_BARRIER::Transition(
		solver->GetParticleBuffer(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
	);
	cmdList->ResourceBarrier(1, &b0);

	cmdList->ClearDepthStencilView(ctx.ShadowDSVHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	cmdList->OMSetRenderTargets(0, nullptr, FALSE, &ctx.ShadowDSVHandle);

	UINT res = ctx.res;
	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)res, (float)res, 0.0f, 1.0f };
	D3D12_RECT scissorRect = { 0, 0, (LONG)res, (LONG)res };
	cmdList->RSSetViewports(1, &viewport);
	cmdList->RSSetScissorRects(1, &scissorRect);

	m_LightParams.LightView = ctx.LightView;
	m_LightParams.LightProj = ctx.LightProj;
	m_LightParams.VisualRadius = ctx.Globals.VisualRadius;

	cmdList->SetGraphicsRootSignature(m_ShadowRootSig.Get());
	cmdList->SetPipelineState(m_ShadowPSO.Get());

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(LightParams) / 4, &m_LightParams, 0);
	cmdList->SetGraphicsRootDescriptorTable(1, m_PosSrvGpu);

	cmdList->IASetVertexBuffers(0, 1, &m_QuadVBView);
	cmdList->IASetIndexBuffer(&m_QuadIBView);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	cmdList->DrawIndexedInstanced(6, solver->GetNumParticles(), 0, 0, 0);

	D3D12_RESOURCE_BARRIER b1 = CD3DX12_RESOURCE_BARRIER::Transition(
		solver->GetParticleBuffer(),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS
	);
	cmdList->ResourceBarrier(1, &b1);
}

void SSFRPass::OnGui(RenderContext& ctx)
{
	if (ImGui::CollapsingHeader("SSFR Visualization", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Debug: Draw Particles (Heatmap)", &m_bDebugDrawParticles);

		ImGui::Separator();

		if (m_bDebugDrawParticles)
		{
			ImGui::DragFloat("Visual Radius", &ctx.Globals.VisualRadius, 0.001f, 0.01f, 0.5f);
		}
		else
		{
			ImGui::SeparatorText("Particles");
			ImGui::DragFloat("Visual Radius", &ctx.Globals.VisualRadius, 0.001f, 0.01f, 0.5f);

			ImGui::SeparatorText("Fluid Material");
			ImGui::DragFloat("Thickness Coeff", &ctx.Globals.ThicknessCoeff, 0.001f, 0.0f, 1.0f);

			ImGui::SeparatorText("Depth Blur");
			ImGui::DragFloat("Blur Radius", &m_BlurParams.BlurRadius, 0.1f, 0.0f, 50.0f);
			ImGui::DragFloat("Sigma Spatial", &m_BlurParams.SigmaSpatial, 0.1f, 0.1f, 50.0f);
			ImGui::DragFloat("Sigma Range", &m_BlurParams.SigmaRange, 0.01f, 0.01f, 10.0f);
		}
	}

	if (ImGui::CollapsingHeader("DiffuseParticles", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::DragFloat("DiffuseScale", &m_DiffuseParams.Scale, 0.001f, 0.0f, 1.0f, "%.3f");
		ImGui::DragFloat("BaseAlpha", &m_DiffuseParams.BaseAlpha, 0.001f, 0.0f, 1.0f, "%.3f");
		ImGui::DragFloat("Turbidity", &m_DiffuseParams.Turbidity, 0.1f, 0.0f, 20.0f, "%.1f");
	}

	if (ImGui::CollapsingHeader("ToneMapping", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::DragFloat("Gamma", &m_ToneMappingParams.Gamma, 0.1f, 0.0f, 10.0f, "%.1f");
		ImGui::DragFloat("Exposure", &m_ToneMappingParams.Exposure, 0.1f, 0.0f, 10.0f, "%.1f");
	}
}

// =========================================================
// Render Steps
// =========================================================

void SSFRPass::RenderParticles(const RenderContext& ctx)
{
	auto cmdList = ctx.CmdList;
	auto solver = ctx.Solver;

	cmdList->RSSetViewports(1, &ctx.Viewport);
	cmdList->RSSetScissorRects(1, &ctx.ScissorRect);

	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	cmdList->ClearRenderTargetView(ctx.CurrentRTV, clearColor, 0, nullptr);
	cmdList->ClearDepthStencilView(ctx.CurrentDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	cmdList->OMSetRenderTargets(1, &ctx.CurrentRTV, FALSE, &ctx.CurrentDSV);

	cmdList->SetGraphicsRootSignature(m_RenderParticleRootSig.Get());
	cmdList->SetPipelineState(m_RenderParticlePSO.Get());

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(RenderContext::GlobalConstants) / 4, &ctx.Globals, 0);
	cmdList->SetGraphicsRootDescriptorTable(1, m_PosSrvGpu);

	cmdList->IASetVertexBuffers(0, 1, &m_QuadVBView);
	cmdList->IASetIndexBuffer(&m_QuadIBView);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	cmdList->DrawIndexedInstanced(6, solver->GetNumParticles(), 0, 0, 0);
}

void SSFRPass::RenderFluidDepth(const RenderContext& ctx)
{
	auto cmdList = ctx.CmdList;
	auto solver = ctx.Solver;

	// Transition: SRV -> RTV
	D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_FluidDepthTexture.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);
	cmdList->ResourceBarrier(1, &barrier);

	// Clear & Set RT
	const float clearColor[] = { 1e9f, 0.0f, 0.0f, 0.0f };
	cmdList->ClearRenderTargetView(m_FluidDepthRtvHandle, clearColor, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &m_FluidDepthRtvHandle, FALSE, &ctx.CurrentDSV);

	// Viewport & Scissor
	cmdList->RSSetViewports(1, &ctx.Viewport);
	cmdList->RSSetScissorRects(1, &ctx.ScissorRect);

	// Pipeline Setup
	cmdList->SetGraphicsRootSignature(m_FluidDepthRootSig.Get());
	cmdList->SetPipelineState(m_FluidDepthPSO.Get());

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(RenderContext::GlobalConstants) / 4, &ctx.Globals, 0);
	cmdList->SetGraphicsRootDescriptorTable(1, m_PosSrvGpu);

	// Draw Quad Instances
	cmdList->IASetVertexBuffers(0, 1, &m_QuadVBView);
	cmdList->IASetIndexBuffer(&m_QuadIBView);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	cmdList->DrawIndexedInstanced(6, solver->GetNumParticles(), 0, 0, 0);
}

void SSFRPass::RenderFluidSmooth(const RenderContext& ctx)
{
	auto cmdList = ctx.CmdList;

	cmdList->SetGraphicsRootSignature(m_FluidSmoothRootSig.Get());
	cmdList->SetPipelineState(m_FluidSmoothPSO.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// [Pass 1] Horizontal: Depth(RTV->SRV) -> Temp(SRV->RTV)
	{
		D3D12_RESOURCE_BARRIER barriers1[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_FluidDepthTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(m_BlurTempTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		};
		cmdList->ResourceBarrier(2, barriers1);

		cmdList->OMSetRenderTargets(1, &m_BlurTempRtvHandle, FALSE, nullptr);

		// Input: Depth (Index 0)
		cmdList->SetGraphicsRootDescriptorTable(1, m_FluidDepthSrvHandleGpu);
		m_BlurParams.DirX = 1.0f; m_BlurParams.DirY = 0.0f;
		cmdList->SetGraphicsRoot32BitConstants(0, sizeof(BlurParams) / 4, &m_BlurParams, 0);
		cmdList->DrawInstanced(3, 1, 0, 0);
	}

	// [Pass 2] Vertical: Temp(RTV->SRV) -> Depth(SRV->RTV)
	D3D12_RESOURCE_BARRIER barriers2[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(m_FluidDepthTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(m_BlurTempTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	};
	cmdList->ResourceBarrier(2, barriers2);

	{
		cmdList->OMSetRenderTargets(1, &m_FluidDepthRtvHandle, FALSE, nullptr);

		// Input: Temp (Index 1)
		cmdList->SetGraphicsRootDescriptorTable(1, m_BlurTempSrvHandleGpu);
		m_BlurParams.DirX = 0.0f; m_BlurParams.DirY = 1.0f;
		cmdList->SetGraphicsRoot32BitConstants(0, sizeof(BlurParams) / 4, &m_BlurParams, 0);
		cmdList->DrawInstanced(3, 1, 0, 0);

		// Cleanup: Depth(RTV->SRV)
		D3D12_RESOURCE_BARRIER barrierEnd = CD3DX12_RESOURCE_BARRIER::Transition(
			m_FluidDepthTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		cmdList->ResourceBarrier(1, &barrierEnd);
	}
}

void SSFRPass::RenderFluidThickness(const RenderContext& ctx)
{
	auto cmdList = ctx.CmdList;
	auto solver = ctx.Solver;

	// Transition: SRV -> RTV
	D3D12_RESOURCE_BARRIER barrierStart = CD3DX12_RESOURCE_BARRIER::Transition(
		m_FluidThicknessTexture.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);
	cmdList->ResourceBarrier(1, &barrierStart);

	// Clear
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	cmdList->ClearRenderTargetView(m_FluidThicknessRtvHandle, clearColor, 0, nullptr);

	// Use Depth Buffer for occlusion (ReadOnly would be better, but sharing DSV)
	cmdList->OMSetRenderTargets(1, &m_FluidThicknessRtvHandle, FALSE, &ctx.SceneDSVHandle);

	cmdList->RSSetViewports(1, &ctx.Viewport);
	cmdList->RSSetScissorRects(1, &ctx.ScissorRect);

	cmdList->SetGraphicsRootSignature(m_FluidDepthRootSig.Get()); // Reusing Depth RootSig
	cmdList->SetPipelineState(m_FluidThicknessPSO.Get());

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(RenderContext::GlobalConstants) / 4, &ctx.Globals, 0);
	cmdList->SetGraphicsRootDescriptorTable(1, m_PosSrvGpu);

	cmdList->IASetVertexBuffers(0, 1, &m_QuadVBView);
	cmdList->IASetIndexBuffer(&m_QuadIBView);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	cmdList->DrawIndexedInstanced(6, solver->GetNumParticles(), 0, 0, 0);

	// Transition: RTV -> SRV
	D3D12_RESOURCE_BARRIER barrierEnd = CD3DX12_RESOURCE_BARRIER::Transition(
		m_FluidThicknessTexture.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);
	cmdList->ResourceBarrier(1, &barrierEnd);
}

void SSFRPass::RenderFluidComposite(const RenderContext& ctx)
{
	auto cmdList = ctx.CmdList;
	auto device = ctx.Device;

	D3D12_RESOURCE_BARRIER barrierStart = CD3DX12_RESOURCE_BARRIER::Transition(
		ctx.SceneColorTex.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);
	cmdList->ResourceBarrier(1, &barrierStart);

	cmdList->SetGraphicsRootSignature(m_FluidCompositeRootSig.Get());
	cmdList->SetPipelineState(m_FluidCompositePSO.Get());

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(CompositeParams) / 4, &m_CompositeParams, 0);
	cmdList->SetGraphicsRootDescriptorTable(1, m_FluidDepthSrvHandleGpu);
	cmdList->SetGraphicsRootDescriptorTable(2, ctx.SceneColorSRVHandleGpu);
	cmdList->SetGraphicsRootDescriptorTable(3, ctx.SkyboxSRVHandleGpu);

	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	cmdList->ClearRenderTargetView(ctx.HdrRTVHandle, clearColor, 0, nullptr);
	cmdList->ClearDepthStencilView(ctx.CurrentDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &ctx.HdrRTVHandle, FALSE, &ctx.CurrentDSV);

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(3, 1, 0, 0);

	D3D12_RESOURCE_BARRIER barrierEnd = CD3DX12_RESOURCE_BARRIER::Transition(
		ctx.SceneColorTex.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);
	cmdList->ResourceBarrier(1, &barrierEnd);
}

void SSFRPass::RenderDiffuse(const RenderContext& ctx)
{
	auto cmdList = ctx.CmdList;
	auto solver = ctx.Solver;
	auto device = ctx.Device;

	D3D12_RESOURCE_BARRIER transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		solver->GetDiffuseParticleBuffer(),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);
	cmdList->ResourceBarrier(1, &transitionBarrier);

	cmdList->SetGraphicsRootSignature(m_DiffuseRootSig.Get());
	cmdList->SetPipelineState(m_DiffusePSO.Get());

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(DiffuseParams) / 4, &m_DiffuseParams, 0);
	cmdList->SetGraphicsRootDescriptorTable(1, m_FluidDepthSrvHandleGpu);

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->ExecuteIndirect(
		solver->GetDrawCommandSignature(),
		1,
		solver->GetDrawArgsBuffer(),
		0,
		nullptr, 0
	);

	D3D12_RESOURCE_BARRIER restoreBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		solver->GetDiffuseParticleBuffer(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
	);
	cmdList->ResourceBarrier(1, &restoreBarrier);
}

void SSFRPass::ToneMapping(const RenderContext& ctx)
{
	auto cmdList = ctx.CmdList;
	auto solver = ctx.Solver;
	auto device = ctx.Device;

	D3D12_RESOURCE_BARRIER transSRV = CD3DX12_RESOURCE_BARRIER::Transition(
		ctx.HdrTex.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);
	cmdList->ResourceBarrier(1, &transSRV);

	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	cmdList->ClearRenderTargetView(ctx.CurrentRTV, clearColor, 0, nullptr);
	cmdList->ClearDepthStencilView(ctx.CurrentDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &ctx.CurrentRTV, FALSE, &ctx.CurrentDSV);

	cmdList->SetGraphicsRootSignature(m_ToneMappingRootSig.Get());
	cmdList->SetPipelineState(m_ToneMappingPSO.Get());

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(ToneMappingParams) / 4, &m_ToneMappingParams, 0);
	cmdList->SetGraphicsRootDescriptorTable(1, ctx.HdrSRVHandleGpu);

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(3, 1, 0, 0);

	D3D12_RESOURCE_BARRIER transRTV = CD3DX12_RESOURCE_BARRIER::Transition(
		ctx.HdrTex.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);
	cmdList->ResourceBarrier(1, &transRTV);
}

// =========================================================
// Initialization Functions
// =========================================================

void SSFRPass::CreateShaders(RenderContext& ctx)
{
	auto helper = ctx.ShaderHelper;

	m_ParticleVS = helper->Compile(m_ShaderBaseName, L"ParticleVS.hlsl", L"main", L"vs_6_0");
	m_ParticlePS = helper->Compile(m_ShaderBaseName, L"ParticlePS.hlsl", L"main", L"ps_6_0");
	m_FluidDepthPS = helper->Compile(m_ShaderBaseName, L"FluidDepthPS.hlsl", L"main", L"ps_6_0");

	m_FluidSmoothPS = helper->Compile(m_ShaderBaseName, L"FluidSmooth.hlsl", L"main", L"ps_6_0");
	m_FluidThicknessPS = helper->Compile(m_ShaderBaseName, L"FluidThicknessPS.hlsl", L"main", L"ps_6_0");
	m_FluidCompositePS = helper->Compile(m_ShaderBaseName, L"FluidComposite.hlsl", L"main", L"ps_6_0");

	m_ShadowVS = helper->Compile(L"./Shaders/Rendering/", L"ShadowVS.hlsl", L"main", L"vs_6_0");
	m_ShadowPS = helper->Compile(L"./Shaders/Rendering/", L"ShadowPS.hlsl", L"main", L"ps_6_0");

	m_DiffuseVS = helper->Compile(m_ShaderBaseName, L"DiffuseVS.hlsl", L"main", L"vs_6_0");
	m_DiffusePS = helper->Compile(m_ShaderBaseName, L"DiffusePS.hlsl", L"main", L"ps_6_0");

	m_ToneMappingPS = helper->Compile(m_ShaderBaseName, L"ToneMappingPS.hlsl", L"main", L"ps_6_0");
}

void SSFRPass::CreateRootSignatures(RenderContext& ctx)
{
	auto device = ctx.Device;

	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

	// Particle / Depth / Thickness (Shared)
	{
		CD3DX12_ROOT_PARAMETER1 params[2];
		params[0].InitAsConstants(sizeof(RenderContext::GlobalConstants) / 4.0, 0, 0, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_DESCRIPTOR_RANGE1 srvRange;
		srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
		params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_VERTEX);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
		desc.Init_1_1(_countof(params), params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Helpers::CreateRootSignature(device, desc, featureData.HighestVersion, m_FluidDepthRootSig, "FluidDepthRootSig");
	}
	m_RenderParticleRootSig = m_FluidDepthRootSig;

	// Smooth
	{
		CD3DX12_ROOT_PARAMETER1 params[2];
		params[0].InitAsConstants(sizeof(BlurParams) / 4, 0);

		CD3DX12_DESCRIPTOR_RANGE1 srvRange;
		srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC sampler = CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
		desc.Init_1_1(_countof(params), params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Helpers::CreateRootSignature(device, desc, featureData.HighestVersion, m_FluidSmoothRootSig, "FluidSmoothRootSig");
	}

	// Composite
	{
		CD3DX12_ROOT_PARAMETER1 params[4];
		params[0].InitAsConstants(sizeof(CompositeParams) / 4, 0);

		CD3DX12_DESCRIPTOR_RANGE1 fluidSrvRange;
		fluidSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

		CD3DX12_DESCRIPTOR_RANGE1 sceneSrvRange;
		sceneSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 3);

		CD3DX12_DESCRIPTOR_RANGE1 skyboxSrvRange;
		skyboxSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 5);

		params[1].InitAsDescriptorTable(1, &fluidSrvRange, D3D12_SHADER_VISIBILITY_PIXEL);
		params[2].InitAsDescriptorTable(1, &sceneSrvRange, D3D12_SHADER_VISIBILITY_PIXEL);
		params[3].InitAsDescriptorTable(1, &skyboxSrvRange, D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC samplers[2];
		samplers[0] = CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
		samplers[1] = CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
		desc.Init_1_1(_countof(params), params, __crt_countof(samplers), samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Helpers::CreateRootSignature(device, desc, featureData.HighestVersion, m_FluidCompositeRootSig, "FluidCompositeRootSig");
	}

	// ShadowMap
	{
		CD3DX12_ROOT_PARAMETER1 params[2];
		params[0].InitAsConstants(sizeof(LightParams) / 4, 0);

		CD3DX12_DESCRIPTOR_RANGE1 srvRange;
		srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
		params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_VERTEX);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
		desc.Init_1_1(_countof(params), params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Helpers::CreateRootSignature(device, desc, featureData.HighestVersion, m_ShadowRootSig, "m_ShadowRootSig");
	}

	// DiffseParticle
	{
		CD3DX12_ROOT_PARAMETER1 params[2];
		params[0].InitAsConstants(sizeof(DiffuseParams) / 4, 0); // b0

		CD3DX12_DESCRIPTOR_RANGE1 srvRange;

		srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

		params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
		desc.Init_1_1(2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Helpers::CreateRootSignature(ctx.Device, desc, D3D_ROOT_SIGNATURE_VERSION_1_1, m_DiffuseRootSig, "m_DiffuseRootSig");
	}

	// ToneMaaping
	{
		CD3DX12_ROOT_PARAMETER1 params[2];
		params[0].InitAsConstants(sizeof(ToneMappingParams) / 4, 0);

		CD3DX12_DESCRIPTOR_RANGE1 range;
		range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
		params[1].InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC samplers[1];
		samplers[0] = CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
		desc.Init_1_1(_countof(params), params, __crt_countof(samplers), samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Helpers::CreateRootSignature(device, desc, featureData.HighestVersion, m_ToneMappingRootSig, "m_ToneMappingRootSig");
	}
}

void SSFRPass::CreatePSOs(RenderContext& ctx)
{
	auto device = ctx.Device;

	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// 1. Particle Render
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
		psoDesc.pRootSignature = m_RenderParticleRootSig.Get();

		psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_ParticleVS->GetBufferPointer(), m_ParticleVS->GetBufferSize());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_ParticlePS->GetBufferPointer(), m_ParticlePS->GetBufferSize());

		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_RenderParticlePSO)));
	}

	// 2. Fluid Depth
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
		psoDesc.pRootSignature = m_FluidDepthRootSig.Get();

		psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_ParticleVS->GetBufferPointer(), m_ParticleVS->GetBufferSize());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_FluidDepthPS->GetBufferPointer(), m_FluidDepthPS->GetBufferSize());

		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_FluidDepthPSO)));
	}

	// 3. Fluid Smooth
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_FluidSmoothRootSig.Get();

		auto FullScreenQuadVS = ctx.ShaderHelper->GetFullScreenQuadVS();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(FullScreenQuadVS->GetBufferPointer(), FullScreenQuadVS->GetBufferSize());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_FluidSmoothPS->GetBufferPointer(), m_FluidSmoothPS->GetBufferSize());

		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_FluidSmoothPSO)));
	}

	// 4. Fluid Thickness
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
		psoDesc.pRootSignature = m_FluidDepthRootSig.Get();

		psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_ParticleVS->GetBufferPointer(), m_ParticleVS->GetBufferSize());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_FluidThicknessPS->GetBufferPointer(), m_FluidThicknessPS->GetBufferSize());

		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

		D3D12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;
		psoDesc.BlendState = blendDesc;

		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_FluidThicknessPSO)));
	}

	// 5. Composite
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_FluidCompositeRootSig.Get();

		auto FullScreenQuadVS = ctx.ShaderHelper->GetFullScreenQuadVS();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(FullScreenQuadVS->GetBufferPointer(), FullScreenQuadVS->GetBufferSize());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_FluidCompositePS->GetBufferPointer(), m_FluidCompositePS->GetBufferSize());

		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_FluidCompositePSO)));
	}

	// Shadow
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
		psoDesc.pRootSignature = m_ShadowRootSig.Get();

		psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_ShadowVS->GetBufferPointer(), m_ShadowVS->GetBufferSize());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_ShadowPS->GetBufferPointer(), m_ShadowPS->GetBufferSize());

		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.DepthBias = 0.0f;
		psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
		psoDesc.RasterizerState.SlopeScaledDepthBias = 0.0f;

		psoDesc.NumRenderTargets = 0;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;

		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_ShadowPSO)));
	}

	// Diffuse
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { nullptr, 0 };
		psoDesc.pRootSignature = m_DiffuseRootSig.Get();

		psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_DiffuseVS->GetBufferPointer(), m_DiffuseVS->GetBufferSize());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_DiffusePS->GetBufferPointer(), m_DiffusePS->GetBufferSize());

		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

		D3D12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState = blendDesc;

		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(ctx.Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_DiffusePSO)));
	}

	// ToneMappingRootSig
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_ToneMappingRootSig.Get();

		auto FullScreenQuadVS = ctx.ShaderHelper->GetFullScreenQuadVS();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(FullScreenQuadVS->GetBufferPointer(), FullScreenQuadVS->GetBufferSize());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_ToneMappingPS->GetBufferPointer(), m_ToneMappingPS->GetBufferSize());

		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_ToneMappingPSO)));
	}
}

void SSFRPass::CreateResources(RenderContext& ctx, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps)
{
	auto device = ctx.Device;
	auto cmdList = ctx.CmdList;

	// Quad Mesh
	{
		Vertex quadVertices[] = {
			{ { -0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f } },
			{ {  0.5f,  0.5f, 0.0f }, { 1.0f, 0.0f } },
			{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f } },
			{ {  0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f } },
		};

		uint16_t quadIndices[] = { 0, 1, 2, 1, 3, 2 };

		UINT vbByteSize = sizeof(quadVertices);
		UINT ibByteSize = sizeof(quadIndices);

		m_QuadVB = Helpers::CreateDefaultBuffer(device, cmdList, quadVertices, vbByteSize, uploadHeaps);
		m_QuadIB = Helpers::CreateDefaultBuffer(device, cmdList, quadIndices, ibByteSize, uploadHeaps);

		m_QuadVBView.BufferLocation = m_QuadVB->GetGPUVirtualAddress();
		m_QuadVBView.StrideInBytes = sizeof(Vertex);
		m_QuadVBView.SizeInBytes = vbByteSize;

		m_QuadIBView.BufferLocation = m_QuadIB->GetGPUVirtualAddress();
		m_QuadIBView.Format = DXGI_FORMAT_R16_UINT;
		m_QuadIBView.SizeInBytes = ibByteSize;
	}

	// Set Params
	{
		m_BlurParams.InvScreenSize = { 1.0f / ctx.Width, 1.0f / ctx.Height };
		m_CompositeParams.InvScreenSize = { 1.0f / ctx.Width, 1.0f / ctx.Height };
	}

	// Create Textures (Depth, Blur, Thickness)
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = m_Width;
	texDesc.Height = m_Height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	// Clear Values
	D3D12_CLEAR_VALUE clearValDepth = { DXGI_FORMAT_R32_FLOAT, { 1e9f } };
	D3D12_CLEAR_VALUE clearValZero = { DXGI_FORMAT_R32_FLOAT, { 0.0f } };

	ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValDepth, IID_PPV_ARGS(&m_FluidDepthTexture)));
	ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValDepth, IID_PPV_ARGS(&m_BlurTempTexture)));
	ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValZero, IID_PPV_ARGS(&m_FluidThicknessTexture)));

	// Helper Lambda
	auto CreateViews = [&](ID3D12Resource* tex, 
		CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle, CD3DX12_CPU_DESCRIPTOR_HANDLE& srvHandle) {
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		device->CreateRenderTargetView(tex, &rtvDesc, rtvHandle);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(tex, &srvDesc, srvHandle);
		};

	m_FluidDepthRtvHandle = ctx.HeapManager->AllocRTV();
	m_BlurTempRtvHandle = ctx.HeapManager->AllocRTV();
	m_FluidThicknessRtvHandle = ctx.HeapManager->AllocRTV();

	m_FluidDepthSrvHandleCpu = ctx.HeapManager->AllocSRV(&m_FluidDepthSrvHandleGpu);
	m_BlurTempSrvHandleCpu = ctx.HeapManager->AllocSRV(&m_BlurTempSrvHandleGpu);
	m_FluidThicknessSrvHandleCpu = ctx.HeapManager->AllocSRV(&m_FluidThicknessSrvHandleGpu);

	CreateViews(m_FluidDepthTexture.Get(), m_FluidDepthRtvHandle, m_FluidDepthSrvHandleCpu);
	CreateViews(m_BlurTempTexture.Get(), m_BlurTempRtvHandle, m_BlurTempSrvHandleCpu);
	CreateViews(m_FluidThicknessTexture.Get(), m_FluidThicknessRtvHandle, m_FluidThicknessSrvHandleCpu);

	auto CreateSRV = [&](ID3D12Resource* buffer, UINT num, UINT stride, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.Buffer.FirstElement = 0;
		desc.Buffer.NumElements = num;
		desc.Buffer.StructureByteStride = stride,
		desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		device->CreateShaderResourceView(buffer, &desc, handle);
		};

	// Create Diffuse Particle Buffer View
	struct DiffuseParticle
	{
		SM::Vector4 PositionLife;
		SM::Vector4 VelocityScale;
	};
	m_DiffuseBufferSrvCpu = ctx.HeapManager->AllocSRV(&m_DiffuseBufferSrvGpu);
	CreateSRV(ctx.Solver->GetDiffuseParticleBuffer(), ctx.Solver->GetNumDiffuseParticles(), sizeof(DiffuseParticle), m_DiffuseBufferSrvCpu);

	m_PosSrvCpu = ctx.HeapManager->AllocSRV(&m_PosSrvGpu);
	CreateSRV(ctx.Solver->GetParticleBuffer(), ctx.Solver->GetNumParticles(), sizeof(SM::Vector3), m_PosSrvCpu);

	m_DensitySrvCpu = ctx.HeapManager->AllocSRV(&m_DensitySrvGpu);
	CreateSRV(ctx.Solver->GetDensityBuffer(), ctx.Solver->GetNumParticles(), sizeof(float), m_DensitySrvCpu);
}