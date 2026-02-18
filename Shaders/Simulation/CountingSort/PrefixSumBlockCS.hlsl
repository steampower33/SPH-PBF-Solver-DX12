#include "CountingSortCommon.hlsli"

#define DISPATCH_X 1024

groupshared uint shMem[2048];

[numthreads(DISPATCH_X, 1, 1)]
void main(
    uint tid : SV_GroupThreadID,
    uint3 DTid : SV_DispatchThreadID,
    uint groupIdx : SV_GroupID
)
{
    uint numPartialSums = g_SP.NumPartialSums;
    
    uint idx1 = tid;
    uint idx2 = tid + DISPATCH_X;

    if (idx1 < numPartialSums)
        shMem[idx1] = g_PartialSum[idx1];
    else
        shMem[idx1] = 0;

    if (idx2 < numPartialSums)
        shMem[idx2] = g_PartialSum[idx2];
    else
        shMem[idx2] = 0;

    GroupMemoryBarrierWithGroupSync();

    if (tid == 0)
    {
        uint sum = 0;
        for (uint i = 0; i < numPartialSums; ++i)
        {
            uint temp = shMem[i];
            shMem[i] = sum;
            sum += temp;
        }
    }
    
    GroupMemoryBarrierWithGroupSync();

    if (idx1 < numPartialSums)
        g_RW_PartialSum[idx1] = shMem[idx1];
    if (idx2 < numPartialSums)
        g_RW_PartialSum[idx2] = shMem[idx2];
}