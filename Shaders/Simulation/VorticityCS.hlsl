#include "Common.hlsli"

RWStructuredBuffer<Particle> g_Particles : register(u0);
RWStructuredBuffer<uint2> g_GridIndices : register(u1);
RWStructuredBuffer<float3> g_Vorticities : register(u4);

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    Particle p = g_Particles[id];
    float3 vorticity = float3(0, 0, 0);

    float h = g_CellSize;
    float hSq = h * h;
    int3 myGridPos = (int3) floor(p.Position / h) + int3(1000, 1000, 1000);

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
                
                for (uint j = cellRange.x; j < cellRange.y; ++j)
                {
                    if (id == j)
                        continue;

                    Particle pj = g_Particles[j];
                    float3 rVec = p.Position - pj.Position;
                    float rSq = dot(rVec, rVec);

                    if (rSq < hSq && rSq > 1e-6f)
                    {
                        float r = sqrt(rSq);
                        // Eq (15): curl = Sum( v_ij x gradW )
                        float3 v_ij = pj.Velocity - p.Velocity;
                        float3 gradW = normalize(rVec) * SpikyKernelGrad(r, h);

                        vorticity += cross(v_ij, gradW);
                    }
                }
            }
        }
    }
    
    g_Vorticities[id] = vorticity;
}