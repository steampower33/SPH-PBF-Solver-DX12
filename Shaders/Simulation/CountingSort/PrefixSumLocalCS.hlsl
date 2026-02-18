#include "CountingSortCommon.hlsli"

#define DISPATCH_X 1024

groupshared uint shMem[DISPATCH_X];

[numthreads(DISPATCH_X, 1, 1)]
void main(
    uint tid : SV_GroupThreadID,
    uint3 DTid : SV_DispatchThreadID,
    uint groupIdx : SV_GroupID
)
{
    uint i = DTid.x;

    uint gridDim = g_SP.GridDim;
    uint cellCnt = gridDim * gridDim * gridDim;
    
    uint localValue = 0;
    if (i < cellCnt)
    {
        localValue = g_CellCount[i];
    }

    shMem[tid] = localValue;

    GroupMemoryBarrierWithGroupSync();

    for (uint dUp = 1; dUp < DISPATCH_X; dUp <<= 1)
    {
        uint idx = (tid + 1) * (dUp << 1) - 1;
        if (idx < DISPATCH_X)
        {
            uint prev = idx - dUp;
            shMem[idx] += shMem[prev];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (tid == 0)
    {
        shMem[DISPATCH_X - 1] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint dDown = DISPATCH_X >> 1; dDown > 0; dDown >>= 1)
    {
        uint idx = (tid + 1) * (dDown << 1) - 1;
        if (idx < DISPATCH_X)
        {
            uint prev = idx - dDown;
            uint temp = shMem[prev];
            shMem[prev] = shMem[idx];
            shMem[idx] += temp;
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (i < cellCnt)
    {
        g_RW_CellStart[i] = shMem[tid];
    }

    GroupMemoryBarrierWithGroupSync();

    if (tid == DISPATCH_X - 1)
    {
        uint totalGroupSum = shMem[tid] + localValue;
        g_RW_PartialSum[groupIdx] = totalGroupSum;
    }
}
