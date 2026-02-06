#include "Common.hlsli"

RWStructuredBuffer<Particle> gParticles : register(u0);

cbuffer SortConstants : register(b1)
{
    uint g_BlockSize; // k
    uint g_Stride; // j
    uint g_Padding0;
    uint g_Padding1;
};

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    // 1. Calculate the 'Partner' index to compare with.
    uint partnerIndex = id ^ g_Stride;

    // 2. Decide the sort direction (Ascending or Descending).
    // In Bitonic sort, the direction flips based on the 'Block'.
    bool isAscending = (id & g_BlockSize) == 0;
    
    // 3. Compare Keys (Hash) and Swap if needed.
    // Note: To prevent race conditions (two threads swapping same pair),
    // usually we restrict the swap operation to the 'lower' index thread only.
    if (id < partnerIndex)
    {
        Particle myP = gParticles[id];
        Particle partnerP = gParticles[partnerIndex];

        uint myKey = GetGridHash(myP.Position);
        uint partnerKey = GetGridHash(partnerP.Position);
        
        bool needSwap = false;
        
        if (isAscending)
        {
            needSwap = (myKey > partnerKey);
        }
        else
        {
            needSwap = (myKey < partnerKey);
        }
        
        if (needSwap)
        {
            gParticles[id] = partnerP;
            gParticles[partnerIndex] = myP;
        }
    }
}