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

    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRTV;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentDSV;

    ID3D12Resource* SceneColorTex = nullptr;
    ID3D12Resource* SceneDepthTex = nullptr;

    D3D12_CPU_DESCRIPTOR_HANDLE SceneRTV;
    D3D12_CPU_DESCRIPTOR_HANDLE SceneDSV;
    D3D12_VIEWPORT Viewport;
    D3D12_RECT ScissorRect;

    D3D12_CPU_DESCRIPTOR_HANDLE SceneColorCPUHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE SceneDepthCPUHandle;

    struct GlobalConstants {
        SM::Matrix View;
        SM::Matrix Proj;
        float VisualRadius = 0.02f;
        float ThicknessCoeff = 0.05f;
    } Globals;

    const SphSolver* Solver = nullptr;

    UINT FrameIndex = 0;
};
