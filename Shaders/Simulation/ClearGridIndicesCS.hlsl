#include "Common.hlsli"

RWStructuredBuffer<uint2> g_RW_GridIndices : register(u8);

cbuffer CB_SimParams : register(b0)
{
    SimParams g_SP;
};

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;

    uint numCells = g_SP.GridDim * g_SP.GridDim * g_SP.GridDim;
    
    if (id >= numCells)
        return;

    g_RW_GridIndices[id] = uint2(0xFFFFFFFF, 0xFFFFFFFF);
}