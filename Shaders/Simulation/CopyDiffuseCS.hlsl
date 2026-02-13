#include "DiffuseCommon.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x == 0)
    {
        g_Counters[0] = g_Counters[1];
    }

    g_DiffuseParticles[id.x] = g_DiffuseParticlesCompacted[id.x];
}