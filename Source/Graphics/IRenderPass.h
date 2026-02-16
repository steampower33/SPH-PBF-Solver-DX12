#pragma once

#include "RenderContext.h"

class IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    void Initialize(RenderContext& ctx, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps)
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

    virtual void CreateShaders(RenderContext& ctx) = 0;
    virtual void CreateRootSignatures(RenderContext& ctx) = 0;
    virtual void CreatePSOs(RenderContext& ctx) = 0;
    virtual void CreateResources(RenderContext& ctx, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps) = 0;
};