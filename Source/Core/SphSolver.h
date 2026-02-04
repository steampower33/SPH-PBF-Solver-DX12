#pragma once

class ShaderHelper;

struct Particle
{
	SM::Vector3 Position;
	float Density;      // Padding for alignment/density
	SM::Vector3 Velocity;
	float Pressure;     // Padding for alignment/pressure
	SM::Vector3 OldPosition;
	float Padding;
};

class SphSolver
{
public:
	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper);

	void Update(ID3D12GraphicsCommandList* cmdList);

	void RunBitonicSort(ID3D12GraphicsCommandList* cmdList);

	ID3D12Resource* GetParticleBuffer() const { return m_ParticleBuffer.Get(); }
	UINT GetNumParticles() const { return m_NumParticles; }
	ID3D12DescriptorHeap* GetSrvHeap() const { return m_SrvHeap.Get(); }

	struct SimParams
	{
		float DeltaTime = 1.0f / 144.0f;
		UINT NumParticles;
		float CellSize = 0.1f;
		UINT GridDim = 128;

		float Mass = 0.1f;
		float RestDensity = 1000.0f;
		float Viscosity = 0.05f;
		float GravityY = -9.81f;
		
		SM::Vector2 BoxX = {-3.0f, 4.0f };
		SM::Vector2 BoxY = {0.0f, 4.0f };

		SM::Vector2 BoxZ = {-1.5f, 2.0f };
		float epsilon = 5000.0f;
		float k = 0.0f;

		float n = 0.0f;
		float dqScale = 0.0f;
		float vorticityEpsilon = 0.00001f;
		float externalAccel = 0.0f;
	} m_SimParams;
	
	int m_Iterations = 4;

	bool m_WallMove = false;
	float m_TotalTime = 0.0f;
	float m_OriginMinX = m_SimParams.BoxX.x;
	float m_WallAmplitude = 2.0f;
	float m_WallSpeed = 1.5f;

private:
	ComPtr<ID3D12Resource> m_ParticleBuffer;
	ComPtr<ID3D12Resource> m_UploadBuffer;

	ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
	ComPtr<ID3D12DescriptorHeap> m_UavHeap;

	ComPtr<ID3D12RootSignature> m_ComputeRootSig;
	ComPtr<ID3D12PipelineState> m_IntegrationPSO;

	UINT m_NumParticles = 0;

	void InitParticles(std::vector<Particle>& outParticles);

	void CreateUavHeap(ID3D12Device* device);
	void CreateComputeRootSignature(ID3D12Device* device);
	void CreateComputePSO(ID3D12Device* device, ShaderHelper* helper);

private:
	// --- Sort Resources ---
	ComPtr<ID3D12RootSignature> m_SortRootSig;
	ComPtr<ID3D12PipelineState> m_SortPSO;

	// Constants struct for Bitonic Sort logic
	struct SortConstants {
		UINT BlockSize;     // Current block size (k) - often called 'Level'
		UINT Stride;        // Comparison distance (j) - often called 'Mask'
		UINT Padding[2];
	};

	void CreateSortRootSignature(ID3D12Device* device);
	void CreateSortPSO(ID3D12Device* device, ShaderHelper* helper);

private:
	ComPtr<ID3D12Resource> m_GridIndicesBuffer;
	ComPtr<ID3D12Resource> m_GridIndicesUpload;

	ComPtr<ID3D12RootSignature> m_GridMapRootSig;
	ComPtr<ID3D12PipelineState> m_ClearGridPSO;
	ComPtr<ID3D12PipelineState> m_BuildGridPSO;


	void CreateGridMapRootSignature(ID3D12Device* device);
	void CreateGridMapPSO(ID3D12Device* device, ShaderHelper* helper);

private:
	ComPtr<ID3D12Resource> m_DensityBuffer;
	ComPtr<ID3D12Resource> m_DensityUpload;
	ComPtr<ID3D12Resource> m_LambdaBuffer;
	ComPtr<ID3D12Resource> m_LambdaUpload;
	ComPtr<ID3D12Resource> m_VorticityBuffer;
	ComPtr<ID3D12Resource> m_VorticityUpload;

	ComPtr<ID3D12RootSignature> m_PbfSolverRootSig;
	ComPtr<ID3D12PipelineState> m_DensityLambdaPSO;
	ComPtr<ID3D12PipelineState> m_DeltaPosPSO;

	void CreatePbfSolverRootSignature(ID3D12Device* device);
	void CreateDensityLambdaPSO(ID3D12Device* device, ShaderHelper* helper);
	void CreateDeltaPosPSO(ID3D12Device* device, ShaderHelper* helper);

private:
	ComPtr<ID3D12PipelineState> m_ConstraintPSO;
	ComPtr<ID3D12PipelineState> m_VorticityPSO;
	ComPtr<ID3D12PipelineState> m_UpdateVelocityPSO;

	void CreateConstraintPSO(ID3D12Device* device, ShaderHelper* helper);
	void CreateVorticityPSO(ID3D12Device* device, ShaderHelper* helper);
	void CreateUpdateVelocityPSO(ID3D12Device* device, ShaderHelper* helper);

};