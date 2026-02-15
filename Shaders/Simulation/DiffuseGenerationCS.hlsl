#include "DiffuseCommon.hlsli"

float GetPotential(float val, float minVal, float maxVal)
{
    return saturate((val - minVal) / (maxVal - minVal));
}

float3 CalculateOrthonormal(float3 dir)
{
    if (length(dir) < 1e-5)
        return float3(1, 0, 0);
    
    dir = normalize(dir);
    float3 right = cross(float3(0, 1, 0), dir);
    if (length(right) < 1e-5)
        right = cross(float3(1, 0, 0), dir);
    return normalize(right);
}

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    uint numParticles = g_SP.NumParticles;
    
    if (id >= numParticles)
        return;

    if (id == 0)
        g_Counters[1] = 0;

    float3 pi = g_PosPred[id];
    float3 vi = g_VelOut[id];
    float dt = g_DP.DiffuseDeltaTime;

    float gridH = g_SP.CellSize;

    float checkH = g_SP.CellSize * g_DP.CellSizeScale;
    float checkHSq = checkH * checkH;

    float v_diff = 0.0;
    float3 normal = 0.0;
    
    int3 myGridPos = (int3) floor(pi / gridH) + int3(1000, 1000, 1000);
    
    int neighbourCount = 0;
    bool stopSearch = false;
    for (int z = -1; z <= 1 && !stopSearch; ++z)
    {
        for (int y = -1; y <= 1 && !stopSearch; ++y)
        {
            for (int x = -1; x <= 1 && !stopSearch; ++x)
            {
                if (neighbourCount >= g_DP.BubbleClassifyMinNeighbours)
                {
                    stopSearch = true;
                    break;
                }
                
                int3 neighborGridPos = myGridPos + int3(x, y, z);
                uint gridDim = g_SP.GridDim;
                uint neighborHash = ((uint) (neighborGridPos.x * 73856093) ^
                                     (uint) (neighborGridPos.y * 19349663) ^
                                     (uint) (neighborGridPos.z * 83492791)) % (gridDim * gridDim * gridDim);
                uint2 cellRange = g_GridIndices[neighborHash];

                for (uint j = cellRange.x; j < cellRange.y; ++j)
                {
                    if (id == j)
                        continue;
                    float3 pj = g_PosPred[j];
                    float3 rVec = pi - pj; // j -> i
                    float rSq = dot(rVec, rVec);

                    if (rSq < checkHSq && rSq > 1e-6)
                    {
                        float r = sqrt(rSq);
                        float3 dir_p = rVec / r;
                        
                        float3 v_ij = vi - g_VelOut[j];
                        float3 dir_v = normalize(v_ij);
                        float impact = length(v_ij) * (1.0 - dot(dir_v, dir_p));
                        float W = max(0.0, 1.0 - r / gridH);
                        v_diff += impact * W;
                        normal += dir_p * SpikyKernelGrad(r, gridH);
                        
                        neighbourCount++;
                    }
                }
            }
        }
    }

    float I_ta = GetPotential(v_diff, g_DP.TrappedAirMin, g_DP.TrappedAirMax);
    
    float3 n_hat = -normalize(normal); // reverse
    float curvature = saturate(dot(normalize(vi), n_hat));
    float I_wc = GetPotential(curvature, g_DP.WaveCrestMin, g_DP.WaveCrestMax);
    
    float energy = 0.5 * dot(vi, vi);
    float I_k = GetPotential(energy, g_DP.EnergyMin, g_DP.EnergyMax);

    float n_d = (g_DP.kTa * I_ta + g_DP.kWc * I_wc) * I_k * dt;
    n_d = min(n_d, 64.0);
    
    uint rngState = WangHash(id + uint(dt * 12345.0) + asuint(pi.x));
    
    int spawnCount = (int) n_d;
    float remainder = n_d - spawnCount;
    if (Random01(rngState) < remainder)
        spawnCount++;

    if (spawnCount > 0)
    {
        uint startIndex;
        InterlockedAdd(g_Counters[0], spawnCount, startIndex);

        int countToSpawn = min(spawnCount, (int) g_DP.MaxDiffuseParticles - (int) startIndex);
        
        if (countToSpawn > 0)
        {
            float3 cylinderBase = pi;
            float3 cylinderTop = pi + vi * dt;
            float3 axisU = CalculateOrthonormal(vi);
            float3 axisV = normalize(cross(axisU, normalize(vi + 1e-5)));
            float rVal = checkH;

            for (int k = 0; k < countToSpawn; ++k)
            {
                float randDist = sqrt(Random01(rngState)); // 0 ~ 1
                float randAngle = Random01(rngState) * 2.0 * 3.14159; // 0 ~ 2 pi
                float randHeight = Random01(rngState); // 0 ~ 1

                float3 radialOffset = rVal * randDist * (cos(randAngle) * axisU + sin(randAngle) * axisV);
                float3 spawnPos = cylinderBase + radialOffset + (cylinderTop - cylinderBase) * randHeight;

                DiffuseParticle p;
                p.PositionLife.xyz = spawnPos;
                p.PositionLife.w = lerp(0.5, g_DP.MaxLifeTime, Random01(rngState));

                p.VelocityScale.xyz = vi + radialOffset;
                p.VelocityScale.w = 0;

                g_DiffuseParticles[startIndex + k] = p;
            }
        }
    }
}