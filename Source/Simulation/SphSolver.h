#pragma once

class ShaderHelper;

class SphSolver
{
public:
	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps);

	void Run(ID3D12GraphicsCommandList* cmdList);
	void OnGui();

	// [Getters]
	UINT GetNumParticles() const { return m_NumParticles; }
	UINT GetNumDiffuseParticles() const { return m_DiffuseParams.MaxDiffuseParticles; }
	ID3D12Resource* GetParticleBuffer() const { return m_PosPred.Get(); }
	ID3D12Resource* GetDensityBuffer() const { return m_Density.Get(); }
	ID3D12DescriptorHeap* GetGlobalHeap() const { return m_GlobalHeap.Get(); }
	ID3D12Resource* GetDrawArgsBuffer() const { return m_DrawArgsBuffer.Get(); }
	ID3D12CommandSignature* GetDrawCommandSignature() const { return m_DrawSig.Get(); }
	UINT GetPositionSrvIndex() const { return SRV_IDX_POS_PRED; }
	UINT GetDensitySrvIndex() const { return SRV_IDX_DENSITY_RENDER; }
	UINT GetDiffuseSrvIndex() const { return SRV_IDX_DIFFUSE_PARTICLES_RENDER; }
	UINT GetDescriptorSize() const { return m_CbvSrvUavDescriptorSize; }
	ID3D12Resource* GetDiffuseParticleBuffer() const { return m_DiffuseParticles.Get(); }

	bool m_bSolveDiffuseParticles = true;
	float m_FixedDt = 1.0f / 60.0f;
private:
	struct SimParams
	{
		UINT NumParticles;
		float DeltaTime;
		float CellSize = 0.2f;
		UINT GridDim = 128;

		float Mass = 1.0f;
		float RestDensity = 1000.0f;
		float Viscosity = 0.05f;
		float GravityY = -9.81f;

		SM::Vector2 BoxX;
		SM::Vector2 BoxY;

		SM::Vector2 BoxZ;
		float Epsilon = 600.0f;
		float K = 0.001f;

		float N = 4.0f;
		float DqScale = 0.3f;
		float VorticityEpsilon = 0.3f;
		float ExternalAccel = 0.0f;

		float JitterFactor = 0.005f;
		UINT NumPartialSums;
	} m_SimParams;

	int m_Substeps = 1;
	int m_Iterations = 2;
	float m_Spacing = 0.10f;

	bool m_bWallMove = false;
	float m_TotalTime = 0.0f;
	float m_OriginMinX = 0.0f;
	float m_WallSpeed = 2.0f;
	float m_WallAmplitude = 3.0f;

	bool m_bSingleDamBreak = false;
	bool m_bDoubleDamBreak = false;
	bool m_bCornerDamBreak = true;

	UINT m_Groups = 0;
	int m_X = 200;
	int m_Y = 50;
	int m_Z = 50;
	UINT m_NumParticles = m_X * m_Y * m_Z;

	std::vector<SM::Vector3> m_InitPos;
	std::vector<float>		 m_Zero1;
	std::vector<SM::Vector3> m_Zero3;

	ComPtr<ID3D12Resource> m_ResetUploadBuffer;

	template <typename T>
	void UploadData(ID3D12GraphicsCommandList* cmdList, ComPtr<ID3D12Resource>& buffer, std::vector<T>& data, UINT count, UINT stride);

	enum HeapDescriptors
	{
		// Main UAVs Solver
		UAV_IDX_POS_PRED = 0,   // u0
		UAV_IDX_POS_OLD,
		UAV_IDX_VEL_IN,
		UAV_IDX_VEL_OUT,
		UAV_IDX_DENSITY,
		UAV_IDX_LAMBDA,
		UAV_IDX_DELTAPOS,
		UAV_IDX_VORTICITY,
		UAV_IDX_GRID_INDICES,
		UAV_IDX_SORTED_INDICES,
		UAV_IDX_CELL_COUNT,
		UAV_IDX_PARTICLE_CELL_INFO,
		UAV_IDX_CELL_START,
		UAV_IDX_PARTIAL_SUM,

		// Diffuse Particles
		UAV_IDX_DIFFUSE_PARTICLES,
		UAV_IDX_DIFFUSE_PARTICLES_COMPACTED,
		UAV_IDX_COUNTERS,
		UAV_IDX_DISPATCH_ARGS,
		UAV_IDX_DRAW_ARGS,

		// Temp UAVs
		UAV_IDX_TEMP_POS,    // u0
		UAV_IDX_TEMP_OLD,    // u1
		UAV_IDX_TEMP_VEL,    // u2

		// Source SRVs
		SRV_IDX_POS_PRED,    // t0
		SRV_IDX_POS_OLD,     // t1
		SRV_IDX_VEL_IN,      // t2
		SRV_IDX_INDICES,     // t3
		SRV_IDX_VEL_OUT,     // t4
		SRV_IDX_GRID_INDICES,// t5
		SRV_IDX_LAMBDA,		 // t6
		SRV_IDX_VORTICITY,   // t7

		SRV_IDX_CELL_COUNT,
		SRV_IDX_PARTICLE_CELL_INFO,
		SRV_IDX_CELL_START,
		SRV_IDX_PARTIAL_SUM,

		SRV_IDX_DENSITY_RENDER,
		SRV_IDX_DIFFUSE_PARTICLES_RENDER,

		DESCRIPTOR_COUNT
	};

	enum RootParamIdx {
		RP_CB_SIM_PARAMS = 0,
		RP_CB_SORT_OR_DIFFUSE_PARAMS = 1,
		RP_DT_UAV = 2,
		RP_DT_SRV = 3,
		RP_DT_UAV_DIFFUSE = 4
	};

	enum BufferBarrierIndex
	{
		UAV_BARRIER_POS_PRED = 0,
		TRANS_SRV_POS_PRED,
		TRANS_UAV_POS_PRED, 

		TRANS_SRV_POS_OLD,
		TRANS_UAV_POS_OLD,

		TRANS_SRV_VEL_IN,
		TRANS_UAV_VEL_IN,

		UAV_BARRIER_VEL_OUT,
		TRANS_SRV_VEL_OUT,
		TRANS_UAV_VEL_OUT,

		UAV_BARRIER_SORTED_INDICIES,
		TRANS_SRV_SORTED_INDICIES,
		TRANS_UAV_SORTED_INDICIES,

		UAV_BARRIER_GRID_INDICES,
		TRANS_SRV_GRID_INDICES,
		TRANS_UAV_GRID_INDICES,

		UAV_BARRIER_LAMBDA,
		TRANS_SRV_LAMBDA,
		TRANS_UAV_LAMBDA,

		UAV_BARRIER_DENSITY,

		UAV_BARRIER_DELTAPOS,

		UAV_BARRIER_VORTICITY,
		TRANS_SRV_VORTICITY,
		TRANS_UAV_VORTICITY,

		UAV_BARRIER_DIFFUSE,
		TRANS_UAV_DIFFUSE,
		TRANS_SRV_DIFFUSE,

		UAV_BARRIER_DIFFUSE_COMPACT,

		UAV_BARRIER_COUNTERS,

		TRANS_UAV_DISPATCH_ARGS,
		TRANS_INDIRECT_DISPATCH_ARGS,
		TRANS_UAV_DRAW_ARGS,
		TRANS_INDIRECT_DRAW_ARGS,

		UAV_BARRIER_CELL_COUNT,
		TRANS_SRV_CELL_COUNT,
		TRANS_UAV_CELL_COUNT,
		UAV_BARRIER_PARTICLE_CELL_INFO,
		TRANS_SRV_PARTICLE_CELL_INFO,
		TRANS_UAV_PARTICLE_CELL_INFO,
		UAV_BARRIER_CELL_START,
		TRANS_SRV_CELL_START,
		TRANS_UAV_CELL_START,
		UAV_BARRIER_PARTIAL_SUM,
		TRANS_SRV_PARTIAL_SUM,
		TRANS_UAV_PARTIAL_SUM,

		NUM_BARRIERS
	};

	CD3DX12_RESOURCE_BARRIER m_AllBarriers[NUM_BARRIERS];

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

	ComPtr<ID3D12Resource> m_CellCount;
	ComPtr<ID3D12Resource> m_ParticleCellInfo;
	ComPtr<ID3D12Resource> m_CellStart;
	ComPtr<ID3D12Resource> m_PartialSum;

	ComPtr<ID3D12Resource> m_TempPosPred;
	ComPtr<ID3D12Resource> m_TempPosOld;
	ComPtr<ID3D12Resource> m_TempVel;
	ComPtr<ID3D12Resource> m_SortedIndices;

	// For DiffuseParticles
	struct DiffuseParams
	{
		UINT MaxDiffuseParticles = 64 * 64 * 64;
		float DiffuseDeltaTime = 1.0f / 60.0f;
		float TrappedAirMin = 5.0f;
		float TrappedAirMax = 20.0f;

		float K_Ta = 100.0f;
		float WaveCrestMin = 0.7f;
		float WaveCrestMax = 0.9f;
		float K_Wc = 200.0f;

		float EnergyMin = 10.0f;
		float EnergyMax = 20.0f;
		float MaxLifeTime = 2.0f;
		float CellSizeScale = 1.0f;

		float BubbleScale = 10.0f;
		float BubbleScaleChangeSpeed = 10.0f;
		int SprayClassifyMaxNeighbours = 2;
		int BubbleClassifyMinNeighbours = 8;

		float BubbleBuoyancy = 2.0f;
		float FluidAccelMul = 20.0f;
		int GeneratePerFrame = 32;
	} m_DiffuseParams;

	struct DiffuseParticle
	{
		SM::Vector4 PositionLife;
		SM::Vector4 VelocityScale;
	};
	std::vector<UINT> m_CounterValues = { 0, 0 };
	std::vector<DiffuseParticle> m_DiffuseParticlesData = {};
	ComPtr<ID3D12Resource> m_DiffuseParticles;
	ComPtr<ID3D12Resource> m_DiffuseParticlesCompacted;
	ComPtr<ID3D12Resource> m_Counters;

	ComPtr<ID3D12Resource> m_DispatchArgsBuffer;
	ComPtr<ID3D12Resource> m_DrawArgsBuffer;

	struct DispatchIndirectCommand
	{
		UINT ThreadGroupCountX;
		UINT ThreadGroupCountY;
		UINT ThreadGroupCountZ;
	};
	ComPtr<ID3D12CommandSignature> m_DispatchSig;

	struct DrawIndirectCommand
	{
		UINT VertexCountPerInstance;
		UINT InstanceCount;
		UINT StartVertexLocation;
		UINT StartInstanceLocation;
	};
	ComPtr<ID3D12CommandSignature> m_DrawSig;

	UINT m_CbvSrvUavDescriptorSize = 0;
	ComPtr<ID3D12DescriptorHeap> m_GlobalHeap;

	ComPtr<ID3D12RootSignature> m_GlobalRootSig;
	ComPtr<ID3D12RootSignature> m_PermuteRootSig;
	ComPtr<ID3D12RootSignature> m_DiffuseRoogSig;

	ComPtr<ID3D12PipelineState> m_IntegrationPSO;
	ComPtr<ID3D12PipelineState> m_ClearGridPSO;
	ComPtr<ID3D12PipelineState> m_BuildGridPSO;
	ComPtr<ID3D12PipelineState> m_BitonicSortLdsPSO;
	ComPtr<ID3D12PipelineState> m_BitonicSortPSO;
	ComPtr<ID3D12PipelineState> m_PermuteDataPSO;

	ComPtr<ID3D12PipelineState> m_DensityLambdaPSO;
	ComPtr<ID3D12PipelineState> m_DeltaPosPSO;
	ComPtr<ID3D12PipelineState> m_ConstraintPSO;
	ComPtr<ID3D12PipelineState> m_VorticityPSO;
	ComPtr<ID3D12PipelineState> m_UpdateVelocityPSO;

	ComPtr<ID3D12PipelineState> m_ClearCounterPSO;
	ComPtr<ID3D12PipelineState> m_DiffuseGenerationPSO;
	ComPtr<ID3D12PipelineState> m_BuildDispatchArgsPSO;
	ComPtr<ID3D12PipelineState> m_UpdateDiffusePSO;
	ComPtr<ID3D12PipelineState> m_CopyDiffusePSO;
	ComPtr<ID3D12PipelineState> m_BuildDrawArgsPSO;

	ComPtr<ID3D12PipelineState> m_ClearCellCountPSO;
	ComPtr<ID3D12PipelineState> m_CountParticlesPerCellPSO;
	ComPtr<ID3D12PipelineState> m_PrefixSumLocalPSO;
	ComPtr<ID3D12PipelineState> m_PrefixSumBlockPSO;
	ComPtr<ID3D12PipelineState> m_PrefixSumFinalAddPSO;
	ComPtr<ID3D12PipelineState> m_BuildSortedIndicesPSO;

	ID3D12Device* m_pDevice = nullptr;

	void UpdateInputs();
	void ResetSimulation(ID3D12GraphicsCommandList* cmdList);

	void BitonicSort(ID3D12GraphicsCommandList* cmdList);
	void CountingSort(ID3D12GraphicsCommandList* cmdList);
	void PermuteAndCopyBack(ID3D12GraphicsCommandList* cmdList);
	void BuildGrid(ID3D12GraphicsCommandList* cmdList);

	void InitBarriers();
	void CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, std::vector<ComPtr<ID3D12Resource>>& tempUploadBuffers);
	void ResetParticlePos();
	void CreateAllViews(ID3D12Device* device);
	void CreateComputePSO(ID3D12Device* device, ShaderHelper* helper,
		std::wstring shaderFile, ComPtr<ID3D12PipelineState>& outPSO, 
		ComPtr<ID3D12RootSignature>& sig, std::wstring shaderPath);
	void CreateGlobalRootSignature(ID3D12Device* device);
	void CreatePermuteRootSignature(ID3D12Device* device);
	void CreateDiffuseRootSignature(ID3D12Device* device);
	void CreateCommandSignature(ID3D12Device* device);
};