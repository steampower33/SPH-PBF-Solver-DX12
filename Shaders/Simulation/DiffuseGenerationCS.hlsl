#include "Common.hlsli"

static const uint g_MaxDiffuseParticles = 500000;

float GetPotential(float val, float minVal, float maxVal)
{
    return saturate((val - minVal) / (maxVal - minVal));
}

[numthreads(256, 1, 1)]
void GenerateDiffuse(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= g_NumParticles)
        return;

    float3 pi = g_PosPred[i];
    float3 vi = g_VelOut[i];
    
    float h = g_CellSize;
    int3 myGridPos = (int3) floor(pi / h) + int3(1000, 1000, 1000);
    
    float v_diff = 0.0;
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
                    if (i == j)
                        continue;
                    
                    float3 pj = g_PosPred[j];
                    float3 vj = g_VelOut[j];
                    
                    float3 p_ij = pi - pj;
                    float dist = length(p_ij);
                    float3 v_ij = vi - vj;
                    
                    float3 dir_p = normalize(p_ij);
                    float3 dir_v = normalize(v_ij);
                    
                    float impact = length(v_ij) * (1.0 - dot(dir_v, dir_p));
                    
                    float W = max(0.0, 1.0 - dist / g_CellSize);
                    
                    v_diff += impact * W;

                }
            }
        }
    }
    
    // 1. 잠재력(Potential) 계산 (I_ta, I_wc, I_k)
    float I_ta = GetPotential(v_diff, g_TrappedAirMin, g_TrappedAirMax);
    
    // 2. 생성 개수 계산
    float n_d = I_k * (k_ta * I_ta + k_wc * I_wc) * dt;
    uint count = (uint) n_d;
    if (frac(n_d) > Random(i))
        count++;

    // 3. 생성 (공간 확보)
    if (count > 0)
    {
        // 현재 확산 입자 수 확인 (꽉 찼으면 생성 불가)
        if (g_Counters[0] >= g_MaxDiffuseParticles)
            return;

        // 생성할 공간 예약 (Atomic)
        uint startIndex;
        InterlockedAdd(g_Counters[0], count, startIndex);

        // 예약된 공간이 Max를 넘지 않는지 체크
        if (startIndex + count <= g_MaxDiffuseParticles)
        {
            for (uint k = 0; k < count; ++k)
            {
                DiffuseParticle p;
                p.position = InitPosition(i); // 원통형 샘플링
                p.velocity = InitVelocity(i);
                p.life = g_MaxLifeTime;
                p.type = TYPE_FOAM; // 일단 Foam으로 시작
                
                g_DiffuseParticles[startIndex + k] = p;
            }
        }
    }
}