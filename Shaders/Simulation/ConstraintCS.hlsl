#include "Common.hlsli"

float hash(uint n)
{
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 789221U) + 1376312589U;
    return float(n & 0x7fffffffU) / float(0x7fffffff);
}

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    float3 p = g_PosPred[id] + g_DeltaPos[id];

    float radius = 0.1;
    
    float3 minBox = float3(g_BoxX.x, g_BoxY.x, g_BoxZ.x) + radius;
    float3 maxBox = float3(g_BoxX.y, g_BoxY.y, g_BoxZ.y) - radius;

    float randVal = hash(id);
    float jitter = randVal * g_JitterFactor;

    if (p.x < minBox.x)
        p.x = minBox.x + jitter;
    else if (p.x > maxBox.x)
        p.x = maxBox.x - jitter;

    if (p.y < minBox.y)
    {
        p.y = minBox.y + jitter;
    }
    else if (p.y > maxBox.y)
    {
        p.y = maxBox.y - jitter;
    }

    if (p.z < minBox.z)
        p.z = minBox.z + jitter;
    else if (p.z > maxBox.z)
        p.z = maxBox.z - jitter;

    g_PosPred[id] = p;
}