#include "GameTimer.h"

#include "FluidSimApp.h"

bool FluidSimApp::Run()
{
	GameTimer timer;
	timer.Reset();

	SM::Vector3 eye(0.0f, 0.0f, -30.0f);
	SM::Vector3 target(0.0f, 0.0f, 1.0f);
	SM::Vector3 up(0.0f, 1.0f, 0.0f);

	SM::Matrix view = SM::Matrix::CreateLookAt(eye, target, up);

	SM::Matrix proj = SM::Matrix::CreatePerspectiveFieldOfView(
		0.785398163f,  // 45 degrees FOV
		m_AspectRatio,
		0.1f,           // Near plane
		1000.0f         // Far plane
	);

	bool bIsExit = false;

	while (bIsExit == false)
	{
		timer.Tick();

		MSG msg;
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT) bIsExit = true;
		}
		if (bIsExit) break;

		float dt = timer.GetDeltaTime();
		float totalTime = timer.GetTotalTime();

		// --- Update ---
		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
		{
			PostQuitMessage(0);
			break;
		}

		// --- Rendering ---
		ID3D12GraphicsCommandList* cmdList = m_GraphicsCore.BeginFrame();

		m_Solver.Update(cmdList, 0.016f);
		m_Solver.RunBitonicSort(cmdList);

		m_Renderer.Render(cmdList, &m_Solver, view, proj);

		m_GraphicsCore.EndFrame();
	}

	return true;
}

LRESULT CALLBACK FluidSimApp::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	//if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
	//	return true;

	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

void FluidSimApp::Initialize(HINSTANCE hInstance)
{
	WCHAR WindowClass[] = L"SPH-PBF-Solver-DX12";
	WCHAR Title[] = L"SPH-PBF-Solver-DX12";

	WNDCLASSW wndclass = { 0, WndProc, 0, 0, 0, 0, 0, 0, 0, WindowClass };
	RegisterClassW(&wndclass);

	DWORD dwStyle = WS_OVERLAPPEDWINDOW;
	RECT wr = { 0, 0, (LONG)m_Width, (LONG)m_Height };
	AdjustWindowRect(&wr, dwStyle, FALSE);

	HWND hWnd = CreateWindowExW(0, WindowClass, Title,
		dwStyle | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		wr.right - wr.left,   // This will be larger than 1280 (e.g., 1296)
		wr.bottom - wr.top,   // This will be larger than 720 (e.g., 759)
		nullptr, nullptr, hInstance, nullptr);

	RECT cr;
	GetClientRect(hWnd, &cr);
	m_Width = (float)(cr.right - cr.left);   // Should be EXACTLY 1280.0f
	m_Height = (float)(cr.bottom - cr.top);  // Should be EXACTLY 720.0f
	m_AspectRatio = m_Width / m_Height;      // Calculate Aspect Ratio

	m_GraphicsCore.Initialize(hWnd, m_Width, m_Height);
	m_ShaderHelper.Initialize();

	ID3D12GraphicsCommandList* cmdList = m_GraphicsCore.BeginFrame();

	m_Renderer.Initialize(m_GraphicsCore.GetDevice(), cmdList, &m_ShaderHelper);

	UINT numParticles = 1024; // Increased for visibility
	m_Solver.Initialize(m_GraphicsCore.GetDevice(), cmdList, numParticles, &m_ShaderHelper);

	m_GraphicsCore.EndFrame();
}