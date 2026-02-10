#include "ShaderHelper.h"

#include "SphSolver.h"

void SphSolver::UpdateInputs()
{
	float pushStrength = 4.0f;

	if (GetAsyncKeyState(VK_LEFT) & 0x8000)
		m_SimParams.ExternalAccel = -pushStrength;
	else if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
		m_SimParams.ExternalAccel = +pushStrength;
	else if (GetAsyncKeyState(VK_DOWN) & 0x8000)
		m_SimParams.ExternalAccel = 0.0f;

	if (m_bWallMove)
	{
		m_TotalTime += m_SimParams.DeltaTime;
		float animationFactor = 0.5f * (1.0f - cosf(m_TotalTime * m_WallSpeed));
		m_SimParams.BoxX.x = m_OriginMinX + (m_WallAmplitude * animationFactor);
	}

	if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
	{
		m_bReset = true;
	}
}

void SphSolver::Run(ID3D12GraphicsCommandList* cmdList)
{
	if (m_bReset)
	{
		ResetSimulation(cmdList);
		m_bReset = false;
	}

	UpdateInputs();

	cmdList->SetComputeRootSignature(m_GlobalRootSig.Get());

	ID3D12DescriptorHeap* heaps[] = { m_UavHeap.Get() };
	cmdList->SetDescriptorHeaps(1, heaps);

	cmdList->SetComputeRoot32BitConstants(0, sizeof(SimParams) / 4, &m_SimParams, 0);
	cmdList->SetComputeRootDescriptorTable(2, m_UavHeap->GetGPUDescriptorHandleForHeapStart());

	UINT groups = (m_NumParticles + 255) / 256;

	// Barriers
	auto posPredBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_PosPred.Get());

	// [1] Integration Pass
	{
		cmdList->SetPipelineState(m_IntegrationPSO.Get());

		cmdList->Dispatch(groups, 1, 1);

		cmdList->ResourceBarrier(1, &posPredBarrier);
	}

	// [2] Sort Pass
	{
		RunBitonicSort(cmdList);
	}

	// [3] Permute Pass (Data Reordering)
	{
		auto sortedIndicesUAVtoSRV = CD3DX12_RESOURCE_BARRIER::Transition(m_SortedIndices.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmdList->ResourceBarrier(1, &sortedIndicesUAVtoSRV);

		cmdList->SetComputeRootSignature(m_PermuteRootSig.Get());
		cmdList->SetPipelineState(m_PermuteDataPSO.Get());

		cmdList->SetComputeRoot32BitConstants(0, sizeof(SimParams) / 4, &m_SimParams, 0);

		// UAV Table (Param 1) -> u0~u2 (Temp Buffers)
		CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(m_UavHeap->GetGPUDescriptorHandleForHeapStart());
		uavHandle.Offset(UAV_IDX_TEMP_POS, m_CbvSrvUavDescriptorSize);
		cmdList->SetComputeRootDescriptorTable(1, uavHandle);

		// SRV Table (Param 2) -> t0~t3 (Source)
		CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_UavHeap->GetGPUDescriptorHandleForHeapStart());
		srvHandle.Offset(SRV_IDX_POS_PRED, m_CbvSrvUavDescriptorSize);
		cmdList->SetComputeRootDescriptorTable(2, srvHandle);

		cmdList->Dispatch(groups, 1, 1);

		auto sortedIndicesSRVtoUAV = CD3DX12_RESOURCE_BARRIER::Transition(m_SortedIndices.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmdList->ResourceBarrier(1, &sortedIndicesSRVtoUAV);
	}

	// [4] Copy Back
	{
		CD3DX12_RESOURCE_BARRIER barriers[] = {
			// Temp (UAV -> Source)
			CD3DX12_RESOURCE_BARRIER::Transition(m_TempPosPred.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(m_TempPosOld.Get(),  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(m_TempVel.Get(),     D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
			// Main (UAV -> Dest)
			CD3DX12_RESOURCE_BARRIER::Transition(m_PosPred.Get(),     D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(m_PosOld.Get(),      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(m_VelIn.Get(),       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST),
		};
		cmdList->ResourceBarrier(6, barriers);

		cmdList->CopyResource(m_PosPred.Get(), m_TempPosPred.Get());
		cmdList->CopyResource(m_PosOld.Get(), m_TempPosOld.Get());
		cmdList->CopyResource(m_VelIn.Get(), m_TempVel.Get());

		CD3DX12_RESOURCE_BARRIER restoreBarriers[] = {
			// Main: Dest -> UAV
			CD3DX12_RESOURCE_BARRIER::Transition(m_PosPred.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_PosOld.Get(),  D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_VelIn.Get(),   D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),

			// Temp: Source -> UAV
			CD3DX12_RESOURCE_BARRIER::Transition(m_TempPosPred.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_TempPosOld.Get(),  D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_TempVel.Get(),     D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		};
		cmdList->ResourceBarrier(6, restoreBarriers);
	}

	cmdList->SetComputeRootSignature(m_GlobalRootSig.Get());

	cmdList->SetComputeRoot32BitConstants(0, sizeof(SimParams) / 4, &m_SimParams, 0);
	cmdList->SetComputeRootDescriptorTable(2, m_UavHeap->GetGPUDescriptorHandleForHeapStart());

	// [5] Grid Pass
	{
		cmdList->SetPipelineState(m_ClearGridPSO.Get());

		UINT gridGroups = (m_SimParams.GridDim * m_SimParams.GridDim * m_SimParams.GridDim + 255) / 256;
		cmdList->Dispatch(gridGroups, 1, 1);

		auto gridBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_GridIndices.Get());
		cmdList->ResourceBarrier(1, &gridBarrier);

		cmdList->SetPipelineState(m_BuildGridPSO.Get());
		cmdList->Dispatch(groups, 1, 1);

		cmdList->ResourceBarrier(1, &gridBarrier);
	}

	// [5] Solver Iteration
	{
		CD3DX12_RESOURCE_BARRIER DensityLambda[] = {
			CD3DX12_RESOURCE_BARRIER::UAV(m_Density.Get()),
			CD3DX12_RESOURCE_BARRIER::UAV(m_Lambda.Get())
		};

		CD3DX12_RESOURCE_BARRIER PredDelta[] = {
			CD3DX12_RESOURCE_BARRIER::UAV(m_PosPred.Get()),
			CD3DX12_RESOURCE_BARRIER::UAV(m_DeltaPos.Get())
		};

		for (int iter = 0; iter < m_Iterations; ++iter)
		{
			// Density & Lambda
			cmdList->SetPipelineState(m_DensityLambdaPSO.Get());
			cmdList->Dispatch(groups, 1, 1);

			cmdList->ResourceBarrier(2, DensityLambda);

			// Delta Pos
			cmdList->SetPipelineState(m_DeltaPosPSO.Get());
			cmdList->Dispatch(groups, 1, 1);

			cmdList->ResourceBarrier(2, PredDelta);

			// Constraint Apply
			cmdList->SetPipelineState(m_ConstraintPSO.Get());
			cmdList->Dispatch(groups, 1, 1);

			cmdList->ResourceBarrier(1, &posPredBarrier);
		}
	}

	cmdList->SetPipelineState(m_VorticityPSO.Get());
	cmdList->Dispatch(groups, 1, 1);

	auto vorticityBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_Vorticity.Get());
	cmdList->ResourceBarrier(1, &vorticityBarrier);

	cmdList->SetPipelineState(m_UpdateVelocityPSO.Get());
	cmdList->Dispatch(groups, 1, 1);

	auto velOutBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_VelOut.Get());
	cmdList->ResourceBarrier(1, &velOutBarrier);
}

void SphSolver::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps)
{
	m_pDevice = device;

	CreateBuffers(device, cmdList, uploadHeaps);
	CreateRenderSrvHeap(device, cmdList);
	CreateAllViews(device);
	CreateGlobalRootSignature(device);
	CreatePermuteRootSignature(device);

	CreateComputePSO(device, shaderHelper, L"IntegrationCS.hlsl", m_IntegrationPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"BitonicSortCS.hlsl", m_SortPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"PermuteDataCS.hlsl", m_PermuteDataPSO, m_PermuteRootSig);
	CreateComputePSO(device, shaderHelper, L"ClearGridIndicesCS.hlsl", m_ClearGridPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"BuildGridIndicesCS.hlsl", m_BuildGridPSO, m_GlobalRootSig);

	CreateComputePSO(device, shaderHelper, L"DensityLambdaCS.hlsl", m_DensityLambdaPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"DeltaPosCS.hlsl", m_DeltaPosPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"ConstraintCS.hlsl", m_ConstraintPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"VorticityCS.hlsl", m_VorticityPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"UpdateVelocityCS.hlsl", m_UpdateVelocityPSO, m_GlobalRootSig);

	{
		m_SimParams.NumParticles = m_NumParticles;
		m_SimParams.DeltaTime = 1.0f / 144.0f;

		m_OriginMinX = m_SimParams.BoxX.x;
	}
}

void SphSolver::CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, std::vector<ComPtr<ID3D12Resource>>& tempUploadBuffers)
{
	float spacing = m_SimParams.CellSize * 0.5f;

	auto CornerDamBreak = [&]()
		{
			int m_X = 64;
			int m_Y = 64;
			int m_Z = 32;

			m_NumParticles = m_X * m_Y * m_Z;

			m_Zero1.resize(m_NumParticles, 0.0f);
			m_Zero3.resize(m_NumParticles, SM::Vector3(0.0f));

			m_InitPos.resize(m_NumParticles);

			m_SimParams.BoxX = { -3.0f, 3.0f };
			m_SimParams.BoxZ = { -2.0f, 2.0f };

			float offset = 0.2f;

			float startX = m_SimParams.BoxX.x + offset; // up
			float startY = m_SimParams.BoxY.x + offset; // up
			float startZ = m_SimParams.BoxZ.y - offset; // down

			int idx = 0;
			for (int z = 0; z < m_Z; z++)
				for (int y = 0; y < m_Y; ++y)
					for (int x = 0; x < m_X; ++x)
					{
						m_InitPos[idx] = SM::Vector3(
							startX + (x * spacing),
							startY + (y * spacing),
							startZ - (z * spacing));
						idx++;
					}
		};

	auto DoubleDamBreak = [&]()
		{
			int m_X = 64;
			int m_Y = 64;
			int m_Z = 32;

			m_NumParticles = m_X * m_Y * m_Z;

			m_Zero1.resize(m_NumParticles, 0.0f);
			m_Zero3.resize(m_NumParticles, SM::Vector3(0.0f));

			m_InitPos.resize(m_NumParticles);

			m_SimParams.BoxX = { -2.5f, 2.5f };
			m_SimParams.BoxZ = { -2.5f, 2.5f };

			float offset = 0.2f;

			UINT halfX = m_X;
			UINT halfY = m_Y;
			UINT halfZ = m_Z * 0.5f;

			int idx = 0;

			{
				float startX = m_SimParams.BoxX.x + offset; // up
				float startY = m_SimParams.BoxY.x + offset; // up
				float startZ = m_SimParams.BoxZ.x + offset; // up

				for (int z = 0; z < halfZ; z++)
					for (int y = 0; y < halfY; ++y)
						for (int x = 0; x < halfX; ++x)
						{
							m_InitPos[idx] = SM::Vector3(
								startX + (x * spacing),
								startY + (y * spacing),
								startZ + (z * spacing));
							idx++;
						}
			}

			{
				float startX = m_SimParams.BoxX.y - offset; // down
				float startY = m_SimParams.BoxY.x + offset; // up
				float startZ = m_SimParams.BoxZ.y - offset; // down

				for (int z = 0; z < halfZ; z++)
					for (int y = 0; y < halfY; ++y)
						for (int x = 0; x < halfX; ++x)
						{
							m_InitPos[idx] = SM::Vector3(
								startX - (x * spacing),
								startY + (y * spacing),
								startZ - (z * spacing));
							idx++;
						}
			}
		};

	CornerDamBreak();
	//DoubleDamBreak();

	UINT64 sizeVec3 = m_NumParticles * sizeof(SM::Vector3);
	UINT64 sizeFloat = m_NumParticles * sizeof(float);

	auto CreateAndTrackBuffer = [&](
		const void* initData,
		UINT64 byteSize,
		ComPtr<ID3D12Resource>& outDefaultBuffer,
		const char* debugName)
		{
			outDefaultBuffer = Helpers::CreateDefaultBuffer(
				device, cmdList,
				initData, byteSize,
				tempUploadBuffers,
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
		};

	UINT numGridCells = m_SimParams.GridDim * m_SimParams.GridDim * m_SimParams.GridDim;

	CreateAndTrackBuffer(m_InitPos.data(), sizeVec3, m_PosPred, "Pos_Pred");
	CreateAndTrackBuffer(m_InitPos.data(), sizeVec3, m_PosOld, "Pos_Old");
	CreateAndTrackBuffer(m_Zero3.data(), sizeVec3, m_VelIn, "Vel_In");
	CreateAndTrackBuffer(m_Zero3.data(), sizeVec3, m_VelOut, "Vel_Out");
	CreateAndTrackBuffer(m_Zero1.data(), sizeFloat, m_Density, "Density");
	CreateAndTrackBuffer(m_Zero1.data(), sizeFloat, m_Lambda, "Lambda");
	CreateAndTrackBuffer(m_Zero3.data(), sizeVec3, m_DeltaPos, "DeltaPos");
	CreateAndTrackBuffer(m_Zero3.data(), sizeVec3, m_Vorticity, "Vorticity");
	CreateAndTrackBuffer(nullptr, numGridCells * sizeof(UINT) * 2, m_GridIndices, "GridIndices");

	CreateAndTrackBuffer(m_Zero3.data(), sizeVec3, m_TempPosPred, "m_TempPosPred");
	CreateAndTrackBuffer(m_Zero3.data(), sizeVec3, m_TempPosOld, "m_TempPosOld");
	CreateAndTrackBuffer(m_Zero3.data(), sizeVec3, m_TempVel, "m_TempVel");

	std::vector<UINT> indices(m_NumParticles);
	for (UINT i = 0; i < m_NumParticles; ++i)
	{
		indices[i] = i;
	}
	CreateAndTrackBuffer(indices.data(), m_NumParticles * sizeof(UINT), m_SortedIndices, "m_SortedIndices");
}

void SphSolver::CreateRenderSrvHeap(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 2;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_SrvHeap)));
	Helpers::SetDebugName(m_SrvHeap.Get(), "Heap_SRV_Particles");

	CD3DX12_CPU_DESCRIPTOR_HANDLE hHandle(m_SrvHeap->GetCPUDescriptorHandleForHeapStart());
	m_CbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	auto CreateBufferSRV = [&](ID3D12Resource* pBuffer, int heapIdx, UINT stride) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = m_NumParticles;
		srvDesc.Buffer.StructureByteStride = stride;

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = hHandle;
		handle.Offset(heapIdx, m_CbvSrvUavDescriptorSize);
		device->CreateShaderResourceView(pBuffer, &srvDesc, handle);
		};

	CreateBufferSRV(m_PosPred.Get(), 0, sizeof(SM::Vector3));
	CreateBufferSRV(m_Density.Get(), 1, sizeof(float));
}

void SphSolver::CreateAllViews(ID3D12Device* device)
{
	// Create Heap
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = HeapDescriptors::DESCRIPTOR_COUNT;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_UavHeap)));
	Helpers::SetDebugName(m_UavHeap.Get(), "Heap_UAV_SoA");

	m_CbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE hHandle(m_UavHeap->GetCPUDescriptorHandleForHeapStart());

	UINT sizeVec3 = sizeof(SM::Vector3);
	UINT sizeFloat = sizeof(float);
	UINT numGridCells = m_SimParams.GridDim * m_SimParams.GridDim * m_SimParams.GridDim;

	// Create UAV
	auto CreateBufferUAV = [&](ID3D12Resource* pBuffer, UINT numElements, UINT stride, int heapIdx)
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = numElements;
			uavDesc.Buffer.StructureByteStride = stride;
			uavDesc.Buffer.CounterOffsetInBytes = 0;
			uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

			CD3DX12_CPU_DESCRIPTOR_HANDLE handle = hHandle;
			handle.Offset(heapIdx, m_CbvSrvUavDescriptorSize);

			device->CreateUnorderedAccessView(pBuffer, nullptr, &uavDesc, handle);
		};

	CreateBufferUAV(m_PosPred.Get(), m_NumParticles, sizeVec3, UAV_IDX_POS_PRED);
	CreateBufferUAV(m_PosOld.Get(), m_NumParticles, sizeVec3, UAV_IDX_POS_OLD);
	CreateBufferUAV(m_VelIn.Get(), m_NumParticles, sizeVec3, UAV_IDX_VEL_IN);
	CreateBufferUAV(m_VelOut.Get(), m_NumParticles, sizeVec3, UAV_IDX_VEL_OUT);
	CreateBufferUAV(m_Density.Get(), m_NumParticles, sizeof(float), UAV_IDX_DENSITY);
	CreateBufferUAV(m_Lambda.Get(), m_NumParticles, sizeof(float), UAV_IDX_LAMBDA);
	CreateBufferUAV(m_DeltaPos.Get(), m_NumParticles, sizeVec3, UAV_IDX_DELTAPOS);
	CreateBufferUAV(m_Vorticity.Get(), m_NumParticles, sizeVec3, UAV_IDX_VORTICITY);
	CreateBufferUAV(m_GridIndices.Get(), numGridCells, sizeof(UINT) * 2, UAV_IDX_GRID_INDICES);
	CreateBufferUAV(m_SortedIndices.Get(), m_NumParticles, sizeof(UINT), UAV_IDX_SORTED_INDICES);

	CreateBufferUAV(m_TempPosPred.Get(), m_NumParticles, sizeof(SM::Vector3), UAV_IDX_TEMP_POS);
	CreateBufferUAV(m_TempPosOld.Get(), m_NumParticles, sizeof(SM::Vector3), UAV_IDX_TEMP_OLD);
	CreateBufferUAV(m_TempVel.Get(), m_NumParticles, sizeof(SM::Vector3), UAV_IDX_TEMP_VEL);

	// Create SRV
	auto CreateBufferSRV = [&](ID3D12Resource* pBuffer, int heapIdx) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.NumElements = m_NumParticles;
		srvDesc.Buffer.StructureByteStride = sizeof(SM::Vector3);

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = hHandle;
		handle.Offset(heapIdx, m_CbvSrvUavDescriptorSize);
		device->CreateShaderResourceView(pBuffer, &srvDesc, handle);
		};

	CreateBufferSRV(m_PosPred.Get(), SRV_IDX_POS_PRED); // t0
	CreateBufferSRV(m_PosOld.Get(), SRV_IDX_POS_OLD);  // t1
	CreateBufferSRV(m_VelIn.Get(), SRV_IDX_VEL_IN);   // t2

	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.NumElements = m_NumParticles;
		srvDesc.Buffer.StructureByteStride = sizeof(UINT);

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = hHandle;
		handle.Offset(SRV_IDX_INDICES, m_CbvSrvUavDescriptorSize);
		device->CreateShaderResourceView(m_SortedIndices.Get(), &srvDesc, handle);
	}
}

void SphSolver::CreateGlobalRootSignature(ID3D12Device* device)
{
	CD3DX12_ROOT_PARAMETER1 rootParameters[4];

	rootParameters[0].InitAsConstants(sizeof(SimParams) / 4, 0);
	rootParameters[1].InitAsConstants(sizeof(SortConstants) / 4, 1);

	CD3DX12_DESCRIPTOR_RANGE1 uavRange;
	uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 10, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	rootParameters[2].InitAsDescriptorTable(1, &uavRange);

	CD3DX12_DESCRIPTOR_RANGE1 srvRange;
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE); // t0 ~ t3
	rootParameters[3].InitAsDescriptorTable(1, &srvRange);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(_countof(rootParameters), rootParameters);
	ComPtr<ID3DBlob> signatureBlob, errorBlob;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signatureBlob, &errorBlob);

	if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); ThrowIfFailed(hr); }

	ThrowIfFailed(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_GlobalRootSig)));
	Helpers::SetDebugName(m_GlobalRootSig.Get(), "m_GlobalRootSig");
}

void SphSolver::CreatePermuteRootSignature(ID3D12Device* device)
{
	CD3DX12_ROOT_PARAMETER1 rootParameters[3];
	rootParameters[0].InitAsConstants(sizeof(SimParams) / 4, 0);

	CD3DX12_DESCRIPTOR_RANGE1 uavRange;
	uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	rootParameters[1].InitAsDescriptorTable(1, &uavRange);

	CD3DX12_DESCRIPTOR_RANGE1 srvRange;
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	rootParameters[2].InitAsDescriptorTable(1, &srvRange);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(_countof(rootParameters), rootParameters);

	ComPtr<ID3DBlob> signatureBlob, errorBlob;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); ThrowIfFailed(hr); }

	ThrowIfFailed(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_PermuteRootSig)));
	Helpers::SetDebugName(m_PermuteRootSig.Get(), "m_PermuteRootSig");
}

void SphSolver::CreateComputePSO(ID3D12Device* device, ShaderHelper* helper,
	std::wstring shaderFile, ComPtr<ID3D12PipelineState>& outPSO, ComPtr<ID3D12RootSignature>& sig)
{
	ComPtr<IDxcBlob> csBlob = helper->Compile(m_ShaderBaseName, shaderFile, L"main", L"cs_6_0");

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = sig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&outPSO));
}

void SphSolver::RunBitonicSort(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->SetPipelineState(m_SortPSO.Get());

	UINT groups = (m_NumParticles + 255) / 256;
	for (UINT blockSize = 2; blockSize <= m_NumParticles; blockSize <<= 1) {
		for (UINT stride = blockSize >> 1; stride > 0; stride >>= 1) {
			SortConstants sortConsts = { blockSize, stride, 0, 0 };

			cmdList->SetComputeRoot32BitConstants(1, 4, &sortConsts, 0);

			cmdList->Dispatch(groups, 1, 1);

			auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_SortedIndices.Get());
			cmdList->ResourceBarrier(1, &barrier);
		}
	}
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

		ImGui::Checkbox("Reset", &m_bReset);

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
		ImGui::Checkbox("Enable Move(Shift)", &m_bWallMove);
		if (m_bWallMove)
		{
			ImGui::DragFloat("Wall Speed", &m_WallSpeed, 0.1f);
			ImGui::DragFloat("Wall Amplitude", &m_WallAmplitude, 0.1f);
		}

		ImGui::SeparatorText("Advanced Stability");
		ImGui::DragFloat("CFM Epsilon", &m_SimParams.Epsilon, 10.0f, 0.0f, 1e6f, "%.0f");
		ImGui::DragFloat("Tensile K", &m_SimParams.K, 1e-6f, 0.0f, 1.0f, "%.6f");
		ImGui::DragFloat("Tensile N", &m_SimParams.N, 0.1f, 1.0f, 10.0f);
		ImGui::DragFloat("DqScale", &m_SimParams.DqScale, 0.1f, 0.0f, 1.0f);
		ImGui::DragFloat("Vorticity", &m_SimParams.VorticityEpsilon, 1e-6f, 0.0f, 1.0f, "%.6f");
	}
}

void SphSolver::ResetSimulation(ID3D12GraphicsCommandList* cmdList)
{
	UploadData(cmdList, m_PosPred, m_InitPos, m_NumParticles, sizeof(SM::Vector3));
	UploadData(cmdList, m_VelOut, m_Zero3, m_NumParticles, sizeof(SM::Vector3));
	UploadData(cmdList, m_Density, m_Zero1, m_NumParticles, sizeof(float));
}

template <typename T>
void SphSolver::UploadData(ID3D12GraphicsCommandList* cmdList, ComPtr<ID3D12Resource>& buffer, std::vector<T>& data, UINT count, UINT stride)
{
	auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(count * stride, D3D12_RESOURCE_FLAG_NONE);

	ThrowIfFailed(m_pDevice->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&uploadBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_ResetUploadBuffer.GetAddressOf())));

	auto barrierToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
		buffer.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COPY_DEST
	);
	cmdList->ResourceBarrier(1, &barrierToCopy);

	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = data.data();
	subResourceData.RowPitch = stride;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	UpdateSubresources<1>(cmdList, buffer.Get(), m_ResetUploadBuffer.Get(), 0, 0, 1, &subResourceData);

	auto barrierToRead = CD3DX12_RESOURCE_BARRIER::Transition(
		buffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS
	);
	cmdList->ResourceBarrier(1, &barrierToRead);
}