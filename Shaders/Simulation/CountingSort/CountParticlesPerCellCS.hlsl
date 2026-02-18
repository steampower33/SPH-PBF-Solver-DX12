#include "CountingSortCommon.hlsli"

#define DISPATCH_X 1024

[numthreads(DISPATCH_X, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= g_SP.NumParticles)
        return;
    
    float3 pos = g_PosPred[i];

    float h = g_SP.CellSize;
    uint gridDim = g_SP.GridDim;

    uint hash = GetGridHash(pos, h, gridDim);
    
    uint localOff;
    InterlockedAdd(g_RW_CellCount[hash], 1, localOff);

    g_RW_ParticleCellInfo[i] = uint2(hash, localOff);
}