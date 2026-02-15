#include "SolverCommon.hlsli"

#define BITONIC_BLOCK_SIZE 1024
#define NUM_ELEMENTS (BITONIC_BLOCK_SIZE * 2)

groupshared uint s_Indices[NUM_ELEMENTS];
groupshared uint s_Hashes[NUM_ELEMENTS];

[numthreads(BITONIC_BLOCK_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex, uint3 GID : SV_GroupID)
{
    uint tID = GI;
    uint startOffset = GID.x * NUM_ELEMENTS;
    
    uint idx1 = startOffset + tID;
    uint idx2 = startOffset + tID + BITONIC_BLOCK_SIZE;

    uint numParticles = g_SP.NumParticles;
    
    if (idx1 < numParticles)
    {
        uint pID = g_RW_SortedIndices[idx1];
        s_Indices[tID] = pID;
        s_Hashes[tID] = GetGridHash(g_PosPred[pID], g_SP.CellSize, g_SP.GridDim);
    }
    else
    {
        s_Indices[tID] = 0xFFFFFFFF;
        s_Hashes[tID] = 0xFFFFFFFF;
    }

    if (idx2 < numParticles)
    {
        uint pID = g_RW_SortedIndices[idx2];
        s_Indices[tID + BITONIC_BLOCK_SIZE] = pID;
        s_Hashes[tID + BITONIC_BLOCK_SIZE] = GetGridHash(g_PosPred[pID], g_SP.CellSize, g_SP.GridDim);
    }
    else
    {
        s_Indices[tID + BITONIC_BLOCK_SIZE] = 0xFFFFFFFF;
        s_Hashes[tID + BITONIC_BLOCK_SIZE] = 0xFFFFFFFF;
    }

    GroupMemoryBarrierWithGroupSync();

    bool bGroupAscending = (GID.x % 2 == 0);
    for (uint k = 2; k <= NUM_ELEMENTS; k <<= 1)
    {
        for (uint j = k >> 1; j > 0; j >>= 1)
        {
            uint index = (tID & ~(j - 1)) * 2 + (tID & (j - 1));
            uint partner = index + j;

            bool ascending = ((index & k) == 0) ^ (!bGroupAscending);
            
            uint hashA = s_Hashes[index];
            uint hashB = s_Hashes[partner];
            uint idA = s_Indices[index];
            uint idB = s_Indices[partner];

            bool swap = false;
            if (hashA != hashB)
                swap = (hashA > hashB) == ascending;
            else
                swap = (idA > idB) == ascending;

            if (swap)
            {
                s_Indices[index] = idB;
                s_Indices[partner] = idA;
                s_Hashes[index] = hashB;
                s_Hashes[partner] = hashA;
            }

            GroupMemoryBarrierWithGroupSync();
        }
    }

    if (idx1 < numParticles)
        g_RW_SortedIndices[idx1] = s_Indices[tID];
    if (idx2 < numParticles)
        g_RW_SortedIndices[idx2] = s_Indices[tID + BITONIC_BLOCK_SIZE];
}