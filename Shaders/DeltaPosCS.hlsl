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
    float lambdaI = g_Lambdas[id];
    float3 deltaPos = float3(0, 0, 0);

    float h = g_CellSize;
    float hSq = h * h;

    // Eq (13) : W(dq, h)
    float dq_len = g_dqScale * h;
    float w_dq = Poly6Kernel(dq_len * dq_len, h);
    
    float k = g_k;
    float n = g_n;
    
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
                
                if (cellRange.x >= cellRange.y || cellRange.y > g_NumParticles)
                    continue;

                for (uint j = cellRange.x; j < cellRange.y; ++j)
                {
                    if (id == j)
                        continue;

                    float3 neighborPos = g_Particles[j].Position;
                    float3 rVec = myPos - neighborPos;
                    float rSq = dot(rVec, rVec);

                    if (rSq < hSq && rSq > 1e-6f)
                    {
                        float r = sqrt(rSq);
                        float lambdaJ = g_Lambdas[j];
                        
                        // W(pi - pj, h)
                        float w_ij = Poly6Kernel(rSq, h);
                        
                        // s_corr = -k * (W(rij) / W(dq))^n
                        float sCorr = -k * pow(w_ij / w_dq, n);
                        
                        float3 gradW = normalize(rVec) * SpikyKernelGrad(r, h);

                        deltaPos += (lambdaI + lambdaJ + sCorr) * gradW;
                    }
                }
            }
        }
    }

    g_Particles[id].Position += deltaPos / g_RestDensity;
}