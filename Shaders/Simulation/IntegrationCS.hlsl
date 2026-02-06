#include "Common.hlsli"

RWStructuredBuffer<Particle> gParticles : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    Particle p = gParticles[id];
    
    float3 acceleration = float3(0, g_GravityY, 0) + float3(g_externalAccel, 0.0, 0.0);
    p.Velocity += acceleration * g_DeltaTime;

    p.OldPosition = p.Position;

    p.Position += p.Velocity * g_DeltaTime;
    
    gParticles[id] = p;
}