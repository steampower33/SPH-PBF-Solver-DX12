#pragma once

#define NOMINMAX

// Windows & Standard
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl.h> 
#include <vector>
#include <string>
#include <memory>
#include <iostream>
#include <algorithm>
#include <assert.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <shlobj.h>
#include <strsafe.h>
#include "windowsx.h"

// DirectX 12 Core
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <dxcapi.h>

// Libs
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

#include "Helpers.h"

using Microsoft::WRL::ComPtr;
namespace DX = DirectX;

#include "SimpleMath.h"
namespace SM = DirectX::SimpleMath;

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
