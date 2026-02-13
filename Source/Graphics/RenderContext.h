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

    D3D12_CPU_DESCRIPTOR_HANDLE SceneColorSRVHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE SceneDepthSRVHandle;

    struct GlobalConstants {
        SM::Matrix View;
        SM::Matrix Proj;
        float VisualRadius = 0.05f;
        float ThicknessCoeff = 0.10f;
    } Globals;

    SM::Matrix ViewProj;
    SM::Matrix InvView;
    SM::Matrix InvProj;
    SM::Vector3 CamPos;

    UINT res = 2048;
    SM::Vector3 LightPos = SM::Vector3(10.0f, 30.0f, -5.0f);
    SM::Vector3 TargetPos = SM::Vector3(0.0f, 0.0f, 0.0f);
    SM::Vector3 LightDir;
    SM::Matrix LightView;
    SM::Matrix LightProj;
    SM::Matrix ShadowTransform;
    float ShadowIntensity = 0.8f;

    ComPtr<ID3D12DescriptorHeap> ShadowSRVHeap;

    D3D12_CPU_DESCRIPTOR_HANDLE ShadowDSVHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE ShadowSRVHandle;

    const SphSolver* Solver = nullptr;

    UINT FrameIndex = 0;
};
