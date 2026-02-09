#include "Common.hlsli"

// [Bitonic Sort Constants]
cbuffer SortConstants : register(b1)
{
    uint g_BlockSize;
    uint g_Stride;
};

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    uint index = id;
    uint partner = id ^ g_Stride;

    if (partner < index || partner >= g_NumParticles)
        return;

    bool ascending = (id & g_BlockSize) == 0;

    uint particleID_A = g_SortedIndices[index];
    uint particleID_B = g_SortedIndices[partner];

    float3 posA = g_PosPred[particleID_A];
    float3 posB = g_PosPred[particleID_B];

    uint hashA = GetGridHash(posA);
    uint hashB = GetGridHash(posB);

    bool swap = false;
    
    if (hashA != hashB)
    {
        swap = (hashA > hashB) == ascending;
    }
    else
    {
        swap = (particleID_A > particleID_B) == ascending;
    }

    if (swap)
    {
        g_SortedIndices[index] = particleID_B;
        g_SortedIndices[partner] = particleID_A;
    }
}