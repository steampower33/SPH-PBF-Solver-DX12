#include "Common.hlsli"

RWStructuredBuffer<Particle> g_Particles : register(u0);
RWStructuredBuffer<uint2> g_GridIndices : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    Particle p = g_Particles[id];

    float3 disp = p.Position - p.OldPosition;
    
    if (g_DeltaTime > 0.000001f)
    {
        p.Velocity = disp / g_DeltaTime;
    }
    else
    {
        p.Velocity = float3(0, 0, 0);
    }

    float3 viscosityForce = float3(0, 0, 0);
    float c = g_Viscosity;
    float h = g_CellSize;

    int3 myGridPos = (int3) floor(p.Position / h);
    myGridPos += int3(1000, 1000, 1000);

    for (int z = -1; z <= 1; ++z)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                int3 neighborGridPos = myGridPos + int3(x, y, z);
                uint neighborHash = ((uint) (neighborGridPos.x * 73856093) ^
                                     (uint) (neighborGridPos.y * 19349663) ^
                                     (uint) (neighborGridPos.z * 83492791)) % (g_GridDim * g_GridDim * g_GridDim);
                
                uint2 cellRange = g_GridIndices[neighborHash];
                if (cellRange.x >= cellRange.y)
                    continue;

                for (uint j = cellRange.x; j < cellRange.y; ++j)
                {
                    if (id == j)
                        continue;
                    
                    Particle pj = g_Particles[j];
                    float3 rVec = p.Position - pj.Position;
                    float rSq = dot(rVec, rVec);

                    if (rSq < h * h)
                    {
                        float3 v_diff = pj.Velocity - p.Velocity;
                        viscosityForce += v_diff * Poly6Kernel(rSq, h);
                    }
                }
            }
        }
    }

    p.Velocity += c * viscosityForce;

    g_Particles[id] = p;
}