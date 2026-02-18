#include "SolverCommon.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    
    uint numParticles = g_SP.NumParticles;
    
    if (id >= numParticles)
        return;
    
    float h = g_SP.CellSize;
    uint gridDim = g_SP.GridDim;
    
    float3 myPos = g_PosPred[id];
    uint myHash = GetGridHash(myPos, h, gridDim);

    // Boundary Check
    if (id == 0)
    {
        g_RW_GridIndices[myHash].x = id;
        return;
    }

    // Compare with Prev
    float3 prevPos = g_PosPred[id - 1];
    uint prevHash = GetGridHash(prevPos, h, gridDim);

    if (myHash != prevHash)
    {
        // Start of my cell
        g_RW_GridIndices[myHash].x = id;
        
        // End of prev cell
        g_RW_GridIndices[prevHash].y = id;
    }
    
    if (id == numParticles - 1)
    {
        g_RW_GridIndices[myHash].y = numParticles;
    }
}