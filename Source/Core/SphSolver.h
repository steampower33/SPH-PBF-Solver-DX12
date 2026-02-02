#pragma once

class ShaderHelper;

struct Particle
{
	SM::Vector3 Position;
	float Density;      // Padding for alignment/density
	SM::Vector3 Velocity;
	float Pressure;     // Padding for alignment/pressure
};

class SphSolver
{
public:
	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT numParticles, ShaderHelper* shaderHelper);

	void Update(ID3D12GraphicsCommandList* cmdList, float dt);

	void RunBitonicSort(ID3D12GraphicsCommandList* cmdList);

	ID3D12Resource* GetParticleBuffer() const { return m_ParticleBuffer.Get(); }
	UINT GetNumParticles() const { return m_NumParticles; }
	ID3D12DescriptorHeap* GetSrvHeap() const { return m_SrvHeap.Get(); }

private:
	ComPtr<ID3D12Resource> m_ParticleBuffer;
	ComPtr<ID3D12Resource> m_UploadBuffer;

	ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
	ComPtr<ID3D12DescriptorHeap> m_UavHeap;

	struct SimParams
	{
		float DeltaTime;
		UINT NumParticles;
		float CellSize;  // Critical for Spatial Hashing
		UINT GridDim;    // Critical for Spatial Hashing
	};
	ComPtr<ID3D12RootSignature> m_ComputeRootSig;
	ComPtr<ID3D12PipelineState> m_IntegrationPSO;

	UINT m_NumParticles = 0;

	void InitRandomParticles(std::vector<Particle>& outParticles);

	void CreateUavHeap(ID3D12Device* device);
	void CreateComputeRootSignature(ID3D12Device* device);
	void CreateComputePSO(ID3D12Device* device, ShaderHelper* shaderHelper);

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
	void CreateSortPSO(ID3D12Device* device, ShaderHelper* shaderHelper);
};