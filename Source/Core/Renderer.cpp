#include "Vertex.h"

#include "Renderer.h"

void Renderer::Update(const SM::Matrix& view, const SM::Matrix& proj)
{
    m_Params.View = view.Transpose();
    m_Params.Proj = proj.Transpose();
}

void Renderer::RenderParticles(ID3D12GraphicsCommandList* cmdList, const SphSolver* solver)
{
    cmdList->SetGraphicsRootSignature(m_RenderParticleRootSig.Get());

    ID3D12DescriptorHeap* heaps[] = { solver->GetSrvHeap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(Params) / 4, &m_Params, 0);

    cmdList->SetGraphicsRootDescriptorTable(1, solver->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart());

    cmdList->SetPipelineState(m_RenderParticlePSO.Get());

    cmdList->IASetVertexBuffers(0, 1, &m_QuadVBView);
    cmdList->IASetIndexBuffer(&m_QuadIBView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmdList->DrawIndexedInstanced(6, solver->GetNumParticles(), 0, 0, 0);
}

void Renderer::RenderFluidDepth(ID3D12GraphicsCommandList* cmdList, const SphSolver* solver)
{
    // m_FluidDepthTexture : SRV -> RTV
    D3D12_RESOURCE_BARRIER barrierStart = CD3DX12_RESOURCE_BARRIER::Transition(
        m_FluidDepthTexture.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, // Before
        D3D12_RESOURCE_STATE_RENDER_TARGET          // After
    );
    cmdList->ResourceBarrier(1, &barrierStart);

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_pGraphicsCore->GetDepthStencilView();
    cmdList->OMSetRenderTargets(1, &m_FluidDepthRtvHandle, FALSE, &dsvHandle);

    const float clearColor[] = { 1e9f, 0.0f, 0.0f, 0.0f };
    cmdList->ClearRenderTargetView(m_FluidDepthRtvHandle, clearColor, 0, nullptr);

    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_Width, (float)m_Height, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)m_Width, (LONG)m_Height };
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissorRect);

    cmdList->SetGraphicsRootSignature(m_FluidDepthRootSig.Get());
    cmdList->SetPipelineState(m_FluidDepthPSO.Get());

    ID3D12DescriptorHeap* heaps[] = { solver->GetSrvHeap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(Params) / 4, &m_Params, 0);
    cmdList->SetGraphicsRootDescriptorTable(1, solver->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart());

    cmdList->IASetVertexBuffers(0, 1, &m_QuadVBView);
    cmdList->IASetIndexBuffer(&m_QuadIBView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmdList->DrawIndexedInstanced(6, solver->GetNumParticles(), 0, 0, 0);
}

void Renderer::RenderFluidSmooth(ID3D12GraphicsCommandList* cmdList)
{
    // Common Setup
    cmdList->SetGraphicsRootSignature(m_FluidSmoothRootSig.Get());
    cmdList->SetPipelineState(m_FluidSmoothPSO.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set Descriptor Heaps (Fluid SRV Heap)
    ID3D12DescriptorHeap* heaps[] = { m_FluidSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    m_BlurParams.InvScreenWidth = 1.0f / m_Width;
    m_BlurParams.InvScreenHeight = 1.0f / m_Height;

    // [Pass 1] Horizontal Blur: Depth(SRV) -> Temp(RTV)
    D3D12_RESOURCE_BARRIER barriers1[] = {
        // Depth: RTV -> SRV
        CD3DX12_RESOURCE_BARRIER::Transition(
            m_FluidDepthTexture.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        ),
            // Temp: SRV -> RTV
            CD3DX12_RESOURCE_BARRIER::Transition(
                m_BlurTempTexture.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET
            )
    };
    cmdList->ResourceBarrier(2, barriers1);

    cmdList->OMSetRenderTargets(1, &m_BlurTempRtvHandle, FALSE, nullptr);

    // Depth 읽기 (Heap Index 0)
    auto gpuHandleDepth = m_FluidSrvHeap->GetGPUDescriptorHandleForHeapStart();
    cmdList->SetGraphicsRootDescriptorTable(1, gpuHandleDepth);

    // Draw
    m_BlurParams.DirX = 1.0f; m_BlurParams.DirY = 0.0f;
    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(BlurParams) / 4.0f, &m_BlurParams, 0);
    cmdList->DrawInstanced(3, 1, 0, 0);

    // [Pass 2] Vertical Blur: Temp(SRV) -> Depth(RTV)
    D3D12_RESOURCE_BARRIER barriers2[] = {
        // Depth: SRV -> RTV
        CD3DX12_RESOURCE_BARRIER::Transition(
            m_FluidDepthTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        ),
            // Temp: RTV -> SRV
            CD3DX12_RESOURCE_BARRIER::Transition(
                m_BlurTempTexture.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
            )
    };
    cmdList->ResourceBarrier(2, barriers2);

    cmdList->OMSetRenderTargets(1, &m_FluidDepthRtvHandle, FALSE, nullptr);

    UINT srvSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandleTemp(m_FluidSrvHeap->GetGPUDescriptorHandleForHeapStart(), 1, srvSize);
    cmdList->SetGraphicsRootDescriptorTable(1, gpuHandleTemp);

    m_BlurParams.DirX = 0.0f; m_BlurParams.DirY = 1.0f;
    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(BlurParams) / 4.0f, &m_BlurParams, 0);
    cmdList->DrawInstanced(3, 1, 0, 0);

    // [Cleanup] Depth(RTV) -> Depth(SRV)
    D3D12_RESOURCE_BARRIER barrierEnd = CD3DX12_RESOURCE_BARRIER::Transition(
        m_FluidDepthTexture.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    cmdList->ResourceBarrier(1, &barrierEnd);
}

void Renderer::RenderFluidThickness(ID3D12GraphicsCommandList* cmdList, const SphSolver* solver)
{
    {
        // m_FluidThicknessTexture : SRV -> RTV
        D3D12_RESOURCE_BARRIER barrierStart = CD3DX12_RESOURCE_BARRIER::Transition(
            m_FluidThicknessTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, // Before
            D3D12_RESOURCE_STATE_RENDER_TARGET          // After
        );
        cmdList->ResourceBarrier(1, &barrierStart);
    }

    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    cmdList->ClearRenderTargetView(m_FluidThicknessRtvHandle, clearColor, 0, nullptr);

    auto dsvHandle = m_pGraphicsCore->GetDepthStencilView();
    cmdList->OMSetRenderTargets(1, &m_FluidThicknessRtvHandle, FALSE, &dsvHandle);

    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_Width, (float)m_Height, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)m_Width, (LONG)m_Height };
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissorRect);

    cmdList->SetGraphicsRootSignature(m_FluidDepthRootSig.Get());
    cmdList->SetPipelineState(m_FluidThicknessPSO.Get());

    ID3D12DescriptorHeap* heaps[] = { solver->GetSrvHeap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(Params) / 4, &m_Params, 0);
    cmdList->SetGraphicsRootDescriptorTable(1, solver->GetSrvHeap()->GetGPUDescriptorHandleForHeapStart());

    cmdList->IASetVertexBuffers(0, 1, &m_QuadVBView);
    cmdList->IASetIndexBuffer(&m_QuadIBView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmdList->DrawIndexedInstanced(6, solver->GetNumParticles(), 0, 0, 0);

    {
        // m_FluidThicknessTexture : RTV -> SRV
        D3D12_RESOURCE_BARRIER barrierStart = CD3DX12_RESOURCE_BARRIER::Transition(
            m_FluidThicknessTexture.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, // Before
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE// After
        );
        cmdList->ResourceBarrier(1, &barrierStart);
    }
}

void Renderer::RenderFluidComposite(ID3D12GraphicsCommandList* cmdList)
{
    cmdList->SetGraphicsRootSignature(m_FluidCompositeRootSig.Get());
    cmdList->SetPipelineState(m_FluidCompositePSO.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D12DescriptorHeap* heaps[] = { m_FluidSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    m_CompositeParams.InvWidth = 1.0f / m_Width;
    m_CompositeParams.InvHeight = 1.0f / m_Height;

    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(CompositeParams) / 4, &m_CompositeParams, 0);

    auto gpuHandleDepth = m_FluidSrvHeap->GetGPUDescriptorHandleForHeapStart();
    cmdList->SetGraphicsRootDescriptorTable(1, gpuHandleDepth);

    auto rtvHandle = m_pGraphicsCore->GetCurrentBackBufferRTV();
    auto dsvHandle = m_pGraphicsCore->GetDepthStencilView();

    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    cmdList->DrawInstanced(3, 1, 0, 0);
}

void Renderer::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, float width, float height, GraphicsCore* graphicsCore)
{
    m_pDevice = device;
    m_Width = (UINT)width;
    m_Height = (UINT)height;
    m_pGraphicsCore = graphicsCore;

    // Initialize Resources
    CreateShaders(shaderHelper);
    CreateRootSignatures();
    CreatePSOs();
    CreateQuadMesh(cmdList);
    CreateFluidDepthResources(device);
}

void Renderer::CreateShaders(ShaderHelper* helper)
{
    // Compile Vertex and Pixel shaders using the helper
    m_ParticleVS = helper->Compile(L"ParticleVS.hlsl", L"main", L"vs_6_0");
    m_ParticlePS = helper->Compile(L"ParticlePS.hlsl", L"main", L"ps_6_0");

    m_FluidDepthPS = helper->Compile(L"FluidDepthPS.hlsl", L"main", L"ps_6_0");

    m_FullScreenQuadVS = helper->Compile(L"FullScreenQuadVS.hlsl", L"main", L"vs_6_0");
    m_FluidSmoothPS = helper->Compile(L"FluidSmooth.hlsl", L"main", L"ps_6_0");

    m_FluidThicknessPS = helper->Compile(L"FluidThicknessPS.hlsl", L"main", L"ps_6_0");

    m_FluidCompositePS = helper->Compile(L"FluidComposite.hlsl", L"main", L"ps_6_0");
}

void Renderer::CreateRootSignatures()
{
    m_FeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &m_FeatureData, sizeof(m_FeatureData))))
    {
        m_FeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    CreateRenderParticleRootSignature();
    CreateFluidDepthRootSignature();
    CreateFluidSmoothRootSignature();
    CreateFluidCompositeRootSignature();
}

void Renderer::CreatePSOs()
{
    CreateRenderParticlePSO();
    CreateFluidDepthPSO();
    CreateFluidSmoothPSO();
    CreateFluidThicknessPSO();
    CreateFluidCompositePSO();
}

void Renderer::CreateRenderParticleRootSignature()
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE m_FeatureData = {};
    m_FeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &m_FeatureData, sizeof(m_FeatureData))))
    {
        m_FeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
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
        m_FeatureData.HighestVersion,
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

    Helpers::SetDebugName(m_RenderParticleRootSig.Get(), "RenderParticleRootSignature");
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
    Helpers::SetDebugName(m_RenderParticlePSO.Get(), "m_RenderParticlePSO");

}

void Renderer::CreateQuadMesh(ID3D12GraphicsCommandList* cmdList)
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
    Helpers::SetDebugName(m_QuadVBUpload.Get(), "m_QuadVBUpload");

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
    Helpers::SetDebugName(m_QuadIBUpload.Get(), "m_QuadIBUpload");

    // Initialize Index Buffer View
    m_QuadIBView.BufferLocation = m_QuadIB->GetGPUVirtualAddress();
    m_QuadIBView.Format = DXGI_FORMAT_R16_UINT;
    m_QuadIBView.SizeInBytes = ibByteSize;
}

void Renderer::CreateFluidDepthResources(ID3D12Device* device)
{
    // Heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = 3;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtvHeapDesc.NodeMask = 0;

        ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_FluidRtvHeap)));
        Helpers::SetDebugName(m_FluidRtvHeap.Get(), "m_FluidRtvHeap");

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 3;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        srvHeapDesc.NodeMask = 0;

        ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_FluidSrvHeap)));
        Helpers::SetDebugName(m_FluidSrvHeap.Get(), "m_FluidSrvHeap");
    }

    // Texture
    {
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Alignment = 0;
        texDesc.Width = m_Width;
        texDesc.Height = m_Height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R32_FLOAT;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = DXGI_FORMAT_R32_FLOAT;
        clearVal.Color[0] = 1e9f;

        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearVal,
            IID_PPV_ARGS(&m_FluidDepthTexture)
        ));
        Helpers::SetDebugName(m_FluidDepthTexture.Get(), "m_FluidDepthTexture");

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearVal,
            IID_PPV_ARGS(&m_BlurTempTexture)
        ));
        Helpers::SetDebugName(m_BlurTempTexture.Get(), "m_BlurTempTexture");

        clearVal.Color[0] = 0.0f;
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearVal,
            IID_PPV_ARGS(&m_FluidThicknessTexture)
        ));
        Helpers::SetDebugName(m_FluidThicknessTexture.Get(), "m_FluidThicknessTexture");
    }

    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_FluidRtvHeap->GetCPUDescriptorHandleForHeapStart());
        UINT incSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.Texture2D.PlaneSlice = 0;

        m_FluidDepthRtvHandle = handle;
        device->CreateRenderTargetView(m_FluidDepthTexture.Get(), &rtvDesc, m_FluidDepthRtvHandle);

        handle.Offset(1, incSize);
        m_BlurTempRtvHandle = handle;
        device->CreateRenderTargetView(m_BlurTempTexture.Get(), &rtvDesc, m_BlurTempRtvHandle);

        handle.Offset(1, incSize);
        m_FluidThicknessRtvHandle = handle;
        device->CreateRenderTargetView(m_FluidThicknessTexture.Get(), &rtvDesc, m_FluidThicknessRtvHandle);
    }

    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_FluidSrvHeap->GetCPUDescriptorHandleForHeapStart());
        UINT incSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_SHADER_RESOURCE_VIEW_DESC srcDesc = {};
        srcDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srcDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srcDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srcDesc.Texture2D.MipLevels = 1;
        srcDesc.Texture2D.MostDetailedMip = 0;

        m_FluidDepthSrvHandle = handle;
        device->CreateShaderResourceView(m_FluidDepthTexture.Get(), &srcDesc, m_FluidDepthSrvHandle);

        handle.Offset(1, incSize);
        m_BlurTempSrvHandle = handle;
        device->CreateShaderResourceView(m_BlurTempTexture.Get(), &srcDesc, m_BlurTempSrvHandle);

        handle.Offset(1, incSize);
        m_FluidThicknessSrvHandle = handle;
        device->CreateShaderResourceView(m_FluidThicknessTexture.Get(), &srcDesc, m_FluidThicknessSrvHandle);
    }
}

void Renderer::CreateFluidDepthRootSignature()
{
    CD3DX12_ROOT_PARAMETER1 rootParameters[2];

    rootParameters[0].InitAsConstants(sizeof(Params) / 4.0, 0, 0, D3D12_SHADER_VISIBILITY_ALL);

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
        m_FeatureData.HighestVersion,
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
        IID_PPV_ARGS(&m_FluidDepthRootSig)
    ));

    Helpers::SetDebugName(m_FluidDepthRootSig.Get(), "m_FluidDepthRootSig");
}

void Renderer::CreateFluidDepthPSO()
{
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };

    psoDesc.pRootSignature = m_FluidDepthRootSig.Get();

    psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_ParticleVS->GetBufferPointer(), m_ParticleVS->GetBufferSize());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_FluidDepthPS->GetBufferPointer(), m_FluidDepthPS->GetBufferSize());

    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState = depthStencilDesc;

    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;

    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(m_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_FluidDepthPSO)));
    Helpers::SetDebugName(m_FluidDepthPSO.Get(), "m_FluidDepthPSO");
}

void Renderer::CreateFluidSmoothRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 (Texture)

    CD3DX12_ROOT_PARAMETER1 params[2];
    params[0].InitAsConstants(sizeof(BlurParams) / 4, 0); // b0 (BlurParams: InvSize, Dir)
    params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    // Sampler (Point Clamp)
    D3D12_STATIC_SAMPLER_DESC sampler = CD3DX12_STATIC_SAMPLER_DESC(0);
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
    desc.Init_1_1(2, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // Serialize and Create
    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DX12SerializeVersionedRootSignature(
        &desc,
        m_FeatureData.HighestVersion,
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
        IID_PPV_ARGS(&m_FluidSmoothRootSig)
    ));

    Helpers::SetDebugName(m_FluidSmoothRootSig.Get(), "m_FluidSmoothRootSig");
}

void Renderer::CreateFluidSmoothPSO() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_FluidSmoothRootSig.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_FullScreenQuadVS->GetBufferPointer(), m_FullScreenQuadVS->GetBufferSize()); // main_vs
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_FluidSmoothPS->GetBufferPointer(), m_FluidSmoothPS->GetBufferSize()); // main
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
    psoDesc.NumRenderTargets = 1;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;

    ThrowIfFailed(m_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_FluidSmoothPSO)));
    Helpers::SetDebugName(m_FluidSmoothPSO.Get(), "m_FluidSmoothPSO");
}

void Renderer::CreateFluidThicknessPSO()
{
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };

    psoDesc.pRootSignature = m_FluidDepthRootSig.Get();

    psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_ParticleVS->GetBufferPointer(), m_ParticleVS->GetBufferSize());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_FluidThicknessPS->GetBufferPointer(), m_FluidThicknessPS->GetBufferSize());

    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    D3D12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    D3D12_RENDER_TARGET_BLEND_DESC& rtBlend = blendDesc.RenderTarget[0];
    rtBlend.BlendEnable = TRUE;
    rtBlend.SrcBlend = D3D12_BLEND_ONE;
    rtBlend.DestBlend = D3D12_BLEND_ONE;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;

    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_CLEAR;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;

    psoDesc.BlendState = blendDesc;

    psoDesc.DepthStencilState.DepthEnable = FALSE;

    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;

    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(m_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_FluidThicknessPSO)));
    Helpers::SetDebugName(m_FluidThicknessPSO.Get(), "m_FluidThicknessPSO");
}

void Renderer::CreateFluidCompositeRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

    CD3DX12_ROOT_PARAMETER1 params[2];
    params[0].InitAsConstants(sizeof(CompositeParams) / 4.0, 0);
    params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC samplers[2];

    // [0] Point Clamp (s0)
    samplers[0] = CD3DX12_STATIC_SAMPLER_DESC(0);
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    // [1] Linear Clamp (s1)
    samplers[1] = CD3DX12_STATIC_SAMPLER_DESC(1);
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
    desc.Init_1_1(2, params, 2, samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // Serialize and Create
    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DX12SerializeVersionedRootSignature(
        &desc,
        m_FeatureData.HighestVersion,
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
        IID_PPV_ARGS(&m_FluidCompositeRootSig)
    ));

    Helpers::SetDebugName(m_FluidCompositeRootSig.Get(), "m_FluidCompositeRootSig");

}

void Renderer::CreateFluidCompositePSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_FluidCompositeRootSig.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_FullScreenQuadVS->GetBufferPointer(), m_FullScreenQuadVS->GetBufferSize());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_FluidCompositePS->GetBufferPointer(), m_FluidCompositePS->GetBufferSize());
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.NumRenderTargets = 1;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;

    ThrowIfFailed(m_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_FluidCompositePSO)));
    Helpers::SetDebugName(m_FluidCompositePSO.Get(), "m_FluidCompositePSO");
}
