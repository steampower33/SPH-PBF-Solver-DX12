#include "ShaderHelper.h"

void ShaderHelper::Initialize()
{
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_Utils)));
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_Compiler)));
    ThrowIfFailed(m_Utils->CreateDefaultIncludeHandler(&m_IncludeHandler));
}

ComPtr<IDxcBlob> ShaderHelper::Compile(
    const std::wstring& filename,
    const std::wstring& entryPoint,
    const std::wstring& targetProfile)
{
    // Construct full path
    std::wstring shaderBaseName = L"./Shaders/";
    std::wstring fullPath = shaderBaseName + filename;

    // Load file
    Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;
    HRESULT hr = m_Utils->LoadFile(fullPath.c_str(), nullptr, &source);
    if (FAILED(hr))
    {
        std::wstring msg = L"Failed to load shader: " + fullPath;
        MessageBox(nullptr, msg.c_str(), L"Shader Error", MB_OK);
        ThrowIfFailed(hr);
    }

    DxcBuffer buffer = {};
    buffer.Ptr = source->GetBufferPointer();
    buffer.Size = source->GetBufferSize();
    buffer.Encoding = DXC_CP_ACP;

    // Arguments
    std::vector<LPCWSTR> args;
    args.push_back(L"-E"); args.push_back(entryPoint.c_str());
    args.push_back(L"-T"); args.push_back(targetProfile.c_str());

    // Include path
    std::wstring includePath = std::filesystem::absolute(L"./Shaders").wstring();
    args.push_back(L"-I"); args.push_back(includePath.c_str());

#if defined(_DEBUG)
    args.push_back(L"-Zi");
    args.push_back(L"-Od");
#else
    args.push_back(L"-O3");
#endif

    // Compile
    Microsoft::WRL::ComPtr<IDxcResult> result;
    ThrowIfFailed(m_Compiler->Compile(
        &buffer,
        args.data(),
        (UINT32)args.size(),
        m_IncludeHandler.Get(),
        IID_PPV_ARGS(&result)
    ));

    // Error Check
    Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (errors && errors->GetStringLength() > 0)
    {
        OutputDebugStringA((char*)errors->GetBufferPointer());
    }

    HRESULT status;
    result->GetStatus(&status);
    ThrowIfFailed(status);

    // Get Blob
    Microsoft::WRL::ComPtr<IDxcBlob> outBlob;
    ThrowIfFailed(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&outBlob), nullptr));

    return outBlob;
}