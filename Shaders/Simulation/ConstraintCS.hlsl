#include "Common.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    float3 p = g_PosPred[id] + g_DeltaPos[id];

    float3 minBox = float3(g_BoxX.x, g_BoxY.x, g_BoxZ.x);
    float3 maxBox = float3(g_BoxX.y, g_BoxY.y, g_BoxZ.y);
    
    p = max(p, minBox);
    p = min(p, maxBox);

    g_PosPred[id] = p;
}