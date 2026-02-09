#include "Common.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) 
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    float3 pos = g_PosPred[id];
    float3 vel = g_VelOut[id];
    
    float3 acceleration = float3(0, g_GravityY, 0) + float3(g_externalAccel, 0.0, 0.0);
    vel += acceleration * g_DeltaTime;

    g_PosOld[id] = pos;
    g_PosPred[id] = pos + vel * g_DeltaTime;
    g_VelIn[id] = vel;
}