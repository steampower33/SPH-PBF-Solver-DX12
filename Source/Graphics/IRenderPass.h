#pragma once

struct RenderInitContext;
struct RenderContext;

class IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    virtual void Initialize(const RenderInitContext& ctx) = 0;

    virtual void Render(const RenderContext& ctx) = 0;
};