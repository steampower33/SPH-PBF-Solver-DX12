#pragma once

class ShaderHelper
{
public:
	void Initialize();

	ComPtr<IDxcBlob> Compile(
		const std::wstring& shaderBaseName,
		const std::wstring& filename,
		const std::wstring& entryPoint,
		const std::wstring& targetProfile);

private:
	ComPtr<IDxcUtils> m_Utils;
	ComPtr<IDxcCompiler3> m_Compiler;
	ComPtr<IDxcIncludeHandler> m_IncludeHandler;
};