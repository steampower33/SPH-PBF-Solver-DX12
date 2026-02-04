#include "SphSolver.h"
#include "Renderer.h"

#include "SimGui.h"

static SimGui* g_SimGuiInstance = nullptr;

void SimGui::DrawControlPanel(SphSolver* solver, Renderer* renderer)
{
    auto& simParams = solver->m_SimParams;
    auto& renderParams = renderer->m_Params;

    ImGui::Begin("Settings");

    {
        float fps = ImGui::GetIO().Framerate;
        float ms = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "%.1f FPS (%.3f ms)", fps, ms);
        ImGui::Text("Active Particles: %d", simParams.NumParticles);
        ImGui::Text("Time Step (dt): %.6f s", simParams.DeltaTime);
        ImGui::Separator();
    }

    auto DrawPropertyGrid = [&](const char* title, auto drawContent) {
        ImGui::SeparatorText(title);
        if (ImGui::BeginChild(title, ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders, ImGuiWindowFlags_NoSavedSettings))
        {
            if (ImGui::BeginTable("PropertyTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV))
            {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 0.6f);

                drawContent();

                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
        };

    auto Row = [&](const char* label, auto drawWidget) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        drawWidget();
        };

    // [Group 1] System & Solver
    DrawPropertyGrid("System & Solver", [&]() {
        Row("Pause (Space)", [&] { ImGui::Checkbox("##Pause", &m_IsPaused); });

        int currentSimFPS = (simParams.DeltaTime > 0.0f) ? (int)(1.0f / simParams.DeltaTime) : 0;
        Row("Target Sim FPS", [&] {
            if (ImGui::DragInt("##TargetFPS", &currentSimFPS, 1, 30, 240)) {
                currentSimFPS = std::max(1, currentSimFPS);
                simParams.DeltaTime = 1.0f / (float)currentSimFPS;
            }
            });

        Row("Sub-steps", [&] { ImGui::SliderInt("##Iter", &solver->m_Iterations, 1, 10); });
        });

    // [Group 2] Fluid Properties
    DrawPropertyGrid("Fluid Properties", [&]() {
        Row("Mass", [&] { ImGui::DragFloat("##Mass", &simParams.Mass, 0.01f, 0.001f, 10.0f, "%.3f"); });
        Row("Rest Density", [&] { ImGui::DragFloat("##RestDensity", &simParams.RestDensity, 10.0f, 100.0f, 5000.0f); });
        Row("Viscosity", [&] { ImGui::DragFloat("##Viscosity", &simParams.Viscosity, 0.001f, 0.0f, 5.0f); });
        Row("Gravity Y", [&] { ImGui::DragFloat("##Gravity", &simParams.GravityY, 0.1f, -20.0f, 20.0f); });
        });

    // [Group 3] Stability
    DrawPropertyGrid("Solver Stability", [&]() {
        Row("CFM Epsilon", [&] { ImGui::DragFloat("##Epsilon", &simParams.Epsilon, 10.0f, 0.0f, 1000000.0f, "%.0f"); });

        // Tensile Instability
        Row("Tensile K", [&] { ImGui::DragFloat("##TK", &simParams.K, 1e-7f, 0.0f, 1.0f, "%.7f"); });
        Row("Tensile N", [&] { ImGui::DragFloat("##TN", &simParams.N, 0.1f, 1.0f, 10.0f, "%.1f"); });
        Row("Tensile dQ", [&] { ImGui::DragFloat("##TdQ", &simParams.DqScale, 0.01f, 0.0f, 1.0f); });

        Row("VorticityEpsilon", [&] { ImGui::DragFloat("##VorticityEpsilon", &simParams.VorticityEpsilon, 1e-6f, 1e-6f, 1.0f, "%.6f"); });
        });

    // [Group 4] Boundary & World
    DrawPropertyGrid("Boundary & World", [&]() {
        Row("Cell Size (h)", [&] {
            if (ImGui::DragFloat("##CellSize", &simParams.CellSize, 0.001f, 0.01f, 1.0f))
                simParams.CellSize = std::clamp(simParams.CellSize, 0.001f, 1.0f);
            });
        Row("Box X-Axis", [&] { ImGui::DragFloat2("##BoxX", &simParams.BoxX.x, 0.1f); });
        Row("Box Y-Axis", [&] { ImGui::DragFloat2("##BoxY", &simParams.BoxY.x, 0.1f); });
        Row("Box Z-Axis", [&] { ImGui::DragFloat2("##BoxZ", &simParams.BoxZ.x, 0.1f); });

        Row("Move Wall", [&] { ImGui::Checkbox("##WallMove", &solver->m_WallMove); });
        if (solver->m_WallMove) {
            Row(" - Speed", [&] { ImGui::DragFloat("##WallSpd", &solver->m_WallSpeed, 0.1f); });
            Row(" - Amp", [&] { ImGui::DragFloat("##WallAmp", &solver->m_WallAmplitude, 0.1f); });
        }
        });

    // [Group 5] Visualization
    DrawPropertyGrid("Visualization", [&]() {
        Row("Particle Radius", [&] { ImGui::DragFloat("##VisRad", &renderParams.VisualRadius, 0.001f, 0.01f, 0.5f); });
        });

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
        Helpers::SetDebugName(m_SrvHeap.Get(), "Heap_Srv_IMGUI");
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