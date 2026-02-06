#pragma once

class SphSolver;
class ShaderHelper;
class GraphicsCore;

struct RenderInitContext
{
    ID3D12Device* Device;
    ID3D12GraphicsCommandList* CmdList;
    ShaderHelper* ShaderHelper;
    GraphicsCore* GraphicsCore;

    int Width;
    int Height;
};

struct RenderContext
{
    ID3D12Device* Device;
    ID3D12GraphicsCommandList* CmdList;

    D3D12_CPU_DESCRIPTOR_HANDLE RTV;
    D3D12_CPU_DESCRIPTOR_HANDLE DSV;
    D3D12_VIEWPORT Viewport;
    D3D12_RECT ScissorRect;

    struct GlobalConstants {
        SM::Matrix View;
        SM::Matrix Proj;
        float VisualRadius = 0.02f;
        float ThicknessCoeff = 0.05f;
    } Globals;

    const SphSolver* Solver = nullptr;
};
