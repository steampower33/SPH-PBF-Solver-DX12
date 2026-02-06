#pragma once

#include "GraphicsCore.h"
#include "RendererManager.h"
#include "SphSolver.h"
#include "ShaderHelper.h"
#include "SimGui.h"
#include "Camera.h"

class FluidSimApp {
public:
	FluidSimApp() {}
	~FluidSimApp();

	// [Rule] System classes should NOT be copied.
	// Copying a core system creates ambiguity in resource ownership.
	FluidSimApp(const FluidSimApp&) = delete;
	FluidSimApp& operator=(const FluidSimApp&) = delete;

	void Initialize(HINSTANCE hInstance);

	bool Run();

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	GraphicsCore m_GraphicsCore;
	RendererManager	 m_RendererManager;
	SphSolver    m_Solver;
	ShaderHelper m_ShaderHelper;
	SimGui       m_Gui;
	Camera		 m_Camera;

private:
	float m_Width = 1280.0f;
	float m_Height = 720.0f;
	float m_AspectRatio = 0.0f;

	static std::wstring GetLatestWinPixGpuCapturerPath();
};