#include "ShaderHelper.h"

#include "SphSolver.h"

void SphSolver::Update(ID3D12GraphicsCommandList* cmdList)
{
	auto barrierToUAV = CD3DX12_RESOURCE_BARRIER::Transition(
		m_ParticleBuffer.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmdList->ResourceBarrier(1, &barrierToUAV);

	cmdList->SetPipelineState(m_IntegrationPSO.Get());
	cmdList->SetComputeRootSignature(m_ComputeRootSig.Get());

	ID3D12DescriptorHeap* heaps[] = { m_UavHeap.Get() };
	cmdList->SetDescriptorHeaps(1, heaps);

	cmdList->SetComputeRoot32BitConstants(0, sizeof(SimParams) / 4, &m_SimParams, 0);

	cmdList->SetComputeRootDescriptorTable(1, m_UavHeap->GetGPUDescriptorHandleForHeapStart());

	UINT groups = (m_NumParticles + 255) / 256;
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

	for (int iter = 0; iter < m_SolverIterations; ++iter)
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

		auto posBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_ParticleBuffer.Get());
		cmdList->ResourceBarrier(1, &posBarrier);
	}

	cmdList->SetPipelineState(m_UpdateVelocityPSO.Get());
	cmdList->Dispatch(groups, 1, 1);

	auto barrierToSRV = CD3DX12_RESOURCE_BARRIER::Transition(
		m_ParticleBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->ResourceBarrier(1, &barrierToSRV);
}

void SphSolver::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper)
{
	std::vector<Particle> particles(m_NumParticles);
	InitRandomParticles(particles);

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

	UINT gridDim = m_SimParams.GridDim;
	UINT numGridCells = gridDim * gridDim * gridDim;
	UINT64 gridBufferSize = numGridCells * sizeof(UINT) * 2;

	m_GridIndicesBuffer = Helpers::CreateDefaultBuffer(
		device, cmdList, nullptr, gridBufferSize, m_GridIndicesUpload,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS // Critical: UAV
	);

	UINT64 floatBufferSize = m_NumParticles * sizeof(float);

	m_DensityBuffer = Helpers::CreateDefaultBuffer(
		device, cmdList, nullptr, floatBufferSize, m_DensityUpload,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	m_LambdaBuffer = Helpers::CreateDefaultBuffer(
		device, cmdList, nullptr, floatBufferSize, m_LambdaUpload,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	// Create SRV Heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 1;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_SrvHeap)));

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
	}


	{
		m_SimParams.NumParticles = m_NumParticles;
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
	CreateUpdateVelocityPSO(device, shaderHelper);
}

void SphSolver::CreateUavHeap(ID3D12Device* device)
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 4;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_UavHeap)));
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
}

void SphSolver::CreateComputePSO(ID3D12Device* device, ShaderHelper* shaderHelper)
{
	ComPtr<IDxcBlob> csBlob = shaderHelper->Compile(L"IntegrationCS.hlsl", L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_ComputeRootSig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_IntegrationPSO)));
}

void SphSolver::InitRandomParticles(std::vector<Particle>& outParticles)
{
	outParticles.clear();

	float spacing = m_SimParams.CellSize * 0.5f;
	float startX = m_SimParams.Box.x * 0.5f;
	float startY = m_SimParams.Box.z * 0.5f;

	int cols = 512;
	int rows = 256;

	for (int y = 0; y < rows; ++y)
	{
		for (int x = 0; x < cols; ++x)
		{
			Particle p = {};

			p.Position.x = startX + (x * spacing);
			p.Position.y = startY + (y * spacing);
			p.Position.z = 0.0f;

			p.Velocity = SM::Vector3(0, 0, 0);
			p.Density = 0.0f;
			p.Pressure = 0.0f;
			p.OldPosition = p.Position;

			outParticles.push_back(p);
		}
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
}

void SphSolver::CreateSortPSO(ID3D12Device* device, ShaderHelper* helper)
{
	ComPtr<IDxcBlob> csBlob = helper->Compile(L"BitonicSortCS.hlsl", L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_SortRootSig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_SortPSO)));
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
}

void SphSolver::CreateGridMapPSO(ID3D12Device* device, ShaderHelper* helper)
{
	{
		ComPtr<IDxcBlob> csBlob = helper->Compile(L"ClearGridIndicesCS.hlsl", L"main", L"cs_6_0");

		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_GridMapRootSig.Get();
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

		ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_ClearGridPSO)));
	}

	{
		ComPtr<IDxcBlob> csBlob = helper->Compile(L"BuildGridIndicesCS.hlsl", L"main", L"cs_6_0");

		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_GridMapRootSig.Get();
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

		ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_BuildGridPSO)));
	}
}

void SphSolver::CreatePbfSolverRootSignature(ID3D12Device* device)
{
	CD3DX12_ROOT_PARAMETER1 rootParameters[2];
	rootParameters[0].InitAsConstants(sizeof(SimParams) / 4, 0); // b0

	CD3DX12_DESCRIPTOR_RANGE1 uavRange;

	uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	rootParameters[1].InitAsDescriptorTable(1, &uavRange);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(_countof(rootParameters), rootParameters);

	ComPtr<ID3DBlob> signatureBlob, errorBlob;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signatureBlob, &errorBlob);

	if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); ThrowIfFailed(hr); }

	ThrowIfFailed(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_PbfSolverRootSig)));
}

void SphSolver::CreateDensityLambdaPSO(ID3D12Device* device, ShaderHelper* helper)
{
	ComPtr<IDxcBlob> csBlob = helper->Compile(L"DensityLambdaCS.hlsl", L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_PbfSolverRootSig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_DensityLambdaPSO)));
}

void SphSolver::CreateDeltaPosPSO(ID3D12Device* device, ShaderHelper* helper)
{
	ComPtr<IDxcBlob> csBlob = helper->Compile(L"DeltaPosCS.hlsl", L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_PbfSolverRootSig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_DeltaPosPSO)));
}

void SphSolver::CreateUpdateVelocityPSO(ID3D12Device* device, ShaderHelper* helper)
{
	ComPtr<IDxcBlob> csBlob = helper->Compile(L"UpdateVelocityCS.hlsl", L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_PbfSolverRootSig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_UpdateVelocityPSO)));
}
