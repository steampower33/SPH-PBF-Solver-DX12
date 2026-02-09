#pragma once

class ShaderHelper;

class SphSolver
{
public:
	struct SimParams
	{
		float DeltaTime = 1.0f / 144.0f;
		UINT NumParticles;
		float CellSize = 0.1f;
		UINT GridDim = 128;

		float Mass = 0.1f;
		float RestDensity = 1000.0f;
		float Viscosity = 0.03f;
		float GravityY = -9.81f;

		SM::Vector2 BoxX = { -3.0f, 4.0f };
		SM::Vector2 BoxY = { 0.0f, 4.0f };
		SM::Vector2 BoxZ = { -1.5f, 2.0f };

		float Epsilon = 30000.0f;
		float K = 0.0000050f;
		float N = 8.0f;
		float DqScale = 0.2f;
		float VorticityEpsilon = 0.000005f;
		float ExternalAccel = 0.0f;
	};

public:
	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps);

	void UpdateInputs();
	void Run(ID3D12GraphicsCommandList* cmdList);
	void OnGui();
	void ResetSimulation(ID3D12GraphicsCommandList* cmdList);

	// [Getters]
	ID3D12DescriptorHeap* GetSrvHeap() const { return m_SrvHeap.Get(); }
	UINT GetNumParticles() const { return m_NumParticles; }
	const SimParams& GetSimParams() const { return m_SimParams; }
	ID3D12Resource* GetParticleBuffer() const { return m_PosPred.Get(); } // Visualization용

	void ToggleWallMovement() { m_WallMove = !m_WallMove; }

	int m_MaxSteps = 2;
private:
	// Simulation State
	bool m_WallMove = false;
	float m_TotalTime = 0.0f;
	float m_OriginMinX = 0.0f;
	float m_WallSpeed = 2.5f;
	float m_WallAmplitude = 2.0f;
	int m_Iterations = 4;
	bool m_Reset = false;

	// Data
	SimParams m_SimParams;
	UINT m_NumParticles = 0;

	std::vector<SM::Vector3> m_InitPos;
	std::vector<float>		 m_Zero1;
	std::vector<SM::Vector3> m_Zero3;

	ComPtr<ID3D12Resource> m_ResetUploadBuffer;

	template <typename T>
	void UploadData(ID3D12GraphicsCommandList* cmdList, ComPtr<ID3D12Resource>& buffer, std::vector<T>& data, UINT count, UINT stride);

	enum HeapDescriptors
	{
		// [1] Main UAVs Solver
		UAV_IDX_POS_PRED = 0, // u0
		UAV_IDX_POS_OLD,
		UAV_IDX_VEL_IN,
		UAV_IDX_VEL_OUT,
		UAV_IDX_DENSITY,
		UAV_IDX_LAMBDA,
		UAV_IDX_DELTAPOS,
		UAV_IDX_VORTICITY,
		UAV_IDX_GRID_INDICES,
		UAV_IDX_SORTED_INDICES, // u8

		// [2] Temp UAVs
		UAV_IDX_TEMP_POS,    // u0
		UAV_IDX_TEMP_OLD,    // u1
		UAV_IDX_TEMP_VEL,    // u2

		// [3] Source SRVs
		SRV_IDX_POS_PRED,    // t0
		SRV_IDX_POS_OLD,     // t1
		SRV_IDX_VEL_IN,      // t2
		SRV_IDX_INDICES,     // t3

		DESCRIPTOR_COUNT
	};

	struct SortConstants {
		UINT BlockSize;
		UINT Stride;
		UINT Padding[2];
	};

	// SoA Buffers
	ComPtr<ID3D12Resource> m_PosPred;
	ComPtr<ID3D12Resource> m_PosOld;
	ComPtr<ID3D12Resource> m_VelIn;
	ComPtr<ID3D12Resource> m_VelOut;
	ComPtr<ID3D12Resource> m_Density;
	ComPtr<ID3D12Resource> m_Lambda;
	ComPtr<ID3D12Resource> m_DeltaPos;
	ComPtr<ID3D12Resource> m_Vorticity;
	ComPtr<ID3D12Resource> m_GridIndices;

	ComPtr<ID3D12Resource> m_TempPosPred;
	ComPtr<ID3D12Resource> m_TempPosOld;
	ComPtr<ID3D12Resource> m_TempVel;
	ComPtr<ID3D12Resource> m_SortedIndices;

	UINT m_CbvSrvUavDescriptorSize = 0;
	ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
	ComPtr<ID3D12DescriptorHeap> m_UavHeap;

	ComPtr<ID3D12RootSignature> m_GlobalRootSig;
	ComPtr<ID3D12RootSignature> m_PermuteRootSig;

	std::wstring m_ShaderBaseName = L"./Shaders/Simulation/";

	ComPtr<ID3D12PipelineState> m_IntegrationPSO;
	ComPtr<ID3D12PipelineState> m_ClearGridPSO;
	ComPtr<ID3D12PipelineState> m_BuildGridPSO;
	ComPtr<ID3D12PipelineState> m_SortPSO;
	ComPtr<ID3D12PipelineState> m_PermuteDataPSO;

	ComPtr<ID3D12PipelineState> m_DensityLambdaPSO;
	ComPtr<ID3D12PipelineState> m_DeltaPosPSO;
	ComPtr<ID3D12PipelineState> m_ConstraintPSO;
	ComPtr<ID3D12PipelineState> m_VorticityPSO;
	ComPtr<ID3D12PipelineState> m_UpdateVelocityPSO;

	ID3D12Device* m_pDevice = nullptr;

	void RunBitonicSort(ID3D12GraphicsCommandList* cmdList);

	void CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, std::vector<ComPtr<ID3D12Resource>>& tempUploadBuffers);
	void CreateRenderSrvHeap(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
	void CreateAllViews(ID3D12Device* device);
	void CreateComputePSO(ID3D12Device* device, ShaderHelper* helper,
		std::wstring shaderFile, ComPtr<ID3D12PipelineState>& outPSO, ComPtr<ID3D12RootSignature>& sig);
	void CreateGlobalRootSignature(ID3D12Device* device);
	void CreatePermuteRootSignature(ID3D12Device* device);
};