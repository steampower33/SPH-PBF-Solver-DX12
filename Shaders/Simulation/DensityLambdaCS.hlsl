#include "Common.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    float3 myPos = g_PosPred[id];
    
    float h = g_CellSize;
    float hSq = h * h;
    
    float density = 0.0;
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
                    float3 neighborPos = g_PosPred[j];
                    float3 rVec = myPos - neighborPos;
                    float rSq = dot(rVec, rVec);

                    if (rSq < hSq)
                    {
                        density += Poly6Kernel(rSq, h) * g_Mass;

                        if (id != j && rSq > 1e-6f)
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

    g_Density[id] = density;
    g_Lambda[id] = lambda;
}