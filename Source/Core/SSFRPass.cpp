#include "RenderContext.h"
#include "ShaderHelper.h"
#include "GraphicsCore.h"
#include "Vertex.h"
#include "SphSolver.h" 

#include "SSFRPass.h"

void SSFRPass::Initialize(const RenderInitContext& ctx)
{
	m_Width = ctx.Width;
	m_Height = ctx.Height;

	CreateShaders(ctx);
	CreateRootSignatures(ctx);
	CreatePSOs(ctx);
	CreateQuadMesh(ctx);
	CreateFluidResources(ctx);
}

void SSFRPass::Render(const RenderContext& ctx)
{
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
	}
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

			if (ImGui::DragFloat3("Light Dir", &m_CompositeParams.LightDir.x, 0.05f, -1.0f, 1.0f))
			{
				m_CompositeParams.LightDir.Normalize();
			}

			ImGui::SeparatorText("Depth Blur");
			ImGui::DragFloat("Blur Radius", &m_BlurParams.Radius, 0.1f, 0.0f, 50.0f);
			ImGui::DragFloat("Sigma Spatial", &m_BlurParams.SigmaSpatial, 0.1f, 0.1f, 50.0f);
			ImGui::DragFloat("Sigma Range", &m_BlurParams.SigmaRange, 0.01f, 0.01f, 10.0f);
		}
    }
}

// =========================================================
// Render Steps
// =========================================================

void SSFRPass::RenderParticles(const RenderContext& ctx)
{
	auto cmdList = ctx.CmdList;
	auto solver = ctx.Solver;

	auto dsvHandle = ctx.DSV;
	cmdList->OMSetRenderTargets(1, &ctx.RTV, FALSE, &ctx.DSV);

	cmdList->RSSetViewports(1, &ctx.Viewport);
	cmdList->RSSetScissorRects(1, &ctx.ScissorRect);

	cmdList->SetGraphicsRootSignature(m_RenderParticleRootSig.Get());
	cmdList->SetPipelineState(m_RenderParticlePSO.Get());

	ID3D12DescriptorHeap* heaps[] = { solver->GetSrvHeap() };
	cmdList->SetDescriptorHeaps(1, heaps);

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(RenderContext::GlobalConstants) / 4, &ctx.Globals, 0);
	cmdList->SetGraphicsRootDescriptorTable(1, solver->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart());

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

	auto dsvHandle = ctx.DSV; // Use Global DSV
	cmdList->OMSetRenderTargets(1, &m_FluidDepthRtvHandle, FALSE, &dsvHandle);

	// Viewport & Scissor
	cmdList->RSSetViewports(1, &ctx.Viewport);
	cmdList->RSSetScissorRects(1, &ctx.ScissorRect);

	// Pipeline Setup
	cmdList->SetGraphicsRootSignature(m_FluidDepthRootSig.Get());
	cmdList->SetPipelineState(m_FluidDepthPSO.Get());

	// Bind Solver Resources (Particles)
	ID3D12DescriptorHeap* heaps[] = { solver->GetSrvHeap() };
	cmdList->SetDescriptorHeaps(1, heaps);

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(RenderContext::GlobalConstants) / 4, &ctx.Globals, 0);
	cmdList->SetGraphicsRootDescriptorTable(1, solver->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart());

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

	// Bind Fluid SRV Heap
	ID3D12DescriptorHeap* heaps[] = { m_FluidSrvHeap.Get() };
	cmdList->SetDescriptorHeaps(1, heaps);

	BlurParams blurParams;
	blurParams.InvScreenWidth = 1.0f / m_Width;
	blurParams.InvScreenHeight = 1.0f / m_Height;

	// [Pass 1] Horizontal: Depth(RTV->SRV) -> Temp(SRV->RTV)
	D3D12_RESOURCE_BARRIER barriers1[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(m_FluidDepthTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_BlurTempTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
	};
	cmdList->ResourceBarrier(2, barriers1);

	cmdList->OMSetRenderTargets(1, &m_BlurTempRtvHandle, FALSE, nullptr);

	// Input: Depth (Index 0)
	auto gpuHandleDepth = m_FluidSrvHeap->GetGPUDescriptorHandleForHeapStart();
	cmdList->SetGraphicsRootDescriptorTable(1, gpuHandleDepth);

	blurParams.DirX = 1.0f; blurParams.DirY = 0.0f;
	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(BlurParams) / 4, &blurParams, 0);
	cmdList->DrawInstanced(3, 1, 0, 0);

	// [Pass 2] Vertical: Temp(RTV->SRV) -> Depth(SRV->RTV)
	D3D12_RESOURCE_BARRIER barriers2[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(m_FluidDepthTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(m_BlurTempTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	};
	cmdList->ResourceBarrier(2, barriers2);

	cmdList->OMSetRenderTargets(1, &m_FluidDepthRtvHandle, FALSE, nullptr);

	// Input: Temp (Index 1)
	UINT srvSize = ctx.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandleTemp(m_FluidSrvHeap->GetGPUDescriptorHandleForHeapStart(), 1, srvSize);
	cmdList->SetGraphicsRootDescriptorTable(1, gpuHandleTemp);

	blurParams.DirX = 0.0f; blurParams.DirY = 1.0f;
	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(BlurParams) / 4, &blurParams, 0);
	cmdList->DrawInstanced(3, 1, 0, 0);

	// Cleanup: Depth(RTV->SRV)
	D3D12_RESOURCE_BARRIER barrierEnd = CD3DX12_RESOURCE_BARRIER::Transition(
		m_FluidDepthTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);
	cmdList->ResourceBarrier(1, &barrierEnd);
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
	auto dsvHandle = ctx.DSV;
	cmdList->OMSetRenderTargets(1, &m_FluidThicknessRtvHandle, FALSE, &dsvHandle);

	cmdList->RSSetViewports(1, &ctx.Viewport);
	cmdList->RSSetScissorRects(1, &ctx.ScissorRect);

	cmdList->SetGraphicsRootSignature(m_FluidDepthRootSig.Get()); // Reusing Depth RootSig
	cmdList->SetPipelineState(m_FluidThicknessPSO.Get());

	// Bind Solver Resources
	ID3D12DescriptorHeap* heaps[] = { solver->GetSrvHeap() };
	cmdList->SetDescriptorHeaps(1, heaps);

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(RenderContext::GlobalConstants) / 4, &ctx.Globals, 0);
	cmdList->SetGraphicsRootDescriptorTable(1, solver->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart());

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

	cmdList->SetGraphicsRootSignature(m_FluidCompositeRootSig.Get());
	cmdList->SetPipelineState(m_FluidCompositePSO.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Bind Fluid Heap (Depth, Thickness)
	ID3D12DescriptorHeap* heaps[] = { m_FluidSrvHeap.Get() };
	cmdList->SetDescriptorHeaps(1, heaps);

	CompositeParams compositeParams;
	compositeParams.InvWidth = 1.0f / m_Width;
	compositeParams.InvHeight = 1.0f / m_Height;

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(CompositeParams) / 4, &compositeParams, 0);

	// Bind SRVs (Depth at 0, BlurTemp at 1, Thickness at 2)
	auto gpuHandle = m_FluidSrvHeap->GetGPUDescriptorHandleForHeapStart();
	cmdList->SetGraphicsRootDescriptorTable(1, gpuHandle);

	// Output to BackBuffer
	cmdList->OMSetRenderTargets(1, &ctx.RTV, FALSE, &ctx.DSV);

	cmdList->DrawInstanced(3, 1, 0, 0);
}

// =========================================================
// Initialization Functions
// =========================================================

void SSFRPass::CreateShaders(const RenderInitContext& ctx)
{
    auto helper = ctx.ShaderHelper;

    m_ParticleVS = helper->Compile(L"ParticleVS.hlsl", L"main", L"vs_6_0");
    m_ParticlePS = helper->Compile(L"ParticlePS.hlsl", L"main", L"ps_6_0");
    m_FluidDepthPS = helper->Compile(L"FluidDepthPS.hlsl", L"main", L"ps_6_0");
    m_FullScreenQuadVS = helper->Compile(L"FullScreenQuadVS.hlsl", L"main", L"vs_6_0");
    m_FluidSmoothPS = helper->Compile(L"FluidSmooth.hlsl", L"main", L"ps_6_0");
    m_FluidThicknessPS = helper->Compile(L"FluidThicknessPS.hlsl", L"main", L"ps_6_0");
    m_FluidCompositePS = helper->Compile(L"FluidComposite.hlsl", L"main", L"ps_6_0");
}

void SSFRPass::CreateRootSignatures(const RenderInitContext& ctx)
{
    auto device = ctx.Device;
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

    // 1. Particle / Depth / Thickness (Shared)
    {
        CD3DX12_ROOT_PARAMETER1 params[2];
        params[0].InitAsConstants(sizeof(RenderContext::GlobalConstants) / 4.0, 0, 0, D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_DESCRIPTOR_RANGE1 srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
        params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_VERTEX);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
        desc.Init_1_1(2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        Helpers::CreateRootSignature(device, desc, featureData.HighestVersion, m_FluidDepthRootSig, "FluidDepthRootSig");
    }
    m_RenderParticleRootSig = m_FluidDepthRootSig;

    // 2. Smooth
    {
        CD3DX12_DESCRIPTOR_RANGE1 srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

        CD3DX12_ROOT_PARAMETER1 params[2];
        params[0].InitAsConstants(sizeof(BlurParams) / 4, 0);
        params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC sampler = CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
        desc.Init_1_1(2, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        Helpers::CreateRootSignature(device, desc, featureData.HighestVersion, m_FluidSmoothRootSig, "FluidSmoothRootSig");
    }

    // 3. Composite
    {
        CD3DX12_DESCRIPTOR_RANGE1 srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

        CD3DX12_ROOT_PARAMETER1 params[2];
        params[0].InitAsConstants(sizeof(CompositeParams) / 4, 0);
        params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC samplers[2];
        samplers[0] = CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        samplers[1] = CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
        desc.Init_1_1(2, params, 2, samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        Helpers::CreateRootSignature(device, desc, featureData.HighestVersion, m_FluidCompositeRootSig, "FluidCompositeRootSig");
    }
}

void SSFRPass::CreatePSOs(const RenderInitContext& ctx)
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
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
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
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_FluidDepthPSO)));
    }

    // 3. Fluid Smooth
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_FluidSmoothRootSig.Get();

        psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_FullScreenQuadVS->GetBufferPointer(), m_FullScreenQuadVS->GetBufferSize());
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
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_FluidThicknessPSO)));
    }

    // 5. Composite
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_FluidCompositeRootSig.Get();

        psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_FullScreenQuadVS->GetBufferPointer(), m_FullScreenQuadVS->GetBufferSize());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_FluidCompositePS->GetBufferPointer(), m_FluidCompositePS->GetBufferSize());

        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_FluidCompositePSO)));
    }
}

void SSFRPass::CreateQuadMesh(const RenderInitContext& ctx)
{
    auto device = ctx.Device;
    auto cmdList = ctx.CmdList;

    Vertex quadVertices[] = {
        { { -0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f } },
        { {  0.5f,  0.5f, 0.0f }, { 1.0f, 0.0f } },
        { { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f } },
        { {  0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f } },
    };

    uint16_t quadIndices[] = { 0, 1, 2, 1, 3, 2 };

    const UINT vbByteSize = sizeof(quadVertices);
    const UINT ibByteSize = sizeof(quadIndices);

    m_QuadVB = Helpers::CreateDefaultBuffer(device, cmdList, quadVertices, vbByteSize, m_QuadVBUpload);
    m_QuadIB = Helpers::CreateDefaultBuffer(device, cmdList, quadIndices, ibByteSize, m_QuadIBUpload);

    m_QuadVBView.BufferLocation = m_QuadVB->GetGPUVirtualAddress();
    m_QuadVBView.StrideInBytes = sizeof(Vertex);
    m_QuadVBView.SizeInBytes = vbByteSize;

    m_QuadIBView.BufferLocation = m_QuadIB->GetGPUVirtualAddress();
    m_QuadIBView.Format = DXGI_FORMAT_R16_UINT;
    m_QuadIBView.SizeInBytes = ibByteSize;
}

void SSFRPass::CreateFluidResources(const RenderInitContext& ctx)
{
	auto device = ctx.Device;

	// 1. Create Heaps
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
		rtvDesc.NumDescriptors = 3; // Depth, BlurTemp, Thickness
		rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_FluidRtvHeap)));

		D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
		srvDesc.NumDescriptors = 3;
		srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_FluidSrvHeap)));
	}

	// 2. Create Textures (Depth, Blur, Thickness)
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

	// 3. Create Views (RTV, SRV)
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_FluidRtvHeap->GetCPUDescriptorHandleForHeapStart());
	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_FluidSrvHeap->GetCPUDescriptorHandleForHeapStart());
	UINT rtvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	UINT srvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Helper Lambda
	auto CreateViews = [&](ID3D12Resource* tex, CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvOut, CD3DX12_CPU_DESCRIPTOR_HANDLE& srvOut) {
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		device->CreateRenderTargetView(tex, &rtvDesc, rtvHandle);
		rtvOut = rtvHandle;
		rtvHandle.Offset(1, rtvInc);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(tex, &srvDesc, srvHandle);
		srvOut = srvHandle;
		srvHandle.Offset(1, srvInc);
		};

	CreateViews(m_FluidDepthTexture.Get(), m_FluidDepthRtvHandle, m_FluidDepthSrvHandle);
	CreateViews(m_BlurTempTexture.Get(), m_BlurTempRtvHandle, m_BlurTempSrvHandle);
	CreateViews(m_FluidThicknessTexture.Get(), m_FluidThicknessRtvHandle, m_FluidThicknessSrvHandle);
}