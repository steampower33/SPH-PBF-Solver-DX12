#include "ShaderHelper.h"

#include "SphSolver.h"

void SphSolver::Update(ID3D12GraphicsCommandList* cmdList)
{
	float pushStrength = 5.0f;

	if (GetAsyncKeyState(VK_LEFT) & 0x8000)
	{
		m_SimParams.ExternalAccel = -pushStrength;
	}
	else if (GetAsyncKeyState(VK_DOWN) & 0x8000)
	{
		m_SimParams.ExternalAccel = 0.0f;
	}

	auto barrierToUAV = CD3DX12_RESOURCE_BARRIER::Transition(
		m_ParticleBuffer.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmdList->ResourceBarrier(1, &barrierToUAV);

	if (m_WallMove)
	{
		m_TotalTime += m_SimParams.DeltaTime;
		float animationFactor = 0.5f * (1.0f - cosf(m_TotalTime * m_WallSpeed));
		m_SimParams.BoxX.x = m_OriginMinX + (m_WallAmplitude * animationFactor);
	}

	ID3D12DescriptorHeap* heaps[] = { m_UavHeap.Get() };
	UINT groups = (m_NumParticles + 255) / 256;

	cmdList->SetPipelineState(m_IntegrationPSO.Get());
	cmdList->SetComputeRootSignature(m_ComputeRootSig.Get());
	cmdList->SetDescriptorHeaps(1, heaps);
	cmdList->SetComputeRoot32BitConstants(0, sizeof(SimParams) / 4, &m_SimParams, 0);
	cmdList->SetComputeRootDescriptorTable(1, m_UavHeap->GetGPUDescriptorHandleForHeapStart());
	cmdList->Dispatch(groups, 1, 1);

	auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_ParticleBuffer.Get());
	cmdList->ResourceBarrier(1, &uavBarrier);

	RunBitonicSort(cmdList);

	cmdList->SetComputeRootSignature(m_GridMapRootSig.Get());
	cmdList->SetDescriptorHeaps(1, heaps);
	cmdList->SetComputeRoot32BitConstants(0, sizeof(SimParams) / 4, &m_SimParams, 0);
	cmdList->SetComputeRootDescriptorTable(1, m_UavHeap->GetGPUDescriptorHandleForHeapStart());

	cmdList->SetPipelineState(m_ClearGridPSO.Get());
	UINT numGridCells = m_SimParams.GridDim * m_SimParams.GridDim * m_SimParams.GridDim;
	UINT gridGroups = (numGridCells + 255) / 256;
	cmdList->Dispatch(gridGroups, 1, 1);

	auto gridBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_GridIndicesBuffer.Get());
	cmdList->ResourceBarrier(1, &gridBarrier);

	cmdList->SetPipelineState(m_BuildGridPSO.Get());
	cmdList->Dispatch(groups, 1, 1);
	cmdList->ResourceBarrier(1, &gridBarrier);

	cmdList->SetComputeRootSignature(m_PbfSolverRootSig.Get());
	cmdList->SetDescriptorHeaps(1, heaps);
	cmdList->SetComputeRoot32BitConstants(0, sizeof(SimParams) / 4, &m_SimParams, 0);
	cmdList->SetComputeRootDescriptorTable(1, m_UavHeap->GetGPUDescriptorHandleForHeapStart());

	auto posBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_ParticleBuffer.Get());

	for (int iter = 0; iter < m_Iterations; ++iter)
	{
		cmdList->SetPipelineState(m_DensityLambdaPSO.Get());
		cmdList->Dispatch(groups, 1, 1);

		CD3DX12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::UAV(m_DensityBuffer.Get()),
			CD3DX12_RESOURCE_BARRIER::UAV(m_LambdaBuffer.Get())
		};
		cmdList->ResourceBarrier(2, barriers);

		cmdList->SetPipelineState(m_DeltaPosPSO.Get());
		cmdList->Dispatch(groups, 1, 1);
		cmdList->ResourceBarrier(1, &posBarrier);

		cmdList->SetPipelineState(m_ConstraintPSO.Get());
		cmdList->Dispatch(groups, 1, 1);
		cmdList->ResourceBarrier(1, &posBarrier);
	}

	auto vorticityBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_VorticityBuffer.Get());
	cmdList->SetPipelineState(m_VorticityPSO.Get());
	cmdList->Dispatch(groups, 1, 1);
	cmdList->ResourceBarrier(1, &vorticityBarrier);

	cmdList->SetPipelineState(m_UpdateVelocityPSO.Get());
	cmdList->Dispatch(groups, 1, 1);

	auto barrierToSRV = CD3DX12_RESOURCE_BARRIER::Transition(
		m_ParticleBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->ResourceBarrier(1, &barrierToSRV);
}

void SphSolver::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper)
{
	std::vector<Particle> particles(m_NumParticles);
	InitParticles(particles);

	m_NumParticles = particles.size();

	UINT64 bufferSize = sizeof(Particle) * m_NumParticles;

	m_ParticleBuffer = Helpers::CreateDefaultBuffer(
		device,
		cmdList,
		particles.data(),
		bufferSize,
		m_UploadBuffer,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);
	Helpers::SetDebugName(m_ParticleBuffer.Get(), "m_ParticleBuffer");

	UINT gridDim = m_SimParams.GridDim;
	UINT numGridCells = gridDim * gridDim * gridDim;
	UINT64 gridBufferSize = numGridCells * sizeof(UINT) * 2;

	m_GridIndicesBuffer = Helpers::CreateDefaultBuffer(
		device, cmdList, nullptr, gridBufferSize, m_GridIndicesUpload,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);
	Helpers::SetDebugName(m_GridIndicesBuffer.Get(), "m_GridIndicesBuffer");

	UINT64 floatBufferSize = m_NumParticles * sizeof(float);

	m_DensityBuffer = Helpers::CreateDefaultBuffer(
		device, cmdList, nullptr, floatBufferSize, m_DensityUpload,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	Helpers::SetDebugName(m_DensityBuffer.Get(), "m_DensityBuffer");

	m_LambdaBuffer = Helpers::CreateDefaultBuffer(
		device, cmdList, nullptr, floatBufferSize, m_LambdaUpload,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	Helpers::SetDebugName(m_LambdaBuffer.Get(), "m_LambdaBuffer");

	UINT64 float3BufferSize = m_NumParticles * sizeof(float) * 3;

	m_VorticityBuffer = Helpers::CreateDefaultBuffer(
		device, cmdList, nullptr, float3BufferSize, m_VorticityUpload,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	Helpers::SetDebugName(m_VorticityBuffer.Get(), "m_VorticityBuffer");

	// Create SRV Heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 1;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_SrvHeap)));
		Helpers::SetDebugName(m_SrvHeap.Get(), "Heap_SRV_Particles");

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = m_NumParticles;
		srvDesc.Buffer.StructureByteStride = sizeof(Particle);
		device->CreateShaderResourceView(m_ParticleBuffer.Get(), &srvDesc, m_SrvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	CreateUavHeap(device);

	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_UavHeap->GetCPUDescriptorHandleForHeapStart());
		UINT incSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// u0 : Particles
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDescParticles = {};
		uavDescParticles.Format = DXGI_FORMAT_UNKNOWN;
		uavDescParticles.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDescParticles.Buffer.FirstElement = 0;
		uavDescParticles.Buffer.NumElements = m_NumParticles;
		uavDescParticles.Buffer.StructureByteStride = sizeof(Particle);

		device->CreateUnorderedAccessView(m_ParticleBuffer.Get(), nullptr, &uavDescParticles, handle);

		// u1 : GridIndices
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDescGrid = {};
		uavDescGrid.Format = DXGI_FORMAT_UNKNOWN;
		uavDescGrid.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDescGrid.Buffer.FirstElement = 0;
		uavDescGrid.Buffer.NumElements = numGridCells;
		uavDescGrid.Buffer.StructureByteStride = sizeof(UINT) * 2; // uint2

		handle.Offset(1, incSize);
		device->CreateUnorderedAccessView(m_GridIndicesBuffer.Get(), nullptr, &uavDescGrid, handle);

		// u2: Density
		handle.Offset(1, incSize);
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDescFloat = {};
		uavDescFloat.Format = DXGI_FORMAT_UNKNOWN; // StructuredBuffer<float>
		uavDescFloat.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDescFloat.Buffer.NumElements = m_NumParticles;
		uavDescFloat.Buffer.StructureByteStride = sizeof(float);
		device->CreateUnorderedAccessView(m_DensityBuffer.Get(), nullptr, &uavDescFloat, handle);

		// u3: Lambda
		handle.Offset(1, incSize);
		device->CreateUnorderedAccessView(m_LambdaBuffer.Get(), nullptr, &uavDescFloat, handle);

		// u4: Vorticity
		handle.Offset(1, incSize);
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDescFloat3 = {};
		uavDescFloat3.Format = DXGI_FORMAT_UNKNOWN; // StructuredBuffer<float3>
		uavDescFloat3.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDescFloat3.Buffer.NumElements = m_NumParticles;
		uavDescFloat3.Buffer.StructureByteStride = sizeof(float) * 3;
		device->CreateUnorderedAccessView(m_VorticityBuffer.Get(), nullptr, &uavDescFloat3, handle);
	}


	{
		m_SimParams.NumParticles = m_NumParticles;
		m_SimParams.DeltaTime = 1.0f / 144.0f;

		m_OriginMinX = m_SimParams.BoxX.x;
	}

	CreateComputeRootSignature(device);
	CreateComputePSO(device, shaderHelper);

	CreateSortRootSignature(device);
	CreateSortPSO(device, shaderHelper);

	CreateGridMapRootSignature(device);
	CreateGridMapPSO(device, shaderHelper);

	CreatePbfSolverRootSignature(device);
	CreateDensityLambdaPSO(device, shaderHelper);
	CreateDeltaPosPSO(device, shaderHelper);

	CreateConstraintPSO(device, shaderHelper);
	CreateVorticityPSO(device, shaderHelper);
	CreateUpdateVelocityPSO(device, shaderHelper);
}

void SphSolver::CreateUavHeap(ID3D12Device* device)
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 5;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_UavHeap)));
	Helpers::SetDebugName(m_UavHeap.Get(), "Heap_UAV_Particles");
}

void SphSolver::CreateComputeRootSignature(ID3D12Device* device)
{
	CD3DX12_ROOT_PARAMETER1 rootParameters[2];
	rootParameters[0].InitAsConstants(sizeof(SimParams) / 4, 0);

	CD3DX12_DESCRIPTOR_RANGE1 uavRange;
	uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	rootParameters[1].InitAsDescriptorTable(1, &uavRange);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(_countof(rootParameters), rootParameters);

	ComPtr<ID3DBlob> signatureBlob, errorBlob;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signatureBlob, &errorBlob);

	if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); ThrowIfFailed(hr); }

	ThrowIfFailed(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_ComputeRootSig)));
	Helpers::SetDebugName(m_ComputeRootSig.Get(), "m_ComputeRootSig");
}

void SphSolver::CreateComputePSO(ID3D12Device* device, ShaderHelper* shaderHelper)
{
	ComPtr<IDxcBlob> csBlob = shaderHelper->Compile(
		m_ShaderBaseName, L"IntegrationCS.hlsl", L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_ComputeRootSig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_IntegrationPSO)));
	Helpers::SetDebugName(m_IntegrationPSO.Get(), "m_IntegrationPSO");
}

void SphSolver::InitParticles(std::vector<Particle>& outParticles)
{
	outParticles.clear();

	float spacing = m_SimParams.CellSize * 0.5f;

	int x_ = 64;
	int y_ = 64;
	int z_ = 32;

	float widthX = m_SimParams.BoxX.x + m_SimParams.BoxX.y;
	float widthY = m_SimParams.BoxY.x + m_SimParams.BoxY.y;
	float widthZ = m_SimParams.BoxZ.x + m_SimParams.BoxZ.y;

	float startX = widthX * 0.5f - spacing * x_ * 0.5f;
	float startY = widthY * 0.5f - spacing * y_ * 0.5f;;
	float startZ = widthZ * 0.5f - spacing * z_ * 0.5f;

	for (int z = 0; z < z_; z++)
		for (int y = 0; y < y_; ++y)
			for (int x = 0; x < x_; ++x)
			{
				Particle p = {};

				p.Position.x = startX + (x * spacing);
				p.Position.y = startY + (y * spacing);
				p.Position.z = startZ + (z * spacing);

				p.Velocity = SM::Vector3(0, 0, 0);
				p.Density = 0.0f;
				p.Pressure = 0.0f;
				p.OldPosition = p.Position;

				outParticles.push_back(p);
			}

}

void SphSolver::CreateSortRootSignature(ID3D12Device* device)
{
	// Just a simple RootSig with 1 ConstantBuffer (b0) and 1 UAV Table (u0)
	CD3DX12_ROOT_PARAMETER1 rootParameters[3];
	rootParameters[0].InitAsConstants(sizeof(SimParams) / 4, 0);
	rootParameters[1].InitAsConstants(sizeof(SortConstants) / 4, 1); // b1: SortConstants

	CD3DX12_DESCRIPTOR_RANGE1 uavRange;
	uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	rootParameters[2].InitAsDescriptorTable(1, &uavRange); // u0: Particle Buffer

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(_countof(rootParameters), rootParameters);

	ComPtr<ID3DBlob> signatureBlob, errorBlob;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signatureBlob, &errorBlob);

	if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); ThrowIfFailed(hr); }

	ThrowIfFailed(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_SortRootSig)));
	Helpers::SetDebugName(m_SortRootSig.Get(), "m_SortRootSig");
}

void SphSolver::CreateSortPSO(ID3D12Device* device, ShaderHelper* helper)
{
	ComPtr<IDxcBlob> csBlob = helper->Compile(m_ShaderBaseName, L"BitonicSortCS.hlsl", L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_SortRootSig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_SortPSO)));
	Helpers::SetDebugName(m_SortPSO.Get(), "m_SortPSO");
}

// [CORE LOOP] This runs on CPU to schedule GPU work
void SphSolver::RunBitonicSort(ID3D12GraphicsCommandList* cmdList)
{
	// 1. Setup Pipeline
	cmdList->SetPipelineState(m_SortPSO.Get());
	cmdList->SetComputeRootSignature(m_SortRootSig.Get());

	ID3D12DescriptorHeap* heaps[] = { m_UavHeap.Get() };
	cmdList->SetDescriptorHeaps(1, heaps);

	cmdList->SetComputeRoot32BitConstants(0, sizeof(SimParams) / 4, &m_SimParams, 0);

	cmdList->SetComputeRootDescriptorTable(2, m_UavHeap->GetGPUDescriptorHandleForHeapStart());

	// 2. Bitonic Sort Algorithm
	// Iterate over 'Block Size' (2, 4, 8, ... N)
	// NumParticles MUST be a Power of 2 (e.g., 1024, 2048) for this to work simply.
	// If m_NumParticles is not POT, we need to pad it, but for now let's assume POT.
	for (UINT blockSize = 2; blockSize <= m_NumParticles; blockSize <<= 1) {
		// Iterate over 'Stride' (BlockSize/2 down to 1)
		for (UINT stride = blockSize >> 1; stride > 0; stride >>= 1) {

			SortConstants sortConsts = { blockSize, stride, 0, 0 };

			// Bind SortConstants (b1) at RootParam 1
			cmdList->SetComputeRoot32BitConstants(1, 4, &sortConsts, 0);

			// Dispatch
			// ThreadGroupSize = 256
			// We need 1 thread per particle? No, 1 thread usually handles 1 comparison (2 particles).
			// But for simplicity in shader, let's say 1 thread = 1 particle logic.
			UINT groups = (m_NumParticles + 255) / 256;
			cmdList->Dispatch(groups, 1, 1);

			// Barrier: We must wait for the swap to finish before the next step
			auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_ParticleBuffer.Get());
			cmdList->ResourceBarrier(1, &barrier);
		}
	}
}

void SphSolver::CreateGridMapRootSignature(ID3D12Device* device)
{
	CD3DX12_ROOT_PARAMETER1 rootParameters[2];
	rootParameters[0].InitAsConstants(sizeof(SimParams) / 4, 0);

	CD3DX12_DESCRIPTOR_RANGE1 uavRange;

	uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	rootParameters[1].InitAsDescriptorTable(1, &uavRange);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(_countof(rootParameters), rootParameters);

	ComPtr<ID3DBlob> signatureBlob, errorBlob;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signatureBlob, &errorBlob);

	if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); ThrowIfFailed(hr); }

	ThrowIfFailed(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_GridMapRootSig)));
	Helpers::SetDebugName(m_GridMapRootSig.Get(), "m_GridMapRootSig");
}

void SphSolver::CreateGridMapPSO(ID3D12Device* device, ShaderHelper* helper)
{
	{
		ComPtr<IDxcBlob> csBlob = helper->Compile(m_ShaderBaseName, L"ClearGridIndicesCS.hlsl", L"main", L"cs_6_0");

		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_GridMapRootSig.Get();
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

		ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_ClearGridPSO)));
		Helpers::SetDebugName(m_ClearGridPSO.Get(), "m_ClearGridPSO");
	}

	{
		ComPtr<IDxcBlob> csBlob = helper->Compile(m_ShaderBaseName, L"BuildGridIndicesCS.hlsl", L"main", L"cs_6_0");

		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_GridMapRootSig.Get();
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

		ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_BuildGridPSO)));
		Helpers::SetDebugName(m_BuildGridPSO.Get(), "m_BuildGridPSO");
	}
}

void SphSolver::CreatePbfSolverRootSignature(ID3D12Device* device)
{
	CD3DX12_ROOT_PARAMETER1 rootParameters[2];
	rootParameters[0].InitAsConstants(sizeof(SimParams) / 4, 0); // b0

	CD3DX12_DESCRIPTOR_RANGE1 uavRange;

	uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	rootParameters[1].InitAsDescriptorTable(1, &uavRange);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(_countof(rootParameters), rootParameters);

	ComPtr<ID3DBlob> signatureBlob, errorBlob;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signatureBlob, &errorBlob);

	if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); ThrowIfFailed(hr); }

	ThrowIfFailed(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_PbfSolverRootSig)));
	Helpers::SetDebugName(m_PbfSolverRootSig.Get(), "m_PbfSolverRootSig");
}

void SphSolver::CreateDensityLambdaPSO(ID3D12Device* device, ShaderHelper* helper)
{
	ComPtr<IDxcBlob> csBlob = helper->Compile(m_ShaderBaseName, L"DensityLambdaCS.hlsl", L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_PbfSolverRootSig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_DensityLambdaPSO)));
	Helpers::SetDebugName(m_DensityLambdaPSO.Get(), "m_DensityLambdaPSO");
}

void SphSolver::CreateDeltaPosPSO(ID3D12Device* device, ShaderHelper* helper)
{
	ComPtr<IDxcBlob> csBlob = helper->Compile(m_ShaderBaseName, L"DeltaPosCS.hlsl", L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_PbfSolverRootSig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_DeltaPosPSO)));
	Helpers::SetDebugName(m_DeltaPosPSO.Get(), "m_DeltaPosPSO");
}

void SphSolver::CreateConstraintPSO(ID3D12Device* device, ShaderHelper* helper)
{
	ComPtr<IDxcBlob> csBlob = helper->Compile(m_ShaderBaseName, L"ConstraintCS.hlsl", L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_PbfSolverRootSig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_ConstraintPSO)));
	Helpers::SetDebugName(m_ConstraintPSO.Get(), "m_ConstraintPSO");
}

void SphSolver::CreateVorticityPSO(ID3D12Device* device, ShaderHelper* helper)
{
	ComPtr<IDxcBlob> csBlob = helper->Compile(m_ShaderBaseName, L"VorticityCS.hlsl", L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_PbfSolverRootSig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_VorticityPSO)));
	Helpers::SetDebugName(m_VorticityPSO.Get(), "m_VorticityPSO");
}

void SphSolver::CreateUpdateVelocityPSO(ID3D12Device* device, ShaderHelper* helper)
{
	ComPtr<IDxcBlob> csBlob = helper->Compile(m_ShaderBaseName, L"UpdateVelocityCS.hlsl", L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_PbfSolverRootSig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_UpdateVelocityPSO)));
	Helpers::SetDebugName(m_UpdateVelocityPSO.Get(), "m_UpdateVelocityPSO");
}

void SphSolver::OnGui()
{
	if (ImGui::CollapsingHeader("PBF Solver Settings", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Simulation Control
		ImGui::SeparatorText("Simulation Control");

		float realFPS = ImGui::GetIO().Framerate;
		float realMS = 1000.0f / realFPS;
		ImGui::TextColored(ImVec4(1, 1, 0, 1), "Real: %.1f FPS (%.3f ms)", realFPS, realMS);

		float fps = (m_SimParams.DeltaTime > 0.0f) ? (int)(1.0f / m_SimParams.DeltaTime) : 60.0f;
		ImGui::TextColored(ImVec4(0, 1, 0, 1), "Sim Fixed: %.1f FPS", fps);

		ImGui::Text("Active Particles: %d", m_SimParams.NumParticles);
		ImGui::Text("Time Step (dt): %.6f s", m_SimParams.DeltaTime);
		if (ImGui::DragFloat("Target Sim FPS", &fps, 1, 30.0, 240.0)) {
			m_SimParams.DeltaTime = 1.0f / (float)std::max(1, (int)fps);
		}
		ImGui::SliderInt("MaxSteps", &m_MaxSteps, 1, 10);
		ImGui::SliderInt("Sub-steps", &m_Iterations, 1, 10);

		// Physical Properties
		ImGui::SeparatorText("Fluid Properties");
		ImGui::DragFloat("Mass", &m_SimParams.Mass, 0.01f, 0.001f, 10.0f);
		ImGui::DragFloat("Rest Density", &m_SimParams.RestDensity, 10.0f, 100.0f, 5000.0f);
		ImGui::DragFloat("Viscosity", &m_SimParams.Viscosity, 0.001f, 0.0f, 5.0f);
		ImGui::DragFloat("Gravity Y", &m_SimParams.GravityY, 0.1f, -20.0f, 20.0f);

		// Boundary & World
		ImGui::SeparatorText("Boundary & World");

		if (ImGui::DragFloat("Cell Size (h)", &m_SimParams.CellSize, 0.001f, 0.01f, 1.0f)) {
			m_SimParams.CellSize = std::max(0.01f, m_SimParams.CellSize);
		}

		ImGui::DragFloat2("Box X (Min/Max)", &m_SimParams.BoxX.x, 0.1f);
		ImGui::DragFloat2("Box Y (Min/Max)", &m_SimParams.BoxY.x, 0.1f);
		ImGui::DragFloat2("Box Z (Min/Max)", &m_SimParams.BoxZ.x, 0.1f);

		// Wall Movement
		ImGui::SeparatorText("Moving Wall Interaction");
		ImGui::Checkbox("Enable Move", &m_WallMove);
		if (m_WallMove)
		{
			ImGui::DragFloat("Wall Speed", &m_WallSpeed, 0.1f);
			ImGui::DragFloat("Wall Amplitude", &m_WallAmplitude, 0.1f);
		}

		// Advanced Stability
		if (ImGui::TreeNode("Advanced Stability"))
		{
			ImGui::DragFloat("CFM Epsilon", &m_SimParams.Epsilon, 10.0f, 0.0f, 1e6f, "%.0f");
			ImGui::DragFloat("Tensile K", &m_SimParams.K, 1e-6f, 0.0f, 1.0f, "%.6f");
			ImGui::DragFloat("Tensile N", &m_SimParams.N, 0.1f, 1.0f, 10.0f);
			ImGui::DragFloat("Vorticity", &m_SimParams.VorticityEpsilon, 1e-6f, 0.0f, 1.0f, "%.6f");
			ImGui::TreePop();
		}
	}
}