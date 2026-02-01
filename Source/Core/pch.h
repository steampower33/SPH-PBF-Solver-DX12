#pragma once

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

// DirectX 12 Core
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>

// Libs
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

#include "Helpers.h"
