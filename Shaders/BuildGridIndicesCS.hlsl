#include "Common.hlsli"

RWStructuredBuffer<Particle> g_Particles : register(u0);
RWStructuredBuffer<uint2> g_GridIndices : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    // 1. My Hash
    float3 myPos = g_Particles[id].Position;
    uint myHash = GetGridHash(myPos);

    // 2. Boundary Check
    if (id == 0)
    {
        g_GridIndices[myHash].x = id;
        return;
    }

    // 3. Compare with Prev
    float3 prevPos = g_Particles[id - 1].Position;
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