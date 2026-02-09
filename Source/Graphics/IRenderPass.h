#pragma once

#include "RenderContext.h"

class IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    void Initialize(const RenderInitContext& ctx, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps)
    {
        m_Width = ctx.Width;
        m_Height = ctx.Height;

        CreateShaders(ctx);
        CreateRootSignatures(ctx);
        CreatePSOs(ctx);
        CreateResources(ctx, uploadHeaps);
    }

    virtual void Render(const RenderContext& ctx) = 0;
    virtual void RenderDepthOnly(const RenderContext& ctx) = 0;

    virtual void OnGui(RenderContext& ctx) = 0;

protected:
    UINT m_Width = 0;
    UINT m_Height = 0;

    virtual void CreateShaders(const RenderInitContext& ctx) = 0;
    virtual void CreateRootSignatures(const RenderInitContext& ctx) = 0;
    virtual void CreatePSOs(const RenderInitContext& ctx) = 0;
    virtual void CreateResources(const RenderInitContext& ctx, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps) = 0;
};