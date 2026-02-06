#pragma once

class ShaderHelper;

struct Particle
{
	SM::Vector3 Position;
	float Density;      // Align / Density
	SM::Vector3 Velocity;
	float Pressure;     // Align / Pressure
	SM::Vector3 OldPosition;
	float Padding;
};

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
		float K = 0.0000050f; // Tensile K
		float N = 8.0f;       // Tensile N
		float DqScale = 0.2f; // Tensile dQ
		float VorticityEpsilon = 0.000005f;
		float ExternalAccel = 0.0f;
	};

public:
	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper);
	void Update(ID3D12GraphicsCommandList* cmdList);

	void OnGui();

	// [Getters]
	ID3D12Resource* GetParticleBuffer() const { return m_ParticleBuffer.Get(); }
	ID3D12DescriptorHeap* GetSrvHeap() const { return m_SrvHeap.Get(); }
	UINT GetNumParticles() const { return m_NumParticles; }
	const SimParams& GetSimParams() const { return m_SimParams; }

	void ToggleWallMovement() { m_WallMove = !m_WallMove; }

private:
	std::wstring m_ShaderBaseName = L"./Shaders/Simulation/";

	// Core Resources
	SimParams m_SimParams;

	ComPtr<ID3D12Resource> m_ParticleBuffer;
	ComPtr<ID3D12Resource> m_UploadBuffer;

	ComPtr<ID3D12DescriptorHeap> m_SrvHeap; // SRV (t0~)
	ComPtr<ID3D12DescriptorHeap> m_UavHeap; // UAV (u0~)

	UINT m_NumParticles = 0;

	// Wall / Interaction State
	bool m_WallMove = false;
	float m_TotalTime = 0.0f;
	float m_OriginMinX = 0.0f;
	float m_WallSpeed = 2.5f;
	float m_WallAmplitude = 2.0f;
	int m_Iterations = 4;

	void InitParticles(std::vector<Particle>& outParticles);
	void CreateUavHeap(ID3D12Device* device);

	// Integration & Compute Core
	ComPtr<ID3D12RootSignature> m_ComputeRootSig;
	ComPtr<ID3D12PipelineState> m_IntegrationPSO;

	void CreateComputeRootSignature(ID3D12Device* device);
	void CreateComputePSO(ID3D12Device* device, ShaderHelper* helper);

	// Bitonic Sort
	struct SortConstants {
		UINT BlockSize;
		UINT Stride;
		UINT Padding[2];
	};

	ComPtr<ID3D12RootSignature> m_SortRootSig;
	ComPtr<ID3D12PipelineState> m_SortPSO;

	void RunBitonicSort(ID3D12GraphicsCommandList* cmdList);
	void CreateSortRootSignature(ID3D12Device* device);
	void CreateSortPSO(ID3D12Device* device, ShaderHelper* helper);

	// Grid Construction (Spatial Hashing)
	ComPtr<ID3D12Resource> m_GridIndicesBuffer;
	ComPtr<ID3D12Resource> m_GridIndicesUpload;

	ComPtr<ID3D12RootSignature> m_GridMapRootSig;
	ComPtr<ID3D12PipelineState> m_ClearGridPSO;
	ComPtr<ID3D12PipelineState> m_BuildGridPSO;

	void CreateGridMapRootSignature(ID3D12Device* device);
	void CreateGridMapPSO(ID3D12Device* device, ShaderHelper* helper);

	// PBF Solver (Density, Lambda, Constraint, Vorticity)
	// Buffers
	ComPtr<ID3D12Resource> m_DensityBuffer;  ComPtr<ID3D12Resource> m_DensityUpload;
	ComPtr<ID3D12Resource> m_LambdaBuffer;   ComPtr<ID3D12Resource> m_LambdaUpload;
	ComPtr<ID3D12Resource> m_VorticityBuffer; ComPtr<ID3D12Resource> m_VorticityUpload;

	// RootSignature (Shared for PBF steps)
	ComPtr<ID3D12RootSignature> m_PbfSolverRootSig;

	// PSOs
	ComPtr<ID3D12PipelineState> m_DensityLambdaPSO;
	ComPtr<ID3D12PipelineState> m_DeltaPosPSO;
	ComPtr<ID3D12PipelineState> m_ConstraintPSO;
	ComPtr<ID3D12PipelineState> m_VorticityPSO;
	ComPtr<ID3D12PipelineState> m_UpdateVelocityPSO;

	void CreatePbfSolverRootSignature(ID3D12Device* device);
	void CreateDensityLambdaPSO(ID3D12Device* device, ShaderHelper* helper);
	void CreateDeltaPosPSO(ID3D12Device* device, ShaderHelper* helper);
	void CreateConstraintPSO(ID3D12Device* device, ShaderHelper* helper);
	void CreateVorticityPSO(ID3D12Device* device, ShaderHelper* helper);
	void CreateUpdateVelocityPSO(ID3D12Device* device, ShaderHelper* helper);
};