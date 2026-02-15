#pragma once

class ShaderHelper;

class SphSolver
{
public:
	struct SimParams
	{
		UINT NumParticles;
		float DeltaTime = 1.0f / 120.0f;
		float CellSize = 0.2f;
		UINT GridDim = 128;

		float Mass = 1.0f;
		float RestDensity = 1000.0f;
		float Viscosity = 0.10f;
		float GravityY = -9.81f;

		SM::Vector2 BoxX;
		SM::Vector2 BoxY;

		SM::Vector2 BoxZ;
		float Epsilon = 600.0f;
		float K = 0.001f;

		float N = 4.0f;
		float DqScale = 0.3f;
		float VorticityEpsilon = 0.4f;
		float ExternalAccel = 0.0f;

		float JitterFactor = 0.005f;
	} m_SimParams;

	bool m_bSolveDiffuseParticles = true;

public:
	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps);

	void UpdateInputs();
	void Run(ID3D12GraphicsCommandList* cmdList);
	void OnGui();
	void ResetSimulation(ID3D12GraphicsCommandList* cmdList);

	// [Getters]
	UINT GetNumParticles() const { return m_NumParticles; }
	const SimParams& GetSimParams() const { return m_SimParams; }
	ID3D12Resource* GetParticleBuffer() const { return m_PosPred.Get(); }
	ID3D12DescriptorHeap* GetGlobalHeap() const { return m_GlobalHeap.Get(); }
	ID3D12Resource* GetDrawArgsBuffer() const { return m_DrawArgsBuffer.Get(); }
	ID3D12CommandSignature* GetDrawCommandSignature() const { return m_DrawSig.Get(); }
	UINT GetPositionSrvIndex() const { return SRV_IDX_POS_PRED; }
	UINT GetDensitySrvIndex() const { return SRV_IDX_DENSITY_RENDER; }
	UINT GetDiffuseSrvIndex() const { return SRV_IDX_DIFFUSE_PARTICLES_RENDER; }
	UINT GetDescriptorSize() const { return m_CbvSrvUavDescriptorSize; }

	void ToggleWallMovement() { m_bWallMove = !m_bWallMove; }

	float m_FixedDt = 1.0f / 60.0f;
private:
	int m_Substeps = 1;
	int m_Iterations = 2;
	bool m_bWallMove = false;
	float m_TotalTime = 0.0f;
	float m_OriginMinX = 0.0f;
	float m_WallSpeed = 2.0f;
	float m_WallAmplitude = 3.0f;
	float m_Spacing = 0.1f;

	bool m_bSingleDamBreak = false;
	bool m_bDoubleDamBreak = false;
	bool m_bCornerDamBreak = true;

	// Data
	UINT m_NumParticles = 0;

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
		UAV_IDX_SORTED_INDICES, // u9

		// Diffuse Particles
		UAV_IDX_DIFFUSE_PARTICLES,           // u10
		UAV_IDX_DIFFUSE_PARTICLES_COMPACTED, // u11
		UAV_IDX_COUNTERS,					 // u12
		UAV_IDX_DISPATCH_ARGS,				 // u13
		UAV_IDX_DRAW_ARGS,					 // u14

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

		SRV_IDX_DENSITY_RENDER,
		SRV_IDX_DIFFUSE_PARTICLES_RENDER,

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

	// For DiffuseParticles
	struct DiffuseParams
	{
		UINT MaxDiffuseParticles = 64 * 64 * 64;
		float DiffuseDeltaTime = 1.0f / 60.0f;
		float TrappedAirMin = 5.0f;
		float TrappedAirMax = 20.0f;

		float K_Ta = 100.0f;
		float WaveCrestMin = 0.6f;
		float WaveCrestMax = 0.9f;
		float K_Wc = 100.0f;

		float EnergyMin = 10.0f;
		float EnergyMax = 20.0f;
		float MaxLifeTime = 20.0f;
		float CellSizeScale = 1.0f;

		float BubbleScale = 4.0f;
		float BubbleScaleChangeSpeed = 8.0f;
		int SprayClassifyMaxNeighbours = 2;
		int BubbleClassifyMinNeighbours = 8;

		float BubbleBuoyancy = 1.5f;
	} m_DiffuseParams;

	struct DiffuseParticle
	{
		SM::Vector4 PositionLife;
		SM::Vector4 VelocityScale;
	};
	UINT m_ZeroValues[2] = { 0, 0 };
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

	std::wstring m_ShaderBaseName = L"./Shaders/Simulation/";

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

	ComPtr<ID3D12PipelineState> m_DiffuseGenerationPSO;
	ComPtr<ID3D12PipelineState> m_BuildDispatchArgsPSO;
	ComPtr<ID3D12PipelineState> m_UpdateDiffusePSO;
	ComPtr<ID3D12PipelineState> m_CopyDiffusePSO;
	ComPtr<ID3D12PipelineState> m_BuildDrawArgsPSO;

	ID3D12Device* m_pDevice = nullptr;

	void RunBitonicSort(ID3D12GraphicsCommandList* cmdList);

	void CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, std::vector<ComPtr<ID3D12Resource>>& tempUploadBuffers);
	void ResetParticlePos();
	void CreateAllViews(ID3D12Device* device);
	void CreateComputePSO(ID3D12Device* device, ShaderHelper* helper,
		std::wstring shaderFile, ComPtr<ID3D12PipelineState>& outPSO, ComPtr<ID3D12RootSignature>& sig);
	void CreateGlobalRootSignature(ID3D12Device* device);
	void CreatePermuteRootSignature(ID3D12Device* device);
	void CreateDiffuseRootSignature(ID3D12Device* device);
	void CreateCommandSignature(ID3D12Device* device);
};