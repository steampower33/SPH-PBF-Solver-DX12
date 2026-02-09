#include "Common.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;

    uint numCells = g_GridDim * g_GridDim * g_GridDim;
    
    if (id >= numCells)
        return;

    g_GridIndices[id] = uint2(0xFFFFFFFF, 0xFFFFFFFF);
}