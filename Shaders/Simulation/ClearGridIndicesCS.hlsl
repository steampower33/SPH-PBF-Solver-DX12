#include "Common.hlsli"

// u1: Grid Indices
RWStructuredBuffer<uint2> g_GridIndices : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    // Total cells = GridDim * GridDim * GridDim
    uint numCells = g_GridDim * g_GridDim * g_GridDim;
    
    if (id >= numCells)
        return;

    // Reset to MAX_UINT (Invalid)
    g_GridIndices[id] = uint2(0xFFFFFFFF, 0xFFFFFFFF);
}