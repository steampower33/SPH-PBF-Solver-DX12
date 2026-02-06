#include "Vertex.h"

#include "RendererManager.h"

void RendererManager::Update(const SM::Matrix& view, const SM::Matrix& proj)
{
    m_RenderContext.Globals.View = view.Transpose();
    m_RenderContext.Globals.Proj = proj.Transpose();
}

void RendererManager::Render(ID3D12GraphicsCommandList* cmdList)
{
    m_RenderContext.CmdList = cmdList;
    m_RenderContext.RTV = m_RenderInitContext.GraphicsCore->GetCurrentBackBufferRTV();
    m_RenderContext.DSV = m_RenderInitContext.GraphicsCore->GetDepthStencilView();

    UINT width = m_RenderInitContext.Width;
    UINT height = m_RenderInitContext.Height;
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)width, (LONG)height };
    m_RenderContext.Viewport = viewport;
    m_RenderContext.ScissorRect = scissorRect;

    m_SSFRPass.Render(m_RenderContext);
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

    m_SSFRPass.Initialize(m_RenderInitContext);
}

void RendererManager::OnGui()
{
    m_SSFRPass.OnGui(m_RenderContext);
}