#include "SphSolver.h"

#include "SimGui.h"

static SimGui* g_SimGuiInstance = nullptr;

void SimGui::Initialize(ID3D12Device* device, HWND hwnd, int frameCount, ID3D12CommandQueue* commandQueue)
{
    g_SimGuiInstance = this;

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 64;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_SrvHeap)));
    }

    m_SrvAlloc.Create(device, m_SrvHeap.Get());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = device;
    init_info.CommandQueue = commandQueue;
    init_info.NumFramesInFlight = frameCount;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;

    init_info.SrvDescriptorHeap = m_SrvHeap.Get();

    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu)
        {
            return g_SimGuiInstance->m_SrvAlloc.Alloc(out_cpu, out_gpu);
        };

    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
        {
            return g_SimGuiInstance->m_SrvAlloc.Free(cpu, gpu);
        };

    ImGui_ImplDX12_Init(&init_info);
}

void SimGui::Shutdown()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    m_SrvAlloc.Destroy();
    g_SimGuiInstance = nullptr;
}

void SimGui::BeginFrame()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void SimGui::EndFrame(ID3D12GraphicsCommandList* cmdList)
{
    ImGui::Render();

    ID3D12DescriptorHeap* heaps[] = { m_SrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}

void SimGui::DrawControlPanel(SphSolver* solver)
{
    ImGui::Begin("Simulation Control");

    auto& simParams = solver->m_SimParams;

    ImGui::Text("Application Average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

    ImGui::Separator();

    ImGui::Text("NumParticles : %d", simParams.NumParticles);
    ImGui::DragFloat("CellSize", &simParams.CellSize, 0.1f, 0.1f, 1.0f);
    ImGui::DragFloat("Particle Mass", &simParams.Mass, 0.1f, 0.1f, 1000.0f);
    ImGui::DragFloat("Rest Density", &simParams.RestDensity, 1.0f, 1.0f, 1000.0f);
    ImGui::DragFloat("Viscosity", &simParams.Viscosity, 0.0001f, 0.0f, 1.0f, "%.4f");
    ImGui::SliderInt("Iterations", &solver->m_SolverIterations, 1, 10);
    ImGui::DragFloat4("Box", &simParams.Box.x, 0.01f, -30.0f, 30.0f);

    ImGui::End();
}
