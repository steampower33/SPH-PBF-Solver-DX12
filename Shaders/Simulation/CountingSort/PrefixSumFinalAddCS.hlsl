#include "CountingSortCommon.hlsli"

#define DISPATCH_X 1024

[numthreads(DISPATCH_X, 1, 1)]
void main(
    uint tid : SV_GroupThreadID,
    uint3 DTid : SV_DispatchThreadID,
    uint groupIdx : SV_GroupID
)
{
    uint i = DTid.x;

    g_RW_CellStart[i] += g_PartialSum[groupIdx];
}