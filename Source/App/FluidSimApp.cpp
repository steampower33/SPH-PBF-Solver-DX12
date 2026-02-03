#include "GameTimer.h"

#include "FluidSimApp.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool FluidSimApp::Run()
{
	GameTimer timer;
	timer.Reset();

	SM::Vector3 eye(0.0f, 0.0f, 30.0f);
	SM::Vector3 target(0.0f, 0.0f, 0.0f);
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
		}

		// --- Rendering ---
		ID3D12GraphicsCommandList* cmdList = m_GraphicsCore.BeginFrame();

		float simDt = 1.0f / 144.0f;
		m_Solver.Update(cmdList, simDt);

		m_Renderer.Render(cmdList, &m_Solver, view, proj);

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

	m_Solver.Initialize(m_GraphicsCore.GetDevice(), cmdList, &m_ShaderHelper);
	m_Gui.Initialize(
		m_GraphicsCore.GetDevice(),
		hWnd,
		GraphicsCore::FrameCount,
		m_GraphicsCore.GetCommandQueue()
	);

	m_GraphicsCore.EndFrame();
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