#pragma once

#include "GraphicsCore.h"
#include "Renderer.h"
#include "SphSolver.h"
#include "ShaderHelper.h"

class FluidSimApp {
public:
	FluidSimApp() {}
	~FluidSimApp() {}

	// [Rule] System classes should NOT be copied.
	// Copying a core system creates ambiguity in resource ownership.
	FluidSimApp(const FluidSimApp&) = delete;
	FluidSimApp& operator=(const FluidSimApp&) = delete;

	void Initialize(HINSTANCE hInstance);

	bool Run();

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	GraphicsCore m_GraphicsCore;
	Renderer m_Renderer;
	SphSolver m_Solver;
	ShaderHelper m_ShaderHelper;

private:
	float m_Width = 1280.0f;
	float m_Height = 720.0f;
	float m_AspectRatio = 0.0f;
	float m_ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f, };
};