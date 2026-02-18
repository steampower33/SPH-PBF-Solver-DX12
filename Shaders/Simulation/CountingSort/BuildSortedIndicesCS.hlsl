#include "CountingSortCommon.hlsli"

#define DISPATCH_X 1024

[numthreads(DISPATCH_X, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;

    if (i >= g_SP.NumParticles)
        return;

    uint2 info = g_ParticleCellInfo[i];

    uint dst = g_CellStart[info.x] + info.y;

    g_RW_SortedIndices[dst] = i;
}