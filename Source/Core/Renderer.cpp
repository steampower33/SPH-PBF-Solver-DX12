#include "Renderer.h"

void Renderer::Initialize(ID3D12Device* device)
{
    m_pDevice = device;

	ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_Utils)));
	ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_Compiler)));
	ThrowIfFailed(m_Utils->CreateDefaultIncludeHandler(&m_IncludeHandler));

    InitShaders();
    InitRootSignatures();
    InitPSOs();
}

void Renderer::InitShaders()
{
    CompileShader(L"BasicVS.hlsl", L"main", L"vs_6_0", m_BasicVS);
    CompileShader(L"BasicPS.hlsl", L"main", L"ps_6_0", m_BasicPS);
}

void Renderer::InitRootSignatures()
{
    CreateBasicRootSignature();
}

void Renderer::InitPSOs()
{
    CreateBasicPSO();
}

void Renderer::CreateBasicRootSignature()
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(
        0,
        nullptr,
        0, nullptr, // No Static Samplers
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DX12SerializeVersionedRootSignature(
        &rootSignatureDesc,
        featureData.HighestVersion,
        &signatureBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        ThrowIfFailed(hr);
    }

    ThrowIfFailed(m_pDevice->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&m_BasicRootSig)
    ));

    m_BasicRootSig->SetName(L"BasicRootSignature_NoTexture");
}

void Renderer::CreateBasicPSO()
{
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // 1. Define PSO descriptor
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    // 2. Bind Shader and Root Signature
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_BasicRootSig.Get();        // Must be created beforehand

    // For Vertex Shader
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(
        m_BasicVS->GetBufferPointer(),
        m_BasicVS->GetBufferSize()
    );

    // For Pixel Shader
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(
        m_BasicPS->GetBufferPointer(),
        m_BasicPS->GetBufferSize()
    );

    // 3. Rasterizer State (Default: Back-face culling, Solid fill)
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    // 4. Blend State (Default: Blending disabled)
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    // 5. Depth Stencil State (Default: Depth test enabled)
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    // 6. Output Formats (CRITICAL: Must match SwapChain/DSV formats)
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // Matches SwapChain format
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;  // Matches Depth Buffer format

    // 7. Topology & Multisample settings
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1; // No MSAA
    psoDesc.SampleDesc.Quality = 0;

    // 8. Create the PSO
    ThrowIfFailed(m_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_BasicPSO)));
}

void Renderer::CompileShader(
    const std::wstring& filename,
    const std::wstring& entryPoint,
    const std::wstring& targetProfile,
    ComPtr<IDxcBlob>& outBlob)
{
    // 1. Construct full path
    std::wstring shaderBaseName = L"./Shaders/";
    std::wstring fullPath = shaderBaseName + filename;

    // 2. Load the shader file
    ComPtr<IDxcBlobEncoding> source;
    HRESULT hr = m_Utils->LoadFile(fullPath.c_str(), nullptr, &source);
    if (FAILED(hr))
    {
        std::wstring errorMsg = L"Failed to load shader file: " + fullPath;
        MessageBox(nullptr, errorMsg.c_str(), L"Shader Load Error", MB_OK);
        ThrowIfFailed(hr);
    }

    DxcBuffer buffer = {};
    buffer.Ptr = source->GetBufferPointer();
    buffer.Size = source->GetBufferSize();
    buffer.Encoding = DXC_CP_ACP;

    // 3. Setup Compilation Arguments
    std::wstring shaderName = std::filesystem::path(filename).stem().wstring();
    std::wstring pdbFilename = L"./PDB/" + shaderName + L".pdb";
    std::wstring shaderIncludePath = std::filesystem::absolute(L"./Shaders").wstring();

    std::vector<LPCWSTR> args;
    args.push_back(L"-E");
    args.push_back(entryPoint.c_str());
    args.push_back(L"-T");
    args.push_back(targetProfile.c_str());
    args.push_back(L"-I");
    args.push_back(shaderIncludePath.c_str());

#if defined(_DEBUG)
    args.push_back(L"-Zi"); // Enable Debug Info
    args.push_back(L"-Od"); // Disable Optimization
    args.push_back(L"-Fd"); // PDB File Name
    args.push_back(pdbFilename.c_str());
#else
    args.push_back(L"-O3"); // High Optimization
#endif

    // 4. Compile
    ComPtr<IDxcResult> result;
    ThrowIfFailed(m_Compiler->Compile(
        &buffer,
        args.data(),
        static_cast<UINT32>(args.size()),
        m_IncludeHandler.Get(),
        IID_PPV_ARGS(&result)
    ));

    // 5. Check for Errors/Warnings
    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);

    if (errors && errors->GetStringLength() > 0)
    {
        OutputDebugStringA("================ Shader Compile Output ================\n");
        OutputDebugStringA((char*)errors->GetBufferPointer());
        OutputDebugStringA("=======================================================\n");
    }

    // 6. Handle Status (Fail if compilation failed)
    HRESULT hrStatus;
    result->GetStatus(&hrStatus);
    ThrowIfFailed(hrStatus);

    // 7. Save PDB (Optional but recommended for debugging)
    ComPtr<IDxcBlob> pdbBlob;
    result->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(&pdbBlob), nullptr);
    if (pdbBlob)
    {
        std::filesystem::create_directories(L"./PDB");
        std::ofstream pdbFile(pdbFilename, std::ios::binary);
        if (pdbFile)
        {
            pdbFile.write((const char*)pdbBlob->GetBufferPointer(), pdbBlob->GetBufferSize());
        }
    }

    // 8. Retrieve Compiled Object (The Shader Blob)
    ThrowIfFailed(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&outBlob), nullptr));
}