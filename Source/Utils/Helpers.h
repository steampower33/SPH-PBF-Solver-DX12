#pragma once

inline std::string HrToString(HRESULT hr)
{
	char s_str[64] = {};
	sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
	return std::string(s_str);
}

class HrException : public std::runtime_error
{
public:
	HrException(HRESULT hr) : std::runtime_error(HrToString(hr)), m_hr(hr) {}
	HRESULT Error() const { return m_hr; }
private:
	const HRESULT m_hr;
};

inline void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		throw HrException(hr);
	}
}

namespace Helpers {
	inline void CreateRootSignature(
		ID3D12Device* device,
		const CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC& rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION highestVersion,
		Microsoft::WRL::ComPtr<ID3D12RootSignature>& outRootSig,
		const char* debugName = nullptr)
	{
		Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

		HRESULT hr = D3DX12SerializeVersionedRootSignature(
			&rootSigDesc,
			highestVersion,
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

		ThrowIfFailed(device->CreateRootSignature(
			0,
			signatureBlob->GetBufferPointer(),
			signatureBlob->GetBufferSize(),
			IID_PPV_ARGS(&outRootSig)
		));

		if (debugName)
		{
			std::wstring wName(debugName, debugName + strlen(debugName));
			outRootSig->SetName(wName.c_str());
		}
	}

	inline Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* cmdList,
		const void* initData,
		UINT64 byteSize,
		std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& uploadHeaps,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
	{
		using namespace Microsoft::WRL;
		ComPtr<ID3D12Resource> defaultBuffer;

		// 1. Create Default Buffer (GPU Memory)
		// Start in COMMON state. This is best for UAVs or resources that will be transitioned later.
		auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize, flags);

		ThrowIfFailed(device->CreateCommittedResource(
			&defaultHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&defaultBuffer)));

		// 2. Only upload data if initData is provided!
		if (initData != nullptr)
		{
			// Create Upload Buffer
			auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
			auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize, D3D12_RESOURCE_FLAG_NONE);

			Microsoft::WRL::ComPtr<ID3D12Resource> tempUpload;
			ThrowIfFailed(device->CreateCommittedResource(
				&uploadHeapProps,
				D3D12_HEAP_FLAG_NONE,
				&uploadBufferDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&tempUpload)));

			// Barrier: COMMON -> COPY_DEST
			auto barrierToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
				defaultBuffer.Get(),
				D3D12_RESOURCE_STATE_COMMON,
				D3D12_RESOURCE_STATE_COPY_DEST
			);
			cmdList->ResourceBarrier(1, &barrierToCopy);

			// Copy Data
			D3D12_SUBRESOURCE_DATA subResourceData = {};
			subResourceData.pData = initData;
			subResourceData.RowPitch = byteSize;
			subResourceData.SlicePitch = subResourceData.RowPitch;

			UpdateSubresources<1>(cmdList, defaultBuffer.Get(), tempUpload.Get(), 0, 0, 1, &subResourceData);

			// Barrier: COPY_DEST -> GENERIC_READ
			// If we initialized it with data, we usually want to read it (e.g., Vertex Buffer).
			auto barrierToRead = CD3DX12_RESOURCE_BARRIER::Transition(
				defaultBuffer.Get(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_GENERIC_READ
			);
			cmdList->ResourceBarrier(1, &barrierToRead);

			if (tempUpload)
			{
				uploadHeaps.push_back(tempUpload);
			}
		}

		// If initData is null, the buffer stays in D3D12_RESOURCE_STATE_COMMON.
		// This is perfectly fine (and preferred) for UAVs that will be written to by the GPU later.

		return defaultBuffer;
	}

	// Helper function to set debug names efficiently
	// In Release/Master builds, this can be compiled out if needed.
	inline void SetDebugName(ID3D12Object* pObject, const char* name)
	{
#if defined(_DEBUG) || defined(PROFILE) // Only set names in Debug or Profile builds
		if (pObject && name)
		{
			// Convert const char* to std::wstring (wchar_t)
			// DX12 SetName requires LPCWSTR
			wchar_t wName[256];
			size_t convertedChars = 0;
			mbstowcs_s(&convertedChars, wName, name, _TRUNCATE);

			pObject->SetName(wName);
		}
#endif
	}

	// Overload for std::string
	inline void SetDebugName(ID3D12Object* pObject, const std::string& name)
	{
		SetDebugName(pObject, name.c_str());
	}

}