#include "GameTimer.h"

#include "FluidSimApp.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool FluidSimApp::Run()
{
	GameTimer timer;
	timer.Reset();

	float timeAccumulator = 0.0f;

	bool bIsExit = false;

	while (bIsExit == false)
	{
		timer.Tick();

		float dt = timer.GetDeltaTime();
		timeAccumulator += dt;
		if (timeAccumulator > 0.1f) timeAccumulator = 0.1f;

		MSG msg;
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT) bIsExit = true;
		}
		if (bIsExit) break;

		// --- Update ---
		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
		{
			PostQuitMessage(0);
		}
		if (GetAsyncKeyState(VK_SPACE) & 0x0001)
		{
			m_Gui.m_IsPaused = !m_Gui.m_IsPaused;
		}

		m_Camera.Update(dt);

		// --- Rendering ---
		ID3D12GraphicsCommandList* cmdList = m_GraphicsCore.BeginFrame();

		float simDt = m_Solver.m_SimParams.DeltaTime;

		if (!m_Gui.m_IsPaused)
		{
			while (timeAccumulator >= simDt)
			{
				m_Solver.Update(cmdList);
				timeAccumulator -= simDt;
			}
		}
		else
		{
			timeAccumulator = 0.0f;
		}
		
		m_Renderer.Render(cmdList, &m_Solver, m_Camera.GetViewMatrix(), m_Camera.GetProjectionMatrix());

		m_Gui.BeginFrame();
		m_Gui.DrawControlPanel(&m_Solver, &m_Renderer);
		m_Gui.EndFrame(cmdList);

		m_GraphicsCore.EndFrame();
	}

	return true;
}

LRESULT CALLBACK FluidSimApp::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
		return true;

	bool isImGuiHovered = false;
	if (ImGui::GetCurrentContext() != nullptr)
	{
		if (ImGui::GetIO().WantCaptureMouse)
			isImGuiHovered = true;
	}

	FluidSimApp* g_AppInstance = reinterpret_cast<FluidSimApp*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
	if (g_AppInstance)
	{
		if (!isImGuiHovered)
		{
			switch (message)
			{
			case WM_LBUTTONDOWN:
			case WM_MBUTTONDOWN:
			case WM_RBUTTONDOWN:
				g_AppInstance->m_Camera.OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
				return 0;

			case WM_MOUSEWHEEL:
				g_AppInstance->m_Camera.OnMouseWheel((float)GET_WHEEL_DELTA_WPARAM(wParam));
				return 0;
			}
		}

		switch (message)
		{
		case WM_LBUTTONUP:
		case WM_MBUTTONUP:
		case WM_RBUTTONUP:
			g_AppInstance->m_Camera.OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;

		case WM_MOUSEMOVE:
			g_AppInstance->m_Camera.OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		}
	}

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

FluidSimApp::~FluidSimApp()
{
	m_GraphicsCore.WaitForGpu();
	m_Gui.Shutdown();
}

void FluidSimApp::Initialize(HINSTANCE hInstance)
{
	// Check to see if a copy of WinPixGpuCapturer.dll has already been injected into the application.
	// This may happen if the application is launched through the PIX UI.
	if (GetModuleHandle(L"WinPixGpuCapturer.dll") == 0)
	{
		LoadLibrary(GetLatestWinPixGpuCapturerPath().c_str());
	}

	WCHAR WindowClass[] = L"SPH-PBF-Solver-DX12";
	WCHAR Title[] = L"SPH-PBF-Solver-DX12";

	WNDCLASSW wndclass = { 0, WndProc, 0, 0, 0, 0, 0, 0, 0, WindowClass };
	RegisterClassW(&wndclass);

	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

	m_Width = static_cast<float>(screenWidth) * 0.9f;
	m_Height = m_Width * 9.0f / 16.0f;

	DWORD dwStyle = WS_OVERLAPPEDWINDOW;

	RECT wr = { 0, 0, (LONG)m_Width, (LONG)m_Height };
	AdjustWindowRect(&wr, dwStyle, FALSE);

	int windowWidth = wr.right - wr.left;
	int windowHeight = wr.bottom - wr.top;

	int xPos = (screenWidth - windowWidth) / 2;
	int yPos = (screenHeight - windowHeight) / 2;

	HWND hWnd = CreateWindowExW(0, WindowClass, Title,
		dwStyle | WS_VISIBLE,
		xPos, yPos,
		windowWidth,
		windowHeight,
		nullptr, nullptr, hInstance, nullptr);

	SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)this);

	RECT cr;
	GetClientRect(hWnd, &cr);
	m_Width = (float)(cr.right - cr.left);
	m_Height = (float)(cr.bottom - cr.top);
	m_AspectRatio = m_Width / m_Height;

	m_Solver.m_SimParams.DeltaTime = 1.0f / 144.0f;

	m_GraphicsCore.Initialize(hWnd, m_Width, m_Height);
	m_ShaderHelper.Initialize();

	ID3D12GraphicsCommandList* cmdList = m_GraphicsCore.BeginFrame();

	m_Renderer.Initialize(m_GraphicsCore.GetDevice(), cmdList, &m_ShaderHelper);

	m_Solver.Initialize(m_GraphicsCore.GetDevice(), cmdList, &m_ShaderHelper);
	m_Gui.Initialize(
		m_GraphicsCore.GetDevice(),
		hWnd,
		GraphicsCore::FrameCount,
		m_GraphicsCore.GetCommandQueue(),
		m_Width,
		m_Height
	);

	m_GraphicsCore.EndFrame();

	m_Camera.Initialize(m_AspectRatio);
}

std::wstring FluidSimApp::GetLatestWinPixGpuCapturerPath()
{
	LPWSTR programFilesPath = nullptr;
	SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath);

	std::wstring pixSearchPath = programFilesPath + std::wstring(L"\\Microsoft PIX\\*");

	WIN32_FIND_DATA findData;
	bool foundPixInstallation = false;
	wchar_t newestVersionFound[MAX_PATH] = L"";

	HANDLE hFind = FindFirstFile(pixSearchPath.c_str(), &findData);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) &&
				(findData.cFileName[0] != '.'))
			{
				if (!foundPixInstallation || wcscmp(newestVersionFound, findData.cFileName) <= 0)
				{
					foundPixInstallation = true;
					StringCchCopy(newestVersionFound, _countof(newestVersionFound), findData.cFileName);
				}
			}
		} while (FindNextFile(hFind, &findData) != 0);
	}

	FindClose(hFind);

	if (!foundPixInstallation)
	{
		return L"";
	}

	wchar_t output[MAX_PATH];
	StringCchCopy(output, pixSearchPath.length(), pixSearchPath.data());
	StringCchCat(output, MAX_PATH, &newestVersionFound[0]);
	StringCchCat(output, MAX_PATH, L"\\WinPixGpuCapturer.dll");

	return std::wstring(output);
}