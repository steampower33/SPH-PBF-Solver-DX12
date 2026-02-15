#include "SolverCommon.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_SP.NumParticles)
        return;

    float3 pos = g_RW_PosPred[id];
    float3 vel = g_RW_VelOut[id];
    
    float3 acceleration = float3(0, g_SP.GravityY, 0) + float3(g_SP.ExternalAccel, 0.0, 0.0);
    float dt = g_SP.DeltaTime;
    vel += acceleration * dt;

    g_RW_PosOld[id] = pos;
    g_RW_PosPred[id] = pos + vel * dt;
    g_RW_VelIn[id] = vel;
}