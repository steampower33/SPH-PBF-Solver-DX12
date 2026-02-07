#include "SSFRPass.h"
#include "PlanePass.h"
#include "GraphicsCore.h"

#include "RendererManager.h"

void RendererManager::Update(const SM::Matrix& view, const SM::Matrix& proj)
{
    m_RenderContext.Globals.View = view.Transpose();
    m_RenderContext.Globals.Proj = proj.Transpose();
    m_RenderContext.InvView = view.Invert().Transpose();
    m_RenderContext.InvProj = proj.Invert().Transpose();
}

void RendererManager::Render(ID3D12GraphicsCommandList* cmdList)
{
    m_RenderContext.CmdList = cmdList;

    m_RenderContext.CurrentRTV = m_RenderInitContext.GraphicsCore->GetCurrentBackBufferRTV();
    m_RenderContext.CurrentDSV = m_RenderInitContext.GraphicsCore->GetDepthStencilView();

    m_RenderContext.SceneRTV = m_SceneColorRTVHandle;
    m_RenderContext.SceneDSV = m_SceneDSVHandle;

    m_RenderContext.SceneColorCPUHandle = m_SceneColorSRVHandle;
    m_RenderContext.SceneDepthCPUHandle = m_SceneDepthSRVHandle;

    m_RenderContext.SceneColorTex = m_SceneColorTex.Get();
    m_RenderContext.SceneDepthTex = m_SceneDepthTex.Get();

    UINT width = m_RenderInitContext.Width;
    UINT height = m_RenderInitContext.Height;
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)width, (LONG)height };
    m_RenderContext.Viewport = viewport;
    m_RenderContext.ScissorRect = scissorRect;

    m_RenderContext.FrameIndex = m_RenderInitContext.GraphicsCore->m_FrameIndex;

    for (auto& pass : m_RenderPasses)
        pass->Render(m_RenderContext);
}

void RendererManager::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, float width, float height, GraphicsCore* graphicsCore, SphSolver* sphSolver)
{
    m_RenderInitContext.Device = device;
    m_RenderInitContext.CmdList = cmdList;
    m_RenderInitContext.ShaderHelper = shaderHelper;
    m_RenderInitContext.GraphicsCore = graphicsCore;
    m_RenderInitContext.Width = width;
    m_RenderInitContext.Height = height;

    m_RenderContext.Device = device;
    m_RenderContext.Solver = sphSolver;

    CreateSceneResources(m_RenderInitContext);

    m_RenderPasses.emplace_back(std::make_unique<PlanePass>());
    m_RenderPasses.emplace_back(std::make_unique<SSFRPass>()); // Render Last

    for (auto& pass : m_RenderPasses)
        pass->Initialize(m_RenderInitContext);
}

void RendererManager::OnGui()
{
    for (auto& pass : m_RenderPasses)
        pass->OnGui(m_RenderContext);
}
void RendererManager::CreateSceneResources(const RenderInitContext& ctx)
{
    auto device = ctx.Device;
    UINT width = ctx.Width;
    UINT height = ctx.Height;

    // Descriptor Heap (RTV, DSV, SRV)
    {
        // RTV Heap Scene Color 
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = 1;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_SceneRTVHeap)));

        // DSV Heap Scene Depth
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_SceneDSVHeap)));

        // SRV Heap Scene Color 1 + Scene Depth 1 = 2
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 2; // Color 1, Depth 1
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SceneSRVHeap)));
    }

    // [Color] FP16 For HDR
    DXGI_FORMAT colorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

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
        texDesc.Format = colorFormat;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = colorFormat;
        clearVal.Color[0] = 0.0f; clearVal.Color[1] = 0.0f; clearVal.Color[2] = 0.0f; clearVal.Color[3] = 1.0f;

        // Initial State: RENDER_TARGET
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearVal, IID_PPV_ARGS(&m_SceneColorTex)
        ));

        Helpers::SetDebugName(m_SceneColorTex.Get(), "Scene Color Texture");
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
            &clearVal, IID_PPV_ARGS(&m_SceneDepthTex)
        ));

        Helpers::SetDebugName(m_SceneDepthTex.Get(), "Scene Depth Texture");
    }

    // RTV
    m_SceneColorRTVHandle = m_SceneRTVHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = colorFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(m_SceneColorTex.Get(), &rtvDesc, m_SceneColorRTVHandle);

    // DSV
    m_SceneDSVHandle = m_SceneDSVHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = depthDSVFormat; // D32_FLOAT
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(m_SceneDepthTex.Get(), &dsvDesc, m_SceneDSVHandle);

    // SRV
    UINT srvIncSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_SceneSRVHeap->GetCPUDescriptorHandleForHeapStart());

    m_SceneColorSRVHandle = srvHandle;
    D3D12_SHADER_RESOURCE_VIEW_DESC colorSrvDesc = {};
    colorSrvDesc.Format = colorFormat;
    colorSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    colorSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    colorSrvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(m_SceneColorTex.Get(), &colorSrvDesc, srvHandle);

    srvHandle.Offset(1, srvIncSize);
     m_SceneDepthSRVHandle = srvHandle;

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Format = depthSRVFormat; // R32_FLOAT
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(m_SceneDepthTex.Get(), &depthSrvDesc, srvHandle);
}