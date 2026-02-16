#include "ShaderHelper.h"

#include "SphSolver.h"

void SphSolver::UpdateInputs()
{
	float pushStrength = 4.0f;

	if (GetAsyncKeyState('A') & 0x8000)
		m_SimParams.ExternalAccel = -pushStrength;
	else if (GetAsyncKeyState('D') & 0x8000)
		m_SimParams.ExternalAccel = +pushStrength;
	else if (GetAsyncKeyState('S') & 0x8000)
		m_SimParams.ExternalAccel = 0.0f;

	if (GetAsyncKeyState('Z') & 0x8000)
		m_bSingleDamBreak = true;
	else if (GetAsyncKeyState('X') & 0x8000)
		m_bDoubleDamBreak = true;
	else if (GetAsyncKeyState('C') & 0x8000)
		m_bCornerDamBreak = true;

	if (GetAsyncKeyState(VK_SHIFT) & 0x0001)
		m_bWallMove = !m_bWallMove;

	if (m_bWallMove)
	{
		m_TotalTime += m_FixedDt;
		float animationFactor = 0.5f * (1.0f - cosf(m_TotalTime * m_WallSpeed));
		m_SimParams.BoxX.x = m_OriginMinX + (m_WallAmplitude * animationFactor);
	}
}

void SphSolver::Run(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->ResourceBarrier(1, &m_AllBarriers[TRANS_UAV_DIFFUSE]);

	if (m_bSingleDamBreak || m_bDoubleDamBreak || m_bCornerDamBreak)
	{
		ResetSimulation(cmdList);
	}

	m_Groups = (m_NumParticles + 255) / 256;

	UpdateInputs();

	ID3D12DescriptorHeap* heaps[] = { m_GlobalHeap.Get() };
	cmdList->SetDescriptorHeaps(1, heaps);

	CD3DX12_GPU_DESCRIPTOR_HANDLE uavBaseHandle(m_GlobalHeap->GetGPUDescriptorHandleForHeapStart());
	CD3DX12_GPU_DESCRIPTOR_HANDLE srvBaseHandle(m_GlobalHeap->GetGPUDescriptorHandleForHeapStart(), m_CbvSrvUavDescriptorSize, SRV_IDX_POS_PRED);

	cmdList->SetComputeRootSignature(m_GlobalRootSig.Get());
	cmdList->SetComputeRoot32BitConstants(RP_CB_SIM_PARAMS, sizeof(SimParams) / 4, &m_SimParams, 0);
	cmdList->SetComputeRootDescriptorTable(RP_DT_UAV, uavBaseHandle);
	cmdList->SetComputeRootDescriptorTable(RP_DT_SRV, srvBaseHandle);

	m_SimParams.DeltaTime = (m_Substeps == 1) ? m_FixedDt : m_FixedDt / m_Substeps;

	for (int s = 0; s < m_Substeps; ++s)
	{
		// [1] Integration Pass
		{
			GPU_PROFILE_BEGIN(cmdList, "Integration");
			cmdList->SetPipelineState(m_IntegrationPSO.Get());
			cmdList->Dispatch(m_Groups, 1, 1);
			cmdList->ResourceBarrier(1, &m_AllBarriers[UAV_BARRIER_POS_PRED]);
			GPU_PROFILE_END(cmdList);
		}

		// [2] Sort
		RunBitonicSort(cmdList);

		// [3] Permute And CopyBack
		PermuteAndCopyBack(cmdList);

		cmdList->SetComputeRootSignature(m_GlobalRootSig.Get());
		cmdList->SetComputeRoot32BitConstants(RP_CB_SIM_PARAMS, sizeof(SimParams) / 4, &m_SimParams, 0);
		cmdList->SetComputeRootDescriptorTable(RP_DT_UAV, uavBaseHandle);
		cmdList->SetComputeRootDescriptorTable(RP_DT_SRV, srvBaseHandle);

		// [5] Build Grid
		BuildGrid(cmdList);

		// [5] Solver Iteration
		cmdList->ResourceBarrier(1, &m_AllBarriers[TRANS_SRV_GRID_INDICES]);

		for (int iter = 0; iter < m_Iterations; ++iter)
		{
			cmdList->ResourceBarrier(1, &m_AllBarriers[TRANS_SRV_POS_PRED]);

			// Density & Lambda
			GPU_PROFILE_BEGIN(cmdList, "DensityLambda");
			cmdList->SetPipelineState(m_DensityLambdaPSO.Get());
			cmdList->Dispatch(m_Groups, 1, 1);
			GPU_PROFILE_END(cmdList);

			cmdList->ResourceBarrier(1, &m_AllBarriers[TRANS_SRV_LAMBDA]);

			// Delta Pos
			GPU_PROFILE_BEGIN(cmdList, "Delta Pos");
			cmdList->SetPipelineState(m_DeltaPosPSO.Get());
			cmdList->Dispatch(m_Groups, 1, 1);
			GPU_PROFILE_END(cmdList);

			D3D12_RESOURCE_BARRIER barriers2[] = {
				m_AllBarriers[UAV_BARRIER_DELTAPOS],  // Waiting for DeltaPos write completion
				m_AllBarriers[TRANS_UAV_LAMBDA],      // Lambda: SRV -> UAV
				m_AllBarriers[TRANS_UAV_POS_PRED]     // PosPred: SRV -> UAV
			};
			cmdList->ResourceBarrier(_countof(barriers2), barriers2);

			// Constraint Apply
			GPU_PROFILE_BEGIN(cmdList, "Constraint Apply");
			cmdList->SetPipelineState(m_ConstraintPSO.Get());
			cmdList->Dispatch(m_Groups, 1, 1);
			GPU_PROFILE_END(cmdList);
		}

		D3D12_RESOURCE_BARRIER endLoopBarriers[] = {
			m_AllBarriers[TRANS_SRV_POS_PRED],
			m_AllBarriers[TRANS_SRV_VEL_IN]
		};
		cmdList->ResourceBarrier(_countof(endLoopBarriers), endLoopBarriers);

		GPU_PROFILE_BEGIN(cmdList, "Vorticity");
		cmdList->SetPipelineState(m_VorticityPSO.Get());
		cmdList->Dispatch(m_Groups, 1, 1);
		GPU_PROFILE_END(cmdList);

		D3D12_RESOURCE_BARRIER vortBarriers[] = {
			m_AllBarriers[UAV_BARRIER_VORTICITY], // UAV Barrier (Safety)
			m_AllBarriers[TRANS_SRV_VORTICITY]    // UAV -> SRV
		};
		cmdList->ResourceBarrier(_countof(vortBarriers), vortBarriers);

		GPU_PROFILE_BEGIN(cmdList, "Update Velocity");
		cmdList->SetPipelineState(m_UpdateVelocityPSO.Get());
		cmdList->Dispatch(m_Groups, 1, 1);
		GPU_PROFILE_END(cmdList);

		D3D12_RESOURCE_BARRIER restoreBarriers[] = {
			m_AllBarriers[TRANS_UAV_GRID_INDICES],
			m_AllBarriers[TRANS_UAV_POS_PRED],
			m_AllBarriers[TRANS_UAV_VEL_IN],
			m_AllBarriers[TRANS_UAV_VORTICITY]
		};
		cmdList->ResourceBarrier(_countof(restoreBarriers), restoreBarriers);
	}

	// Diffuse Particle
	if (!m_bSolveDiffuseParticles) return;

	D3D12_RESOURCE_BARRIER startDiffuseBarriers[] = {
		m_AllBarriers[TRANS_SRV_GRID_INDICES],
		m_AllBarriers[TRANS_SRV_POS_PRED],
		m_AllBarriers[TRANS_SRV_VEL_OUT],
	};
	cmdList->ResourceBarrier(_countof(startDiffuseBarriers), startDiffuseBarriers);

	cmdList->SetComputeRootSignature(m_DiffuseRoogSig.Get());

	CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseUavHandle(m_GlobalHeap->GetGPUDescriptorHandleForHeapStart());
	diffuseUavHandle.Offset(UAV_IDX_DIFFUSE_PARTICLES, m_CbvSrvUavDescriptorSize);

	cmdList->SetComputeRoot32BitConstants(RP_CB_SIM_PARAMS, sizeof(SimParams) / 4, &m_SimParams, 0);
	cmdList->SetComputeRoot32BitConstants(RP_CB_SORT_OR_DIFFUSE_PARAMS, sizeof(DiffuseParams) / 4, &m_DiffuseParams, 0);
	cmdList->SetComputeRootDescriptorTable(RP_DT_SRV, srvBaseHandle);     // t0~t9
	cmdList->SetComputeRootDescriptorTable(RP_DT_UAV_DIFFUSE, diffuseUavHandle); // u3~u7

	// Clear Counter
	cmdList->SetPipelineState(m_ClearCounterPSO.Get());
	cmdList->Dispatch(1, 1, 1);
	cmdList->ResourceBarrier(1, &m_AllBarriers[UAV_BARRIER_COUNTERS]);

	// Diffuse Generation
	GPU_PROFILE_BEGIN(cmdList, "DiffuseGeneration");
	cmdList->SetPipelineState(m_DiffuseGenerationPSO.Get());
	cmdList->Dispatch(m_Groups, 1, 1);
	GPU_PROFILE_END(cmdList);

	// Build Dispatch Args
	UINT argTypeUpdate = 0;
	cmdList->SetComputeRoot32BitConstants(2, 1, &argTypeUpdate, 0);
	cmdList->SetPipelineState(m_BuildDispatchArgsPSO.Get());
	cmdList->Dispatch(1, 1, 1);

	cmdList->ResourceBarrier(1, &m_AllBarriers[TRANS_INDIRECT_DISPATCH_ARGS]);

	// Update Diffuse
	GPU_PROFILE_BEGIN(cmdList, "UpdateDiffuse");
	cmdList->SetPipelineState(m_UpdateDiffusePSO.Get());
	cmdList->ExecuteIndirect(m_DispatchSig.Get(), 1, m_DispatchArgsBuffer.Get(), 0, nullptr, 0);
	GPU_PROFILE_END(cmdList);

	D3D12_RESOURCE_BARRIER endDiffuseBarriers[] = {
		m_AllBarriers[TRANS_UAV_GRID_INDICES],
		m_AllBarriers[TRANS_UAV_POS_PRED],
		m_AllBarriers[TRANS_UAV_VEL_OUT],
		m_AllBarriers[TRANS_UAV_DISPATCH_ARGS],
		m_AllBarriers[UAV_BARRIER_DIFFUSE_COMPACT]
	};
	cmdList->ResourceBarrier(_countof(endDiffuseBarriers), endDiffuseBarriers);

	// Build Dispatch Args For Copy
	UINT argTypeCopy = 1;
	cmdList->SetPipelineState(m_BuildDispatchArgsPSO.Get());
	cmdList->SetComputeRoot32BitConstants(2, 1, &argTypeCopy, 0);
	cmdList->Dispatch(1, 1, 1);

	cmdList->ResourceBarrier(1, &m_AllBarriers[TRANS_INDIRECT_DISPATCH_ARGS]);

	cmdList->SetPipelineState(m_CopyDiffusePSO.Get());
	cmdList->ExecuteIndirect(m_DispatchSig.Get(), 1, m_DispatchArgsBuffer.Get(), 0, nullptr, 0);

	D3D12_RESOURCE_BARRIER preDrawArgsBarriers[] = {
		m_AllBarriers[TRANS_UAV_DISPATCH_ARGS],
		m_AllBarriers[UAV_BARRIER_DIFFUSE],
		m_AllBarriers[TRANS_UAV_DRAW_ARGS]
	};
	cmdList->ResourceBarrier(_countof(preDrawArgsBarriers), preDrawArgsBarriers);

	cmdList->SetPipelineState(m_BuildDrawArgsPSO.Get());
	cmdList->Dispatch(1, 1, 1);

	D3D12_RESOURCE_BARRIER finalBarriers[] = {
		m_AllBarriers[TRANS_INDIRECT_DRAW_ARGS],
		m_AllBarriers[TRANS_SRV_DIFFUSE]
	};
	cmdList->ResourceBarrier(_countof(finalBarriers), finalBarriers);
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

		float fps = (m_SimParams.DeltaTime > 0.0f) ? (1.0f / m_SimParams.DeltaTime) : 60.0f;
		ImGui::TextColored(ImVec4(0, 1, 0, 1), "Sim Fixed: %.1f FPS", fps);

		ImGui::Text("Active Particles: %d", m_SimParams.NumParticles);
		ImGui::Text("Time Step (dt): %.6f s", m_SimParams.DeltaTime);
		if (ImGui::DragFloat("Target Sim FPS", &fps, 1, 30.0, 240.0)) {
			m_SimParams.DeltaTime = 1.0f / (float)std::max(1, (int)fps);
		}
		ImGui::Checkbox("SolveDiffuseParticles", &m_bSolveDiffuseParticles);
		ImGui::SliderInt("Substeps", &m_Substeps, 1, 10);
		ImGui::SliderInt("Iterations", &m_Iterations, 1, 10);

		ImGui::SeparatorText("Scene Reset");
		ImGui::Checkbox("SingleDamBreak(Z)", &m_bSingleDamBreak);
		ImGui::Checkbox("DoubleDamBreak(X)", &m_bDoubleDamBreak);
		ImGui::Checkbox("CornerDamBreak(C)", &m_bCornerDamBreak);

		// Physical Properties
		ImGui::SeparatorText("Fluid Properties");
		ImGui::DragFloat("Mass", &m_SimParams.Mass, 0.01f, 0.001f, 10.0f);
		ImGui::DragFloat("Rest Density", &m_SimParams.RestDensity, 10.0f, 100.0f, 5000.0f);
		ImGui::DragFloat("Viscosity", &m_SimParams.Viscosity, 0.001f, 0.0f, 5.0f);
		ImGui::DragFloat("Gravity Y", &m_SimParams.GravityY, 0.1f, -20.0f, 20.0f);
		ImGui::DragFloat("Spacing", &m_Spacing, 1e-3f, 0.0f, 2.0f, "%.3f");

		// Boundary & World
		ImGui::SeparatorText("Boundary & World");

		if (ImGui::DragFloat("Cell Size (h)", &m_SimParams.CellSize, 0.001f, 0.01f, 1.0f)) {
			m_SimParams.CellSize = std::max(0.01f, m_SimParams.CellSize);
		}
		ImGui::DragFloat("JitterFactor", &m_SimParams.JitterFactor, 1e-3f, 0.0f, 1.0f, "%.3f");

		ImGui::DragFloat2("Box X (Min/Max)", &m_SimParams.BoxX.x, 0.1f);
		ImGui::DragFloat2("Box Y (Min/Max)", &m_SimParams.BoxY.x, 0.1f);
		ImGui::DragFloat2("Box Z (Min/Max)", &m_SimParams.BoxZ.x, 0.1f);

		// Wall Movement
		ImGui::SeparatorText("Moving Wall Interaction");
		ImGui::Checkbox("Enable Move(Shift)", &m_bWallMove);
		ImGui::DragFloat("OriginMinX", &m_OriginMinX, 0.1f);
		ImGui::DragFloat("Wall Speed", &m_WallSpeed, 0.1f);
		ImGui::DragFloat("Wall Amplitude", &m_WallAmplitude, 0.1f);

		ImGui::SeparatorText("Advanced Stability");
		ImGui::DragFloat("CFM Epsilon", &m_SimParams.Epsilon, 10.0f, 0.0f, 1000.0f);
		ImGui::DragFloat("Tensile K", &m_SimParams.K, 0.0f, 0.001f, 1.0f, "%.8f");
		ImGui::DragFloat("Tensile N", &m_SimParams.N, 0.1f, 1.0f, 10.0f);
		ImGui::DragFloat("DqScale", &m_SimParams.DqScale, 1e-8f, 0.0f, 1.0f, "%.8f");
		ImGui::DragFloat("Vorticity", &m_SimParams.VorticityEpsilon, 1e-3f, 0.0f, 1.0f, "%.3f");

		// About Generate Diffuse Particles
		ImGui::SeparatorText("Generate Diffuse Particles");
		ImGui::DragFloat("TrappedAirMin", &m_DiffuseParams.TrappedAirMin, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragFloat("TrappedAirMax", &m_DiffuseParams.TrappedAirMax, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragFloat("K_Ta", &m_DiffuseParams.K_Ta, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragFloat("WaveCrestMin", &m_DiffuseParams.WaveCrestMin, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragFloat("WaveCrestMax", &m_DiffuseParams.WaveCrestMax, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragFloat("K_Wc", &m_DiffuseParams.K_Wc, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragFloat("EnergyMin", &m_DiffuseParams.EnergyMin, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragFloat("EnergyMax", &m_DiffuseParams.EnergyMax, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragFloat("MaxLifeTime", &m_DiffuseParams.MaxLifeTime, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragFloat("CellSizeScale", &m_DiffuseParams.CellSizeScale, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragFloat("BubbleScale", &m_DiffuseParams.BubbleScale, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragFloat("BubbleScaleChangeSpeed", &m_DiffuseParams.BubbleScaleChangeSpeed, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragInt("SprayClassifyMaxNeighbours", &m_DiffuseParams.SprayClassifyMaxNeighbours, 1, 0, 100);
		ImGui::DragInt("BubbleClassifyMinNeighbours", &m_DiffuseParams.BubbleClassifyMinNeighbours, 1, 0, 100);
		ImGui::DragFloat("BubbleBuoyancy", &m_DiffuseParams.BubbleBuoyancy, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragFloat("FluidAccelMul", &m_DiffuseParams.FluidAccelMul, 0.1f, 0.0f, 100.0f, "%.1f");
		ImGui::DragInt("GeneratePerFrame", &m_DiffuseParams.GeneratePerFrame, 1, 0, 100);
	}
}

void SphSolver::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ShaderHelper* shaderHelper, std::vector<ComPtr<ID3D12Resource>>& uploadHeaps)
{
	m_pDevice = device;

	CreateBuffers(device, cmdList, uploadHeaps);
	InitBarriers();
	CreateAllViews(device);
	CreateGlobalRootSignature(device);
	CreatePermuteRootSignature(device);
	CreateDiffuseRootSignature(device);
	CreateCommandSignature(device);

	std::wstring version62 = L"cs_6_2";

	CreateComputePSO(device, shaderHelper, L"IntegrationCS.hlsl", m_IntegrationPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"BitonicSortLdsCS.hlsl", m_BitonicSortLdsPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"BitonicSortCS.hlsl", m_BitonicSortPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"PermuteDataCS.hlsl", m_PermuteDataPSO, m_PermuteRootSig);
	CreateComputePSO(device, shaderHelper, L"ClearGridIndicesCS.hlsl", m_ClearGridPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"BuildGridIndicesCS.hlsl", m_BuildGridPSO, m_GlobalRootSig);

	CreateComputePSO(device, shaderHelper, L"DensityLambdaCS.hlsl", m_DensityLambdaPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"DeltaPosCS.hlsl", m_DeltaPosPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"ConstraintCS.hlsl", m_ConstraintPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"VorticityCS.hlsl", m_VorticityPSO, m_GlobalRootSig);
	CreateComputePSO(device, shaderHelper, L"UpdateVelocityCS.hlsl", m_UpdateVelocityPSO, m_GlobalRootSig);

	CreateComputePSO(device, shaderHelper, L"ClearCounterCS.hlsl", m_ClearCounterPSO, m_DiffuseRoogSig);
	CreateComputePSO(device, shaderHelper, L"DiffuseGenerationCS.hlsl", m_DiffuseGenerationPSO, m_DiffuseRoogSig);
	CreateComputePSO(device, shaderHelper, L"BuildDispatchArgsCS.hlsl", m_BuildDispatchArgsPSO, m_DiffuseRoogSig);
	CreateComputePSO(device, shaderHelper, L"UpdateDiffuseCS.hlsl", m_UpdateDiffusePSO, m_DiffuseRoogSig);
	CreateComputePSO(device, shaderHelper, L"CopyDiffuseCS.hlsl", m_CopyDiffusePSO, m_DiffuseRoogSig);
	CreateComputePSO(device, shaderHelper, L"BuildDrawArgsCS.hlsl", m_BuildDrawArgsPSO, m_DiffuseRoogSig);
}

void SphSolver::InitBarriers()
{
	// Position Predicted
	m_AllBarriers[UAV_BARRIER_POS_PRED] = CD3DX12_RESOURCE_BARRIER::UAV(m_PosPred.Get());
	m_AllBarriers[TRANS_SRV_POS_PRED] = CD3DX12_RESOURCE_BARRIER::Transition(m_PosPred.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	m_AllBarriers[TRANS_UAV_POS_PRED] = CD3DX12_RESOURCE_BARRIER::Transition(m_PosPred.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// Position Old
	m_AllBarriers[TRANS_SRV_POS_OLD] = CD3DX12_RESOURCE_BARRIER::Transition(m_PosOld.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	m_AllBarriers[TRANS_UAV_POS_OLD] = CD3DX12_RESOURCE_BARRIER::Transition(m_PosOld.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// Velocity In
	m_AllBarriers[TRANS_SRV_VEL_IN] = CD3DX12_RESOURCE_BARRIER::Transition(m_VelIn.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	m_AllBarriers[TRANS_UAV_VEL_IN] = CD3DX12_RESOURCE_BARRIER::Transition(m_VelIn.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// Velocity Out
	m_AllBarriers[TRANS_SRV_VEL_OUT] = CD3DX12_RESOURCE_BARRIER::Transition(m_VelOut.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	m_AllBarriers[TRANS_UAV_VEL_OUT] = CD3DX12_RESOURCE_BARRIER::Transition(m_VelOut.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// Grid Indices
	m_AllBarriers[TRANS_SRV_GRID_INDICES] = CD3DX12_RESOURCE_BARRIER::Transition(m_GridIndices.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	m_AllBarriers[TRANS_UAV_GRID_INDICES] = CD3DX12_RESOURCE_BARRIER::Transition(m_GridIndices.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// Lambda
	m_AllBarriers[UAV_BARRIER_LAMBDA] = CD3DX12_RESOURCE_BARRIER::UAV(m_Lambda.Get());
	m_AllBarriers[TRANS_SRV_LAMBDA] = CD3DX12_RESOURCE_BARRIER::Transition(m_Lambda.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	m_AllBarriers[TRANS_UAV_LAMBDA] = CD3DX12_RESOURCE_BARRIER::Transition(m_Lambda.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// Density & DeltaPos & VelOut (UAV Barrier Only)
	m_AllBarriers[UAV_BARRIER_DENSITY] = CD3DX12_RESOURCE_BARRIER::UAV(m_Density.Get());
	m_AllBarriers[UAV_BARRIER_DELTAPOS] = CD3DX12_RESOURCE_BARRIER::UAV(m_DeltaPos.Get());
	m_AllBarriers[UAV_BARRIER_VEL_OUT] = CD3DX12_RESOURCE_BARRIER::UAV(m_VelOut.Get());

	// Vorticity
	m_AllBarriers[UAV_BARRIER_VORTICITY] = CD3DX12_RESOURCE_BARRIER::UAV(m_Vorticity.Get());
	m_AllBarriers[TRANS_SRV_VORTICITY] = CD3DX12_RESOURCE_BARRIER::Transition(m_Vorticity.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	m_AllBarriers[TRANS_UAV_VORTICITY] = CD3DX12_RESOURCE_BARRIER::Transition(m_Vorticity.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// Diffuse Particles Main
	m_AllBarriers[TRANS_UAV_DIFFUSE] = CD3DX12_RESOURCE_BARRIER::Transition(m_DiffuseParticles.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	m_AllBarriers[TRANS_SRV_DIFFUSE] = CD3DX12_RESOURCE_BARRIER::Transition(m_DiffuseParticles.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	m_AllBarriers[UAV_BARRIER_DIFFUSE] = CD3DX12_RESOURCE_BARRIER::UAV(m_DiffuseParticles.Get());

	// Diffuse Compacted
	m_AllBarriers[UAV_BARRIER_DIFFUSE_COMPACT] = CD3DX12_RESOURCE_BARRIER::UAV(m_DiffuseParticlesCompacted.Get());

	// Counters
	m_AllBarriers[UAV_BARRIER_COUNTERS] = CD3DX12_RESOURCE_BARRIER::UAV(m_Counters.Get());

	// Indirect Arguments Buffers
	m_AllBarriers[TRANS_UAV_DISPATCH_ARGS] = CD3DX12_RESOURCE_BARRIER::Transition(m_DispatchArgsBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	m_AllBarriers[TRANS_INDIRECT_DISPATCH_ARGS] = CD3DX12_RESOURCE_BARRIER::Transition(m_DispatchArgsBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

	m_AllBarriers[TRANS_UAV_DRAW_ARGS] = CD3DX12_RESOURCE_BARRIER::Transition(m_DrawArgsBuffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	m_AllBarriers[TRANS_INDIRECT_DRAW_ARGS] = CD3DX12_RESOURCE_BARRIER::Transition(m_DrawArgsBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
}

void SphSolver::CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, std::vector<ComPtr<ID3D12Resource>>& tempUploadBuffers)
{
	m_InitPos.resize(m_NumParticles);
	m_Zero1.resize(m_NumParticles, 0.0f);
	m_Zero3.resize(m_NumParticles, SM::Vector3(0.0f));
	m_DiffuseParticlesData.resize(m_DiffuseParams.MaxDiffuseParticles, DiffuseParticle(SM::Vector4(0.0f), SM::Vector4(0.0f)));

	m_SimParams.NumParticles = m_NumParticles;
	m_OriginMinX = m_SimParams.BoxX.x;

	ResetParticlePos();

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

	CreateAndTrackBuffer(nullptr, m_DiffuseParams.MaxDiffuseParticles * sizeof(DiffuseParticle), m_DiffuseParticles, "m_DiffuseParticles");
	CreateAndTrackBuffer(nullptr, m_DiffuseParams.MaxDiffuseParticles * sizeof(DiffuseParticle), m_DiffuseParticlesCompacted, "m_DiffuseParticlesCompacted");

	// [0] = ActiveCount
	// [1] = SurvivorCount
	CreateAndTrackBuffer(m_CounterValues.data(), sizeof(UINT) * 2, m_Counters, "m_Counters");

	CreateAndTrackBuffer(nullptr, sizeof(DispatchIndirectCommand), m_DispatchArgsBuffer, "m_DispatchArgsBuffer");
	CreateAndTrackBuffer(nullptr, sizeof(DrawIndirectCommand), m_DrawArgsBuffer, "m_DrawArgsBuffer");
}

void SphSolver::ResetParticlePos()
{
	m_SimParams.BoxY = { 0.0f, 50.0f };

	float offset = 0.2f;

	float halfWidthX = 11.0f;
	float halfWidthZ = 4.0f;

	auto CornerDamBreak = [&]()
		{
			m_SimParams.BoxX = { -halfWidthX, halfWidthX };
			m_SimParams.BoxZ = { -halfWidthZ, halfWidthZ };

			m_OriginMinX = m_SimParams.BoxX.x;

			float startX = m_SimParams.BoxX.x + offset; // up
			float startY = m_SimParams.BoxY.x + offset; // up
			float startZ = m_SimParams.BoxZ.y - offset; // down

			int idx = 0;
			for (int z = 0; z < m_Z; z++)
				for (int y = 0; y < m_Y; ++y)
					for (int x = 0; x < m_X; ++x)
					{
						m_InitPos[idx] = SM::Vector3(
							startX + (x * m_Spacing),
							startY + (y * m_Spacing),
							startZ - (z * m_Spacing));
						idx++;
					}
		};

	auto SingleDamBreak = [&]()
		{
			m_SimParams.BoxX = { -halfWidthX, halfWidthX };
			m_SimParams.BoxZ = { -halfWidthZ, halfWidthZ };

			float startX = m_SimParams.BoxX.x + offset; // up
			float startY = m_SimParams.BoxY.x + offset; // up
			float startZ = m_Z * m_Spacing * 0.5f; // down

			int idx = 0;
			for (int z = 0; z < m_Z; z++)
				for (int y = 0; y < m_Y; ++y)
					for (int x = 0; x < m_X; ++x)
					{
						m_InitPos[idx] = SM::Vector3(
							startX + (x * m_Spacing),
							startY + (y * m_Spacing),
							startZ - (z * m_Spacing));
						idx++;
					}
		};

	auto DoubleDamBreak = [&]()
		{
			m_SimParams.BoxX = { -halfWidthX, halfWidthX };
			m_SimParams.BoxZ = { -halfWidthZ, halfWidthZ };

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
								startX + (x * m_Spacing),
								startY + (y * m_Spacing),
								startZ + (z * m_Spacing));
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
								startX - (x * m_Spacing),
								startY + (y * m_Spacing),
								startZ - (z * m_Spacing));
							idx++;
						}
			}
		};


	if (m_bSingleDamBreak)
	{
		SingleDamBreak();
		m_bSingleDamBreak = false;
	}
	else if (m_bDoubleDamBreak)
	{
		DoubleDamBreak();
		m_bDoubleDamBreak = false;
	}
	else if (m_bCornerDamBreak)
	{
		CornerDamBreak();
		m_bCornerDamBreak = false;
	}


}

void SphSolver::CreateAllViews(ID3D12Device* device)
{
	// Create Heap
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = HeapDescriptors::DESCRIPTOR_COUNT;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_GlobalHeap)));
	Helpers::SetDebugName(m_GlobalHeap.Get(), "Heap_UAV_SoA");

	m_CbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE hHandle(m_GlobalHeap->GetCPUDescriptorHandleForHeapStart());

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

	CreateBufferUAV(m_DiffuseParticles.Get(), m_DiffuseParams.MaxDiffuseParticles, sizeof(DiffuseParticle), UAV_IDX_DIFFUSE_PARTICLES);
	CreateBufferUAV(m_DiffuseParticlesCompacted.Get(), m_DiffuseParams.MaxDiffuseParticles, sizeof(DiffuseParticle), UAV_IDX_DIFFUSE_PARTICLES_COMPACTED);
	CreateBufferUAV(m_Counters.Get(), 2, sizeof(UINT), UAV_IDX_COUNTERS);
	CreateBufferUAV(m_DispatchArgsBuffer.Get(), 1, sizeof(DispatchIndirectCommand), UAV_IDX_DISPATCH_ARGS);
	CreateBufferUAV(m_DrawArgsBuffer.Get(), 1, sizeof(DrawIndirectCommand), UAV_IDX_DRAW_ARGS);

	// Create SRV
	auto CreateBufferSRV = [&](ID3D12Resource* pBuffer, UINT numElements, UINT stride, int heapIdx) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.NumElements = numElements;
		srvDesc.Buffer.StructureByteStride = stride;

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = hHandle;
		handle.Offset(heapIdx, m_CbvSrvUavDescriptorSize);
		device->CreateShaderResourceView(pBuffer, &srvDesc, handle);
		};

	CreateBufferSRV(m_PosPred.Get(), m_NumParticles, sizeof(SM::Vector3), SRV_IDX_POS_PRED);	  // t0
	CreateBufferSRV(m_PosOld.Get(), m_NumParticles, sizeof(SM::Vector3), SRV_IDX_POS_OLD);        // t1
	CreateBufferSRV(m_VelIn.Get(), m_NumParticles, sizeof(SM::Vector3), SRV_IDX_VEL_IN);	      // t2
	CreateBufferSRV(m_SortedIndices.Get(), m_NumParticles, sizeof(UINT), SRV_IDX_INDICES);		  // t3
	CreateBufferSRV(m_VelOut.Get(), m_NumParticles, sizeof(SM::Vector3), SRV_IDX_VEL_OUT);        // t4
	CreateBufferSRV(m_GridIndices.Get(), numGridCells, sizeof(UINT) * 2, SRV_IDX_GRID_INDICES);   // t5
	CreateBufferSRV(m_Lambda.Get(), m_NumParticles, sizeof(float), SRV_IDX_LAMBDA);				  // t6
	CreateBufferSRV(m_Vorticity.Get(), m_NumParticles, sizeVec3, SRV_IDX_VORTICITY);				  // t7

	CreateBufferSRV(m_Density.Get(), m_NumParticles, sizeof(float), SRV_IDX_DENSITY_RENDER);	// t8
	CreateBufferSRV(m_DiffuseParticles.Get(), m_DiffuseParams.MaxDiffuseParticles, sizeof(DiffuseParticle), SRV_IDX_DIFFUSE_PARTICLES_RENDER);	// t9
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
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	rootParameters[3].InitAsDescriptorTable(1, &srvRange);

	static_assert((sizeof(SimParams) / 4 + sizeof(SortConstants) / 4 + 14) < 64, "Exceeds 64 DWORDs");

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

void SphSolver::CreateDiffuseRootSignature(ID3D12Device* device)
{
	CD3DX12_ROOT_PARAMETER1 rootParameters[5];

	rootParameters[0].InitAsConstants(sizeof(SimParams) / 4, 0);
	rootParameters[1].InitAsConstants(sizeof(DiffuseParams) / 4, 1);
	rootParameters[2].InitAsConstants(1, 2);

	CD3DX12_DESCRIPTOR_RANGE1 srvRange;
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	rootParameters[3].InitAsDescriptorTable(1, &srvRange);

	CD3DX12_DESCRIPTOR_RANGE1 uavRange;
	uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	rootParameters[4].InitAsDescriptorTable(1, &uavRange);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(_countof(rootParameters), rootParameters);

	ComPtr<ID3DBlob> signatureBlob, errorBlob;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); ThrowIfFailed(hr); }

	ThrowIfFailed(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_DiffuseRoogSig)));
	Helpers::SetDebugName(m_DiffuseRoogSig.Get(), "m_DiffuseRoogSig");
}

void SphSolver::CreateCommandSignature(ID3D12Device* device)
{
	D3D12_INDIRECT_ARGUMENT_DESC dispatchArg = {};
	dispatchArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

	D3D12_COMMAND_SIGNATURE_DESC dispatchSigDesc = {};
	dispatchSigDesc.ByteStride = sizeof(DispatchIndirectCommand);
	dispatchSigDesc.NumArgumentDescs = 1;
	dispatchSigDesc.pArgumentDescs = &dispatchArg;
	dispatchSigDesc.NodeMask = 0;

	ThrowIfFailed(device->CreateCommandSignature(
		&dispatchSigDesc,
		nullptr,
		IID_PPV_ARGS(&m_DispatchSig)
	));

	D3D12_INDIRECT_ARGUMENT_DESC drawArg = {};
	drawArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

	D3D12_COMMAND_SIGNATURE_DESC drawSigDesc = {};
	drawSigDesc.ByteStride = sizeof(DrawIndirectCommand);
	drawSigDesc.NumArgumentDescs = 1;
	drawSigDesc.pArgumentDescs = &drawArg;
	drawSigDesc.NodeMask = 0;

	ThrowIfFailed(device->CreateCommandSignature(
		&drawSigDesc,
		nullptr,
		IID_PPV_ARGS(&m_DrawSig)
	));
}

void SphSolver::CreateComputePSO(ID3D12Device* device, ShaderHelper* helper,
	std::wstring shaderFile, ComPtr<ID3D12PipelineState>& outPSO, ComPtr<ID3D12RootSignature>& sig, std::wstring version)
{
	ComPtr<IDxcBlob> csBlob = helper->Compile(m_ShaderBaseName, shaderFile, L"main", version);

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = sig.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(csBlob->GetBufferPointer(), csBlob->GetBufferSize());

	device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&outPSO));
}

void SphSolver::RunBitonicSort(ID3D12GraphicsCommandList* cmdList)
{
	GPU_PROFILE_BEGIN(cmdList, "BitonicSort");

	cmdList->ResourceBarrier(1, &m_AllBarriers[TRANS_SRV_POS_PRED]);

	UINT sortBlockSize = 2048;

	cmdList->SetPipelineState(m_BitonicSortLdsPSO.Get());
	cmdList->Dispatch((m_NumParticles + sortBlockSize - 1) / sortBlockSize, 1, 1);

	auto ldsBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_SortedIndices.Get());
	cmdList->ResourceBarrier(1, &ldsBarrier);

	cmdList->SetPipelineState(m_BitonicSortPSO.Get());
	UINT groups = (m_NumParticles + 255) / 256;
	for (UINT blockSize = sortBlockSize * 2; blockSize <= m_NumParticles; blockSize <<= 1) {
		for (UINT stride = blockSize >> 1; stride > 0; stride >>= 1) {
			SortConstants sortConsts = { blockSize, stride, 0, 0 };

			cmdList->SetComputeRoot32BitConstants(1, 4, &sortConsts, 0);

			cmdList->Dispatch(groups, 1, 1);

			auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_SortedIndices.Get());
			cmdList->ResourceBarrier(1, &barrier);
		}
	}

	cmdList->ResourceBarrier(1, &m_AllBarriers[TRANS_UAV_POS_PRED]);

	GPU_PROFILE_END(cmdList);
}

void SphSolver::PermuteAndCopyBack(ID3D12GraphicsCommandList* cmdList)
{
	// Permute Pass (Data Reordering)
	{
		GPU_PROFILE_BEGIN(cmdList, "PermuteAndCopyBack");

		auto sortedIndicesUAVtoSRV = CD3DX12_RESOURCE_BARRIER::Transition(m_SortedIndices.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmdList->ResourceBarrier(1, &sortedIndicesUAVtoSRV);

		cmdList->SetComputeRootSignature(m_PermuteRootSig.Get());
		cmdList->SetPipelineState(m_PermuteDataPSO.Get());

		cmdList->SetComputeRoot32BitConstants(0, sizeof(SimParams) / 4, &m_SimParams, 0);

		// UAV Table (Param 1) -> u0~u2 (Temp Buffers)
		CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(m_GlobalHeap->GetGPUDescriptorHandleForHeapStart());
		uavHandle.Offset(UAV_IDX_TEMP_POS, m_CbvSrvUavDescriptorSize);
		cmdList->SetComputeRootDescriptorTable(1, uavHandle);

		// SRV Table (Param 2) -> t0~t3 (Source)
		CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_GlobalHeap->GetGPUDescriptorHandleForHeapStart());
		srvHandle.Offset(SRV_IDX_POS_PRED, m_CbvSrvUavDescriptorSize);
		cmdList->SetComputeRootDescriptorTable(2, srvHandle);

		cmdList->Dispatch(m_Groups, 1, 1);

		auto sortedIndicesSRVtoUAV = CD3DX12_RESOURCE_BARRIER::Transition(m_SortedIndices.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmdList->ResourceBarrier(1, &sortedIndicesSRVtoUAV);

		GPU_PROFILE_END(cmdList);
	}

	// Copy Back
	{
		GPU_PROFILE_BEGIN(cmdList, "CopyBack");

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

		GPU_PROFILE_END(cmdList);
	}
}

void SphSolver::BuildGrid(ID3D12GraphicsCommandList* cmdList)
{
	GPU_PROFILE_BEGIN(cmdList, "Build Grid");

	cmdList->SetPipelineState(m_ClearGridPSO.Get());

	UINT gridGroups = (m_SimParams.GridDim * m_SimParams.GridDim * m_SimParams.GridDim + 255) / 256;
	cmdList->Dispatch(gridGroups, 1, 1);

	auto gridBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_GridIndices.Get());
	cmdList->ResourceBarrier(1, &gridBarrier);

	cmdList->SetPipelineState(m_BuildGridPSO.Get());
	cmdList->Dispatch(m_Groups, 1, 1);

	cmdList->ResourceBarrier(1, &gridBarrier);

	GPU_PROFILE_END(cmdList);
}

void SphSolver::ResetSimulation(ID3D12GraphicsCommandList* cmdList)
{
	ResetParticlePos();

	UploadData(cmdList, m_PosPred, m_InitPos, m_NumParticles, sizeof(SM::Vector3));
	UploadData(cmdList, m_VelOut, m_Zero3, m_NumParticles, sizeof(SM::Vector3));
	UploadData(cmdList, m_Density, m_Zero1, m_NumParticles, sizeof(float));
	UploadData(cmdList, m_DiffuseParticles, m_DiffuseParticlesData, m_DiffuseParams.MaxDiffuseParticles, sizeof(DiffuseParticle));
	UploadData(cmdList, m_Counters, m_CounterValues, 2, sizeof(UINT));
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