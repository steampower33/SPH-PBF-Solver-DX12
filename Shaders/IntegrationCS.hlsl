#include "Common.hlsli"

RWStructuredBuffer<Particle> gParticles : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    Particle p = gParticles[id];

    float3 gravity = float3(0.0f, -9.81f, 0.0f);
    p.Velocity += gravity * g_DeltaTime;

    p.OldPosition = p.Position;

    p.Position += p.Velocity * g_DeltaTime;

    p.Position.z = 0.0f;
    p.Velocity.z = 0.0f;
    
    float minX = g_Box.x;
    float maxX = g_Box.y;
    float minY = g_Box.z;
    float maxY = g_Box.w;
    
    if (p.Position.x < minX)
    {
        p.Position.x = minX;
        p.Velocity.x *= -0.5f;
    }
    else if (p.Position.x > maxX)
    {
        p.Position.x = maxX;
        p.Velocity.x *= -0.5f;
    }
    
    if (p.Position.y < minY)
    {
        p.Position.y = minY;
        p.Velocity.y *= -0.5f;
    }
    if (p.Position.y > maxY)
    {
        p.Position.y = maxY;
        p.Velocity.y *= -0.5f;
    }
    
    gParticles[id] = p;
}