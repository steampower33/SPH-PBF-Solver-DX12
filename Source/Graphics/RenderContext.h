#pragma once

class SphSolver;
class ShaderHelper;
class GraphicsCore;
class DescriptorHeapManager;

struct RenderContext
{
    ID3D12Device* Device;
    ID3D12GraphicsCommandList* CmdList;
    ID3D12CommandQueue* Queue;
    ShaderHelper* ShaderHelper;
    GraphicsCore* GraphicsCore;
    DescriptorHeapManager* HeapManager;

    int Width;
    int Height;

    CD3DX12_CPU_DESCRIPTOR_HANDLE CurrentRTV;
    CD3DX12_CPU_DESCRIPTOR_HANDLE CurrentDSV;

    D3D12_VIEWPORT Viewport;
    D3D12_RECT ScissorRect;

    ComPtr<ID3D12Resource> SceneColorTex;
    ComPtr<ID3D12Resource> SceneDepthTex;
    CD3DX12_CPU_DESCRIPTOR_HANDLE SceneRTVHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE SceneDSVHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE SceneColorSRVHandleCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE SceneColorSRVHandleGpu;
    CD3DX12_CPU_DESCRIPTOR_HANDLE SceneDepthSRVHandleCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE SceneDepthSRVHandleGpu;

    ComPtr<ID3D12Resource> ShadowMapTex;
    CD3DX12_CPU_DESCRIPTOR_HANDLE ShadowDSVHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE ShadowSRVHandleCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE ShadowSRVHandleGpu;

    ComPtr<ID3D12Resource> HdrTex;
    CD3DX12_CPU_DESCRIPTOR_HANDLE HdrRTVHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE HdrSRVHandleCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE HdrSRVHandleGpu;

    D3D12_GPU_DESCRIPTOR_HANDLE SkyboxSRVHandleGpu;

    struct GlobalConstants {
        SM::Matrix View;
        SM::Matrix Proj;
        float VisualRadius = 0.05f;
        float ThicknessCoeff = 0.10f;
    } Globals;

    SM::Vector2 InvScreenSize;
    SM::Matrix ViewProj;
    SM::Matrix InvView;
    SM::Matrix InvProj;
    SM::Vector3 CamPos;

    UINT res = 2048;
    SM::Vector3 LightPos = SM::Vector3(-40.0f, 80.0f, -20.0f);
    SM::Vector3 TargetPos = SM::Vector3(0.0f, 0.0f, 0.0f);
    SM::Vector3 LightDir;
    SM::Matrix LightView;
    SM::Matrix LightProj;
    SM::Matrix ShadowTransform;
    float ShadowIntensity = 0.8f;

    const SphSolver* Solver = nullptr;

    UINT FrameIndex = 0;
};
