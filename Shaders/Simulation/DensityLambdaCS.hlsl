#include "Common.hlsli"

RWStructuredBuffer<Particle> g_Particles : register(u0);
RWStructuredBuffer<uint2> g_GridIndices : register(u1);
RWStructuredBuffer<float> g_Densities : register(u2);
RWStructuredBuffer<float> g_Lambdas : register(u3);

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    float3 myPos = g_Particles[id].Position;
    
    float h = g_CellSize;
    float hSq = h * h;
    
    float density = g_Mass * Poly6Kernel(0.0, h);
    float3 gradCiSum = float3(0, 0, 0);
    float sumGradCiSq = 0.0;

    int3 myGridPos = (int3) floor(myPos / g_CellSize);
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
                if (cellRange.y > g_NumParticles)
                    continue;

                for (uint j = cellRange.x; j < cellRange.y; ++j)
                {
                    float3 neighborPos = g_Particles[j].Position;
                    float3 rVec = myPos - neighborPos;
                    float rSq = dot(rVec, rVec);

                    if (rSq < hSq && rSq > 1e-6)
                    {
                        density += Poly6Kernel(rSq, h) * g_Mass;

                        if (id != j)
                        {
                            float r = sqrt(rSq);
                            
                            float3 gradW = normalize(rVec) * SpikyKernelGrad(r, h);
                            
                            float3 gradC_j = -gradW * g_Mass / g_RestDensity;
                            
                            sumGradCiSq += dot(gradC_j, gradC_j);
                            gradCiSum -= gradC_j;
                        }
                    }
                }
            }
        }
    }

    sumGradCiSq += dot(gradCiSum, gradCiSum);

    float C = max(density / g_RestDensity - 1.0, 0.0);
    
    float lambda = 0.0;
    if (C != 0.0)
    {
        lambda = -C / (sumGradCiSq + g_epsilon);
    }

    g_Densities[id] = density;
    g_Lambdas[id] = lambda;
    
    // [DEBUG]
    g_Particles[id].Density = density;
}