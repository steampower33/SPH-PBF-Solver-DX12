#include "SphSolver.h"
#include "Renderer.h"

#include "SimGui.h"

static SimGui* g_SimGuiInstance = nullptr;

void SimGui::DrawControlPanel(SphSolver* solver, Renderer* renderer)
{
	auto row = [&](const char* label, auto drawControl)
		{
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);

			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-FLT_MIN);
			drawControl();
		};

	auto& simParams = solver->m_SimParams;
	auto& renderParams = renderer->m_Params;

	ImGui::Begin("Settings");

	float fps = ImGui::GetIO().Framerate;
	float ms = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
	ImGui::Text("Frame Time: %.3f ms (%.1f FPS)", ms, fps);
	ImGui::Text("NumParticles : %d", simParams.NumParticles);
	ImGui::Text("dt: %.6f s", simParams.DeltaTime);

	auto label = "Simulation";
	ImGui::SeparatorText(label);
	ImGui::BeginChild(label, ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
	if (ImGui::BeginTable(label, 2,
		ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV))
	{
		row("Pause", [&] { ImGui::Checkbox("##Pause", &m_IsPaused); });
		int currentSimFPS = (simParams.DeltaTime > 0.0f) ? (int)(1.0f / simParams.DeltaTime) : 0;
		row("Target Sim FPS", [&] { if (ImGui::DragInt("##Target Sim FPS", &currentSimFPS, 1, 30, 240))
		{
			if (currentSimFPS < 1) currentSimFPS = 1;
			simParams.DeltaTime = 1.0f / (float)currentSimFPS;
		} });
		row("Iteration", [&] { ImGui::SliderInt("##Iteration", &solver->m_SolverIterations, 1, 10); });
		row("CellSize", [&] {ImGui::InputFloat("##CellSize", &simParams.CellSize, 0.1f, 0.1f); });
		row("Particle Mass   ", [&] { ImGui::InputFloat("##Particle Mass", &simParams.Mass, 0.1f, 0.1f); });
		row("Rest Density", [&] { ImGui::InputFloat("##Rest Density", &simParams.RestDensity, 1.0f, 1.0f); });
		row("Viscosity", [&] { ImGui::InputFloat("##Viscosity", &simParams.Viscosity, 0.0001f, 0.0001f, "%.4f"); });
		row("GravityY", [&] { ImGui::InputFloat("##GravityY", &simParams.GravityY, 0.01f, 0.01f, "%.2f"); });
		row("VisualRadius", [&] { ImGui::InputFloat("##VisualRadius", &renderParams.VisualRadius, 0.001f, 0.01f, "%.3f"); });
		row("Box", [&] { ImGui::DragFloat4("Box", &simParams.Box.x, 0.01f, -30.0f, 30.0f); });
		ImGui::EndTable();
	}
	ImGui::EndChild();

	ImGui::End();
}

void SimGui::Initialize(ID3D12Device* device, HWND hwnd, int frameCount, ID3D12CommandQueue* commandQueue, float width, float height)
{
	g_SimGuiInstance = this;
	m_Width = width;
	m_Height = height;

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
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	SetStyle();

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

void SimGui::SetStyle()
{
	//ImGui::StyleColorsClassic();
	//ImGui::StyleColorsLight();
	ImGui::StyleColorsDark();

	ImGuiStyle& style = ImGui::GetStyle();

	style.WindowRounding = 4.0f;
	style.FrameRounding = 4.0f;
	style.GrabRounding = 3.0f;
	style.PopupRounding = 4.0f;
	style.TabRounding = 4.0f;
	style.WindowMenuButtonPosition = ImGuiDir_Right;
	style.ScrollbarSize = 10.0f;
	style.GrabMinSize = 10.0f;
	style.SeparatorTextBorderSize = 2.0f;
	style.ScrollbarRounding = 4.0f;
	style.FontScaleMain = 1.0f;
}