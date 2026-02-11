#include "Common.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    float3 myPos = g_PosPred[id];
    float lambdaI = g_Lambda[id];
    float3 deltaPos = float3(0, 0, 0);

    float h = g_CellSize;
    float hSq = h * h;

    float k = g_k;
    float n = g_n;
    float dq = g_dqScale * h;
    float valAtDq = Poly6Kernel(dq * dq, h);
    
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

                    float3 neighborPos = g_PosPred[j];
                    float3 rVec = myPos - neighborPos;
                    float rSq = dot(rVec, rVec);

                    if (rSq < hSq && rSq > 1e-6f)
                    {
                        float r = sqrt(rSq);
                        float lambdaJ = g_Lambda[j];
                        
                        float3 gradW = normalize(rVec) * SpikyKernelGrad(r, h);

                        float lambdaSum = (lambdaI + lambdaJ);

                        // [Tensile Instability] s_corr
                        float wVal = Poly6Kernel(rSq, h);
        
                        // (W_curr / W_dq)
                        float ratio = wVal / valAtDq;
        
                        // s_corr = -k * (ratio ^ n)
                        float sCorr = -k * pow(ratio, n);

                        deltaPos += (lambdaSum + sCorr) * gradW;
                    }
                }
            }
        }
    }

    g_DeltaPos[id] = deltaPos / g_RestDensity;
}