#include "ShaderHelper.h"

#include "SphSolver.h"

void SphSolver::Update(ID3D12GraphicsCommandList* cmdList, float dt)
{
    auto barrierToUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        m_ParticleBuffer.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->ResourceBarrier(1, &barrierToUAV);

    cmdList->SetPipelineState(m_IntegrationPSO.Get());
    cmdList->SetComputeRootSignature(m_ComputeRootSig.Get());

    ID3D12DescriptorHeap* heaps[] = { m_UavHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    SimParams params = {};
    params.DeltaTime = dt;
    params.NumParticles = m_NumParticles;
    params.CellSize = 1.0f; // Example: 2.0f * ParticleRadius (Set appropriate value)
    params.GridDim = 128;

    cmdList->SetComputeRoot32BitConstants(0, sizeof(SimParams) / 4, &params, 0);

    cmdList->SetComputeRootDescriptorTable(1, m_UavHeap->GetGPUDescriptorHandleForHeapStart());

    UINT groups = (m_NumParticles + 255) / 256;
    cmdList->Dispatch(groups, 1, 1);

    auto barrierToSRV = CD3DX12_RESOURCE_BARRIER::Transition(
        m_ParticleBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->ResourceBarrier(1, &barrierToSRV);
}

void SphSolver::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT particleCount, ShaderHelper* shaderHelper)
{
    m_NumParticles = particleCount;

    std::vector<Particle> particles(m_NumParticles);
    InitRandomParticles(particles);

    UINT64 bufferSize = sizeof(Particle) * m_NumParticles;

    m_ParticleBuffer = Helpers::CreateDefaultBuffer(
        device,
        cmdList,
        particles.data(),
        bufferSize,
        m_UploadBuffer,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    );

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
    CreateComputeRootSignature(device);
    CreateComputePSO(device, shaderHelper);

    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = m_NumParticles;
        uavDesc.Buffer.StructureByteStride = sizeof(Particle);

        device->CreateUnorderedAccessView(
            m_ParticleBuffer.Get(), nullptr, &uavDesc,
            m_UavHeap->GetCPUDescriptorHandleForHeapStart());
    }

    CreateSortRootSignature(device);
    CreateSortPSO(device, shaderHelper);
}

void SphSolver::CreateUavHeap(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_UavHeap)));
}

void SphSolver::CreateComputeRootSignature(ID3D12Device* device)
{
    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
    rootParameters[0].InitAsConstants(4, 0);

    CD3DX12_DESCRIPTOR_RANGE1 uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
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
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> disPos(-5.0f, 5.0f);

    for (auto& p : outParticles) {
        p.Position = SM::Vector3(disPos(gen), disPos(gen) + 10.0f, 0.0f);
        p.Velocity = SM::Vector3(0, 0, 0);
        p.Density = 0.0f;
        p.Pressure = 0.0f;
    }
}

void SphSolver::CreateSortRootSignature(ID3D12Device* device) 
{
    // Just a simple RootSig with 1 ConstantBuffer (b0) and 1 UAV Table (u0)
    CD3DX12_ROOT_PARAMETER1 rootParameters[3];
    rootParameters[0].InitAsConstants(4, 0); // b0: SimParams
    rootParameters[1].InitAsConstants(4, 1); // b1: SortConstants

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

    SimParams params = {};
    params.DeltaTime = 0.0f;        // Not used in sort
    params.NumParticles = m_NumParticles;
    params.CellSize = 1.0f;         // Must match Update()
    params.GridDim = 128;           // Must match Update()
    cmdList->SetComputeRoot32BitConstants(0, sizeof(SimParams) / 4, &params, 0);

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