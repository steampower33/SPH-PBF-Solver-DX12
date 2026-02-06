#include "SphSolver.h"
#include "SSFRPass.h"
#include "RendererManager.h"

#include "SimGui.h"

static SimGui* g_SimGuiInstance = nullptr;

void SimGui::DrawControlPanel(SphSolver* solver, RendererManager* renderManager)
{
    ImGui::Begin("Control Panel");
	
	solver->OnGui();
    renderManager->OnGui();

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