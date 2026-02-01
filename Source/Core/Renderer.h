#pragma once

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT4 color;
};

class Renderer
{
public:
    Renderer() {}
    ~Renderer() {}
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Initialize(ID3D12Device* device);

private:
    ID3D12Device* m_pDevice;

    ComPtr<IDxcCompiler3> m_Compiler;
    ComPtr<IDxcUtils> m_Utils;
    ComPtr<IDxcIncludeHandler> m_IncludeHandler;

    ComPtr<IDxcBlob> m_BasicVS;
    ComPtr<IDxcBlob> m_BasicPS;
    ComPtr<ID3D12RootSignature> m_BasicRootSig;
    ComPtr<ID3D12PipelineState> m_BasicPSO;

    void InitShaders();
    void InitRootSignatures();
    void InitPSOs();

    void CreateBasicRootSignature();
    void CreateBasicPSO();

    void CompileShader(
        const std::wstring& filename,
        const std::wstring& entryPoint,
        const std::wstring& targetProfile,
        ComPtr<IDxcBlob>& outBlob);
};