#include "Vertex.h"

#include "Renderer.h"

void Renderer::Render(ID3D12GraphicsCommandList* cmdList, const SphSolver* solver, const SM::Matrix& view, const SM::Matrix& proj)
{
    cmdList->SetGraphicsRootSignature(m_RenderParticleRootSig.Get());

    ID3D12DescriptorHeap* heaps[] = { solver->GetSrvHeap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    m_Params.View = view.Transpose();
    m_Params.Proj = proj.Transpose();

    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(Params) / 4, &m_Params, 0);

    cmdList->SetGraphicsRootDescriptorTable(1, solver->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart());

    cmdList->SetPipelineState(m_RenderParticlePSO.Get());

    cmdList->IASetVertexBuffers(0, 1, &m_QuadVBView);
    cmdList->IASetIndexBuffer(&m_QuadIBView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmdList->DrawIndexedInstanced(6, solver->GetNumParticles(), 0, 0, 0);
}

void Renderer::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper)
{
    m_pDevice = device;

    // Initialize Resources
    InitShaders(shaderHelper);
    InitRootSignatures();
    InitPSOs();
    InitQuadMesh(cmdList);
}

void Renderer::InitShaders(ShaderHelper* helper)
{
    // Compile Vertex and Pixel shaders using the helper
    m_ParticleVS = helper->Compile(L"ParticleVS.hlsl", L"main", L"vs_6_0");
    m_ParticlePS = helper->Compile(L"ParticlePS.hlsl", L"main", L"ps_6_0");
}

void Renderer::InitRootSignatures()
{
    CreateRenderParticleRootSignature();
}

void Renderer::InitPSOs()
{
    CreateRenderParticlePSO();
}

void Renderer::CreateRenderParticleRootSignature()
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    CD3DX12_ROOT_PARAMETER1 rootParameters[2];

    // Parameter 0: 33 Constants (View Matrix + Proj Matrix + Radius)
    rootParameters[0].InitAsConstants(33, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // Parameter 1: Descriptor Table for SRV (StructuredBuffer)
    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    rootParameters[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_VERTEX);

    // Root Signature Description
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(
        _countof(rootParameters),
        rootParameters,
        0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    // Serialize and Create
    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DX12SerializeVersionedRootSignature(
        &rootSignatureDesc,
        featureData.HighestVersion,
        &signatureBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        ThrowIfFailed(hr);
    }

    ThrowIfFailed(m_pDevice->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&m_RenderParticleRootSig)
    ));

    m_RenderParticleRootSig->SetName(L"RenderParticleRootSignature_NoTexture");
}

void Renderer::CreateRenderParticlePSO()
{
    // Define Input Layout for the Quad Mesh
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        // SemanticName, Index, Format, Slot, AlignedByteOffset, Classification, InstanceDataStepRate
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // 1. Define PSO descriptor
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    // 2. Bind Input Layout and Root Signature
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_RenderParticleRootSig.Get();

    // Attach Shaders
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(
        m_ParticleVS->GetBufferPointer(),
        m_ParticleVS->GetBufferSize()
    );

    psoDesc.PS = CD3DX12_SHADER_BYTECODE(
        m_ParticlePS->GetBufferPointer(),
        m_ParticlePS->GetBufferSize()
    );

    // 3. Configure State (Rasterizer, Blend, Depth)
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    // 4. Set Output Formats
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // 5. Topology Settings (Triangle List for Quads)
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    // 6. Create the PSO
    ThrowIfFailed(m_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_RenderParticlePSO)));
}

void Renderer::InitQuadMesh(ID3D12GraphicsCommandList* cmdList)
{
    // Define Quad Vertices (Local Space)
    Vertex quadVertices[] = {
        { { -0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f } }, // Top-Left
        { {  0.5f,  0.5f, 0.0f }, { 1.0f, 0.0f } }, // Top-Right
        { { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f } }, // Bottom-Left
        { {  0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f } }, // Bottom-Right
    };

    // Define Indices (Two Triangles)
    uint16_t quadIndices[] = {
        0, 1, 2,
        1, 3, 2
    };

    const UINT vbByteSize = sizeof(quadVertices);
    const UINT ibByteSize = sizeof(quadIndices);

    // Create and Upload Vertex Buffer
    m_QuadVB = Helpers::CreateDefaultBuffer(
        m_pDevice,
        cmdList,
        quadVertices,
        vbByteSize,
        m_QuadVBUpload // Keep upload buffer alive until execution
    );

    // Initialize Vertex Buffer View
    m_QuadVBView.BufferLocation = m_QuadVB->GetGPUVirtualAddress();
    m_QuadVBView.StrideInBytes = sizeof(Vertex);
    m_QuadVBView.SizeInBytes = vbByteSize;

    // Create and Upload Index Buffer
    m_QuadIB = Helpers::CreateDefaultBuffer(
        m_pDevice,
        cmdList,
        quadIndices,
        ibByteSize,
        m_QuadIBUpload
    );

    // Initialize Index Buffer View
    m_QuadIBView.BufferLocation = m_QuadIB->GetGPUVirtualAddress();
    m_QuadIBView.Format = DXGI_FORMAT_R16_UINT;
    m_QuadIBView.SizeInBytes = ibByteSize;
}