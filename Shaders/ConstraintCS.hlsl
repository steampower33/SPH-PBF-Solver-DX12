#include "Common.hlsli"

RWStructuredBuffer<Particle> gParticles : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    Particle p = gParticles[id];

    float3 minBox = float3(g_BoxX.x, g_BoxY.x, g_BoxZ.x);
    float3 maxBox = float3(g_BoxX.y, g_BoxY.y, g_BoxZ.y);
    
    float epsilon = 0.001f;
    p.Position = max(p.Position, minBox + epsilon);
    p.Position = min(p.Position, maxBox - epsilon);

    gParticles[id] = p;
}