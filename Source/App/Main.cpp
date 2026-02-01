#include "FluidSimApp.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	FluidSimApp app;
	app.Initialize(hInstance);

	return app.Run();
}