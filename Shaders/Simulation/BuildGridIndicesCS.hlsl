#include "Common.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    float3 myPos = g_PosPred[id];
    uint myHash = GetGridHash(myPos);

    // Boundary Check
    if (id == 0)
    {
        g_GridIndices[myHash].x = id;
        return;
    }

    // Compare with Prev
    float3 prevPos = g_PosPred[id - 1];
    uint prevHash = GetGridHash(prevPos);

    if (myHash != prevHash)
    {
        // Start of my cell
        g_GridIndices[myHash].x = id;
        
        // End of prev cell
        g_GridIndices[prevHash].y = id;
    }
    
    if (id == g_NumParticles - 1)
    {
        g_GridIndices[myHash].y = g_NumParticles;
    }
}