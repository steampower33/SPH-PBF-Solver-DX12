#include "ShaderHelper.h"
#include "GraphicsCore.h"
#include "Vertex.h"
#include "DescriptorHeapManager.h"

#include "SkyboxPass.h"

void SkyboxPass::Render(const RenderContext& ctx)
{
	auto cmdList = ctx.CmdList;

	D3D12_RESOURCE_BARRIER barrierStart = CD3DX12_RESOURCE_BARRIER::Transition(
		ctx.SceneDepthTex.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_DEPTH_WRITE
	);
	cmdList->ResourceBarrier(1, &barrierStart);

	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	cmdList->ClearRenderTargetView(ctx.SceneRTVHandle, clearColor, 0, nullptr);
	cmdList->ClearDepthStencilView(ctx.SceneDSVHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &ctx.SceneRTVHandle, FALSE, &ctx.SceneDSVHandle);

	cmdList->RSSetViewports(1, &ctx.Viewport);
	cmdList->RSSetScissorRects(1, &ctx.ScissorRect);

	cmdList->SetGraphicsRootSignature(m_SkyboxRootSig.Get());
	cmdList->SetPipelineState(m_SkyboxPSO.Get());


	SM::Matrix viewNoTranslation = ctx.Globals.View;
	viewNoTranslation._14 = 0.0f;
	viewNoTranslation._24 = 0.0f;
	viewNoTranslation._34 = 0.0f;

	m_Params.View = viewNoTranslation;
	m_Params.Proj = ctx.Globals.Proj;

	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(Params) / 4, &m_Params, 0);
	cmdList->SetGraphicsRootDescriptorTable(1, m_EnvCubeSRVHandleGpu);

	cmdList->IASetVertexBuffers(0, 1, &m_VBView);
	cmdList->IASetIndexBuffer(&m_IBView);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);
}

void SkyboxPass::RenderDepthOnly(const RenderContext& ctx)
{
}

void SkyboxPass::OnGui(RenderContext& ctx)
{
}

void SkyboxPass::CreateShaders(RenderContext& ctx)
{
	m_SkyboxVS = ctx.ShaderHelper->Compile(L"./Shaders/Rendering/", L"SkyboxVS.hlsl", L"main", L"vs_6_0");
	m_SkyboxPS = ctx.ShaderHelper->Compile(L"./Shaders/Rendering/", L"SkyboxPS.hlsl", L"main", L"ps_6_0");
}

void SkyboxPass::CreateRootSignatures(RenderContext& ctx)
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
		srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
		params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC samplers[1];
		samplers[0] = CD3DX12_STATIC_SAMPLER_DESC(
			0,
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP
		);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
		desc.Init_1_1(_countof(params), params, _countof(samplers), samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Helpers::CreateRootSignature(device, desc, featureData.HighestVersion, m_SkyboxRootSig, "m_SkyboxRootSig");
	}
}

void SkyboxPass::CreatePSOs(RenderContext& ctx)
{
	auto device = ctx.Device;

	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
		psoDesc.pRootSignature = m_SkyboxRootSig.Get();

		psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_SkyboxVS->GetBufferPointer(), m_SkyboxVS->GetBufferSize());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_SkyboxPS->GetBufferPointer(), m_SkyboxPS->GetBufferSize());

		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_SkyboxPSO)));
		Helpers::SetDebugName(m_SkyboxPSO.Get(), "m_SkyboxPSO");
	}
}

void SkyboxPass::CreateResources(RenderContext& ctx, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps)
{
	auto device = ctx.Device;
	auto cmdList = ctx.CmdList;

	struct SimpleVertex {
		SM::Vector3 Pos;
	};

	SimpleVertex cubeVerts[] = {
		{ {-1.0f, -1.0f, -1.0f} }, { {-1.0f, +1.0f, -1.0f} },
		{ {+1.0f, +1.0f, -1.0f} }, { {+1.0f, -1.0f, -1.0f} },
		{ {-1.0f, -1.0f, +1.0f} }, { {-1.0f, +1.0f, +1.0f} },
		{ {+1.0f, +1.0f, +1.0f} }, { {+1.0f, -1.0f, +1.0f} }
	};

	uint16_t indices[] = {
		0, 1, 2, 0, 2, 3, // Front
		4, 6, 5, 4, 7, 6, // Back
		4, 5, 1, 4, 1, 0, // Left
		3, 2, 6, 3, 6, 7, // Right
		1, 5, 6, 1, 6, 2, // Top
		4, 0, 3, 4, 3, 7  // Bottom
	};

	const UINT vbByteSize = sizeof(cubeVerts);
	const UINT ibByteSize = sizeof(indices);

	m_VB = Helpers::CreateDefaultBuffer(device, cmdList, cubeVerts, vbByteSize, uploadHeaps);
	m_IB = Helpers::CreateDefaultBuffer(device, cmdList, indices, ibByteSize, uploadHeaps);

	m_VBView.BufferLocation = m_VB->GetGPUVirtualAddress();
	m_VBView.StrideInBytes = sizeof(SimpleVertex);
	m_VBView.SizeInBytes = vbByteSize;

	m_IBView.BufferLocation = m_IB->GetGPUVirtualAddress();
	m_IBView.Format = DXGI_FORMAT_R16_UINT;
	m_IBView.SizeInBytes = ibByteSize;

	ID3D12CommandQueue* queue = ctx.Queue;

	auto LoadTexture = [&](CD3DX12_CPU_DESCRIPTOR_HANDLE& cpu, CD3DX12_GPU_DESCRIPTOR_HANDLE& gpu, ComPtr<ID3D12Resource>& res, std::wstring path, D3D12_SRV_DIMENSION dim = D3D12_SRV_DIMENSION_TEXTURECUBE) {
		cpu = ctx.HeapManager->AllocSRV(&gpu);
		res = LoadTextureAndCreateSRV(
			ctx.Device,
			queue,
			path,
			cpu,
			dim
		);
		if (res)
			OutputDebugStringA("Env CubeMap Loaded Successfully!\n");
	};

	LoadTexture(m_EnvCubeSRVHandleCpu, m_EnvCubeSRVHandleGpu, m_EnvCubeMap, L"./Assets/Skybox/DaySkyEnvHDR.dds");
	LoadTexture(m_SpecularCubeSRVHandleCpu, m_SpecularCubeSRVHandleGpu, m_SpecularCubeMap, L"./Assets/Skybox/DaySkySpecularHDR.dds");
	LoadTexture(m_DiffuseCubeSRVHandleCpu, m_DiffuseCubeSRVHandleGpu, m_DiffuseCubeMap, L"./Assets/Skybox/DaySkyDiffuseHDR.dds");
	LoadTexture(m_BrdfSRVHandleCpu, m_BrdfSRVHandleGpu, m_BrdfMap, L"./Assets/Skybox/DaySkyBrdf.dds", D3D12_SRV_DIMENSION_TEXTURE2D);

	ctx.SkyboxSRVHandleGpu = m_EnvCubeSRVHandleGpu;
}

ComPtr<ID3D12Resource> SkyboxPass::LoadTextureAndCreateSRV(
	ID3D12Device* device,
	ID3D12CommandQueue* cmdQueue,
	std::wstring filepath,
	D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle,
	D3D12_SRV_DIMENSION dim
)
{
	ComPtr<ID3D12Resource> texture;

	DX::ResourceUploadBatch resourceUpload(device);
	resourceUpload.Begin();

	HRESULT hr = CreateDDSTextureFromFile(
		device,
		resourceUpload,
		filepath.c_str(),
		&texture
	);

	if (FAILED(hr))
	{
		OutputDebugStringA("Failed to load Skybox Texture!\n");
		return nullptr;
	}

	auto uploadFuture = resourceUpload.End(cmdQueue);
	uploadFuture.wait();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = texture->GetDesc().Format;
	srvDesc.ViewDimension = dim;
	srvDesc.TextureCube.MipLevels = -1;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

	device->CreateShaderResourceView(texture.Get(), &srvDesc, srvCpuHandle);

	return texture;
}