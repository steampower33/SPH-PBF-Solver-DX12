#include "SolverCommon.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    
    uint numParticles = g_SP.NumParticles;
    
    if (id >= numParticles)
        return;

    float3 pi = g_PosPred[id];
    
    float h = g_SP.CellSize;
    float hSq = h * h;
    
    float mass = g_SP.Mass;
    float restDensity = g_SP.RestDensity;
    float density = mass * Poly6Kernel(0.0, h);
    float3 gradCiSum = float3(0, 0, 0);
    float sumGradCiSq = 0.0;

    int3 myGridPos = (int3) floor(pi / h);
    myGridPos += int3(1000, 1000, 1000);

    for (int z = -1; z <= 1; ++z)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                int3 neighborGridPos = myGridPos + int3(x, y, z);
                
                uint gridDim = g_SP.GridDim;
                uint neighborHash = ((uint) (neighborGridPos.x * 73856093) ^
                                     (uint) (neighborGridPos.y * 19349663) ^
                                     (uint) (neighborGridPos.z * 83492791)) % (gridDim * gridDim * gridDim);

                uint2 cellRange = g_GridIndices[neighborHash];
                
                if (cellRange.x >= cellRange.y)
                    continue;
                if (cellRange.y > numParticles)
                    continue;

                for (uint j = cellRange.x; j < cellRange.y; ++j)
                {
                    float3 pj = g_PosPred[j];
                    float3 rVec = pi - pj;
                    float rSq = dot(rVec, rVec);
                    
                    if (rSq < hSq && rSq > 1e-6)
                    {
                        density += Poly6Kernel(rSq, h) * mass;
                        
                        if (id != j)
                        {
                            float r = sqrt(rSq);
                            
                            float3 gradW = normalize(rVec) * SpikyKernelGrad(r, h);
                            
                            float3 gradC_j = -gradW * mass / restDensity;
                            
                            sumGradCiSq += dot(gradC_j, gradC_j);
                            gradCiSum -= gradC_j;
                        }
                    }
                }
            }
        }
    }

    sumGradCiSq += dot(gradCiSum, gradCiSum);

    float C = max(density / restDensity - 1.0, 0.0);
    
    float lambda = 0.0;
    if (C != 0.0)
    {
        lambda = -C / (sumGradCiSq + g_SP.Epsilon);
    }

    g_Density[id] = density;
    g_Lambda[id] = lambda;
}