#include "Common.hlsli"

StructuredBuffer<float3> g_PosPred_In : register(t0);
StructuredBuffer<float3> g_PosOld_In : register(t1);
StructuredBuffer<float3> g_Vel_In : register(t2);
StructuredBuffer<uint> g_SortedIndices : register(t3);

RWStructuredBuffer<float3> g_TempPosPred : register(u0);
RWStructuredBuffer<float3> g_TempPosOld : register(u1);
RWStructuredBuffer<float3> g_TempVel : register(u2);

cbuffer CB_SimParams : register(b0)
{
    SimParams g_SP;
};

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_SP.NumParticles)
        return;

    uint oldID = g_SortedIndices[id];

    g_TempPosPred[id] = g_PosPred_In[oldID];
    g_TempPosOld[id] = g_PosOld_In[oldID];
    g_TempVel[id] = g_Vel_In[oldID];
}