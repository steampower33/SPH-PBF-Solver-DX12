#include "ShaderHelper.h"
#include "GraphicsCore.h"
#include "Vertex.h"

#include "PlanePass.h"

void PlanePass::Render(const RenderContext& ctx)
{
	auto cmdList = ctx.CmdList;
	auto device = ctx.Device;

	D3D12_RESOURCE_BARRIER barrierStart = CD3DX12_RESOURCE_BARRIER::Transition(
		ctx.SceneDepthTex,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_DEPTH_WRITE
	);
	cmdList->ResourceBarrier(1, &barrierStart);

	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	cmdList->ClearRenderTargetView(ctx.SceneRTV, clearColor, 0, nullptr);
	cmdList->ClearDepthStencilView(ctx.SceneDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &ctx.SceneRTV, FALSE, &ctx.SceneDSV);

	cmdList->RSSetViewports(1, &ctx.Viewport);
	cmdList->RSSetScissorRects(1, &ctx.ScissorRect);

	UINT frameIndex = ctx.FrameIndex;
	UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE srcHandle(ctx.ShadowSRVHeap->GetCPUDescriptorHandleForHeapStart());
	CD3DX12_CPU_DESCRIPTOR_HANDLE destHandle(m_PlaneSRVHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, descriptorSize);

	device->CopyDescriptorsSimple(1, destHandle, srcHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	cmdList->SetGraphicsRootSignature(m_PlaneRootSig.Get());
	cmdList->SetPipelineState(m_PlanePSO.Get());

	m_Params.View = ctx.Globals.View;
	m_Params.Proj = ctx.Globals.Proj;
	m_Params.ShadowTransform = ctx.ShadowTransform;
	m_Params.LightPos = ctx.LightPos;
	m_Params.LightDir = ctx.LightDir;
	m_Params.ShadowIntensity = ctx.ShadowIntensity;

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(Params) / 4, &m_Params, 0);

	ID3D12DescriptorHeap* heaps[] = { m_PlaneSRVHeap.Get()};
	cmdList->SetDescriptorHeaps(1, heaps);

	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_PlaneSRVHeap->GetGPUDescriptorHandleForHeapStart(), frameIndex, descriptorSize);
	cmdList->SetGraphicsRootDescriptorTable(1, gpuHandle);

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->IASetVertexBuffers(0, 1, &m_QuadVBView);
	cmdList->IASetIndexBuffer(&m_QuadIBView);
	cmdList->DrawIndexedInstanced(6, 1, 0, 0, 0);

	D3D12_RESOURCE_BARRIER barrierEnd = CD3DX12_RESOURCE_BARRIER::Transition(
		ctx.SceneDepthTex,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);
	cmdList->ResourceBarrier(1, &barrierEnd);
}

void PlanePass::RenderDepthOnly(const RenderContext& ctx)
{
}

void PlanePass::CreateShaders(const RenderInitContext& ctx)
{
	m_PlaneVS = ctx.ShaderHelper->Compile(L"./Shaders/Rendering/", L"PlaneVS.hlsl", L"main", L"vs_6_0");
	m_PlanePS = ctx.ShaderHelper->Compile(L"./Shaders/Rendering/", L"PlanePS.hlsl", L"main", L"ps_6_0");
}

void PlanePass::CreateRootSignatures(const RenderInitContext& ctx)
{
	auto device = ctx.Device;

	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

	{
		CD3DX12_ROOT_PARAMETER1 params[2];
		params[0].InitAsConstants(sizeof(Params) / 4.0, 0, 0, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_DESCRIPTOR_RANGE1 srvRange;
		srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
		params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC samplers[1];
		samplers[0] = CD3DX12_STATIC_SAMPLER_DESC(
			0,
			D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			0.0f,
			16,
			D3D12_COMPARISON_FUNC_LESS_EQUAL,
			D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE
		);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
		desc.Init_1_1(__crt_countof(params), params, __crt_countof(samplers), samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Helpers::CreateRootSignature(device, desc, featureData.HighestVersion, m_PlaneRootSig, "m_PlaneRootSig");
	}
}

void PlanePass::CreatePSOs(const RenderInitContext& ctx)
{
	auto device = ctx.Device;

	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
		psoDesc.pRootSignature = m_PlaneRootSig.Get();

		psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_PlaneVS->GetBufferPointer(), m_PlaneVS->GetBufferSize());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_PlanePS->GetBufferPointer(), m_PlanePS->GetBufferSize());

		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PlanePSO)));
		Helpers::SetDebugName(m_PlanePSO.Get(), "m_PlanePSO");
	}
}

void PlanePass::CreateResources(const RenderInitContext& ctx, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps)
{
	auto device = ctx.Device;
	auto cmdList = ctx.CmdList;

	// Quad Mesh
	{
		Vertex quadVertices[] = {
			{ { -1.0f,  0.0f, -1.0f }, { 0.0f, 0.0f } },
			{ {  1.0f,  0.0f, -1.0f }, { 1.0f, 0.0f } },
			{ {  1.0f,  0.0f,  1.0f }, { 1.0f, 1.0f } },
			{ { -1.0f,  0.0f,  1.0f }, { 0.0f, 1.0f } },
		};

		uint16_t quadIndices[] = { 0, 1, 2, 0, 2, 3 };

		const UINT vbByteSize = sizeof(quadVertices);
		const UINT ibByteSize = sizeof(quadIndices);

		m_QuadVB = Helpers::CreateDefaultBuffer(device, cmdList, quadVertices, vbByteSize, uploadHeaps);
		m_QuadIB = Helpers::CreateDefaultBuffer(device, cmdList, quadIndices, ibByteSize, uploadHeaps);

		m_QuadVBView.BufferLocation = m_QuadVB->GetGPUVirtualAddress();
		m_QuadVBView.StrideInBytes = sizeof(Vertex);
		m_QuadVBView.SizeInBytes = vbByteSize;

		m_QuadIBView.BufferLocation = m_QuadIB->GetGPUVirtualAddress();
		m_QuadIBView.Format = DXGI_FORMAT_R16_UINT;
		m_QuadIBView.SizeInBytes = ibByteSize;
	}

	// Srv Heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = GraphicsCore::FrameCount;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_PlaneSRVHeap)));
	}
}

void PlanePass::OnGui(RenderContext& ctx)
{
	if (ImGui::CollapsingHeader("Plane", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::DragFloat("Scale", &m_Params.TileScale, 0.1f, 0.0f, 100.0f);
		ImGui::DragFloat("TileCount", &m_Params.TileCount, 1.0f, 0.0f, 100.0f);
	}
}