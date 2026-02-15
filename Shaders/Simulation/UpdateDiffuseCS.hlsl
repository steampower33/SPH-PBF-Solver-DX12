#include "DiffuseCommon.hlsli"

void ResolveCollisions(in uint id, inout float3 p, inout float3 v)
{
    float radius = 0.1;
    
    float3 minBox = float3(g_SP.BoxX.x, g_SP.BoxY.x, g_SP.BoxZ.x) + radius;
    float3 maxBox = float3(g_SP.BoxX.y, g_SP.BoxY.y, g_SP.BoxZ.y) - radius;

    float restitution = 0.1;
   
    if (p.x < minBox.x)
    {
        uint seed = id + uint(g_DP.DiffuseDeltaTime * 12345.0);
        float jitter = Random01(seed) * g_SP.JitterFactor;
        p.x = minBox.x + jitter;
        if (v.x < 0)
            v.x *= -restitution;
    }
    else if (p.x > maxBox.x)
    {
        uint seed = id + uint(g_DP.DiffuseDeltaTime * 12345.0);
        float jitter = Random01(seed) * g_SP.JitterFactor;
        p.x = maxBox.x - jitter;
        if (v.x > 0)
            v.x *= -restitution;
    }

    if (p.y < minBox.y)
    {
        uint seed = id + uint(g_DP.DiffuseDeltaTime * 12345.0);
        float jitter = Random01(seed) * g_SP.JitterFactor;
        p.y = minBox.y + jitter;
        if (v.y < 0)
            v.y *= -restitution;
    }
    else if (p.y > maxBox.y)
    {
        uint seed = id + uint(g_DP.DiffuseDeltaTime * 12345.0);
        float jitter = Random01(seed) * g_SP.JitterFactor;
        p.y = maxBox.y - jitter;
        if (v.y > 0)
            v.y *= -restitution;
    }

    if (p.z < minBox.z)
    {
        uint seed = id + uint(g_DP.DiffuseDeltaTime * 12345.0);
        float jitter = Random01(seed) * g_SP.JitterFactor;
        p.z = minBox.z + jitter;
        if (v.z < 0)
            v.z *= -restitution;
    }
    else if (p.z > maxBox.z)
    {
        uint seed = id + uint(g_DP.DiffuseDeltaTime * 12345.0);
        float jitter = Random01(seed) * g_SP.JitterFactor;
        p.z = maxBox.z - jitter;
        if (v.z > 0)
            v.z *= -restitution;
    }
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_Counters[0])
        return;

    DiffuseParticle p = g_DiffuseParticles[i];
    
    float dt = g_DP.DiffuseDeltaTime;
    
    float gridH = g_SP.CellSize;

    float checkH = g_SP.CellSize * g_DP.CellSizeScale;
    float checkHSq = checkH * checkH;

    int neighbourCount = 0;
    float3 velocitySum = 0;
    float weightSum = 0;
    
    float3 pi = p.PositionLife.xyz;
    int3 myGridPos = (int3) floor(pi / gridH) + int3(1000, 1000, 1000);
    
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
                    if (i == j)
                        continue;
                    
                    float3 pj = g_PosPred[j];
                    float3 rVec = pi - pj;
                    float rSq = dot(rVec, rVec);
                    
                    if (rSq < checkHSq && rSq > 1e-6f)
                    {
                        float W = Poly6Kernel(rSq, gridH);
    
                        velocitySum += g_VelOut[j] * W;
                        weightSum += W;

                        neighbourCount += 1;
                    }
                }
            }
        }
    }
    
    bool isSpray = neighbourCount <= g_DP.SprayClassifyMaxNeighbours;
    bool isBubble = neighbourCount >= g_DP.BubbleClassifyMinNeighbours;
    bool isFoam = !(isSpray || isBubble);
    float3 fluidAvgVel = (weightSum > 1e-5) ? (velocitySum * (1.0 / weightSum)) : float3(0, 0, 0);
    
    float gravityY = g_SP.GravityY;
    
    if (isFoam)
    {
        p.VelocityScale.xyz = fluidAvgVel;
        p.PositionLife.w -= dt;
        //p.VelocityScale.w = 1;
    }
    else if (isBubble)
    {
        float3 accelerationBuoyancy = float3(0, gravityY, 0) * (1.0 - g_DP.BubbleBuoyancy);
        float3 accelerationFluid = (fluidAvgVel - p.VelocityScale.xyz) * g_DP.FluidAccelMul;
        p.VelocityScale.xyz += (accelerationBuoyancy + accelerationFluid) * dt;
        //p.VelocityScale.w = 2;
    }
    else if (isSpray)
    {
        const float dragMultiplier = 0.04;
        float3 vel = p.VelocityScale.xyz;
        float sqrSpeed = dot(vel, vel);
        float3 drag = -normalize(vel) * sqrSpeed * dragMultiplier;
        p.VelocityScale.xyz += (float3(0, gravityY, 0) + drag) * dt;
        //p.VelocityScale.w = 3;
    }
    
    float targetScale = isBubble ? g_DP.BubbleScale : 1;
    p.VelocityScale.w = lerp(p.VelocityScale.w, targetScale, dt * g_DP.BubbleScaleChangeSpeed);
    p.PositionLife.xyz += p.VelocityScale.xyz * dt;

    ResolveCollisions(i, p.PositionLife.xyz, p.VelocityScale.xyz);
    
    if (p.PositionLife.w > 0)
    {
        uint destIdx;
        InterlockedAdd(g_Counters[1], 1, destIdx);
        g_DiffuseParticlesCompacted[destIdx] = p;
    }

    //bool isAlive = (p.PositionLife.w > 0);
    
    //uint waveActiveCount = WaveActiveCountBits(isAlive);
    //uint wavePrefixCount = WavePrefixCountBits(isAlive);
    
    //uint waveBaseIndex = 0;
    //if (WaveIsFirstLane())
    //{
    //    if (waveActiveCount > 0)
    //    {
    //        InterlockedAdd(g_Counters[1], waveActiveCount, waveBaseIndex);
    //    }
    //}
    
    //waveBaseIndex = WaveReadLaneFirst(waveBaseIndex);
    
    //if (isAlive)
    //{
    //    uint destIdx = waveBaseIndex + wavePrefixCount;
    //    g_DiffuseParticlesCompacted[destIdx] = p;
    //}
}