#include "SolverCommon.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    
    uint numParticles = g_SP.NumParticles;
    
    if (id >= numParticles)
        return;

    float3 pi = g_PosPred[id];
    float li = g_Lambda[id];
    float3 deltaPos = float3(0, 0, 0);
    
    float restDensity = g_SP.RestDensity;
    float h = g_SP.CellSize;
    float hSq = h * h;

    float k = g_SP.k;
    float n = g_SP.n;
    float dq = g_SP.DqScale * h;
    float valAtDq = Poly6Kernel(dq * dq, h);
    
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
                
                if (cellRange.x >= cellRange.y || cellRange.y > numParticles)
                    continue;

                for (uint j = cellRange.x; j < cellRange.y; ++j)
                {
                    if (id == j)
                        continue;

                    float3 pj = g_PosPred[j];
                    float3 rVec = pi - pj;
                    float rSq = dot(rVec, rVec);

                    if (rSq < hSq && rSq > 1e-6f)
                    {
                        float r = sqrt(rSq);
                        float lj = g_Lambda[j];
                        
                        float3 gradW = normalize(rVec) * SpikyKernelGrad(r, h);

                        float lambdaSum = (li + lj);

                        // [Tensile Instability] s_corr
                        float wVal = Poly6Kernel(rSq, h);
        
                        // (W_curr / W_dq)
                        float ratio = wVal / valAtDq;
        
                        // s_corr = -k * (ratio ^ n)
                        float sCorr = -k * pow(max(ratio, 0.0001), n);

                        deltaPos += (lambdaSum + sCorr) * gradW;
                    }
                }
            }
        }
    }

    g_DeltaPos[id] = deltaPos / restDensity;
}