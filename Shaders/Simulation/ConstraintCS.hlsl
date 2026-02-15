#include "SolverCommon.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    uint numParticles = g_SP.NumParticles;
    
    if (id >= numParticles)
        return;
    
    float3 p = g_RW_PosPred[id] + g_RW_DeltaPos[id];

    float radius = 0.1;
    
    float3 minBox = float3(g_SP.BoxX.x, g_SP.BoxY.x, g_SP.BoxZ.x) + radius;
    float3 maxBox = float3(g_SP.BoxX.y, g_SP.BoxY.y, g_SP.BoxZ.y) - radius;

    uint seed = id + uint(g_SP.DeltaTime * 12345.0);
    float randVal = Random01(seed);
    float jitter = randVal * g_SP.JitterFactor;

    if (p.x < minBox.x)
        p.x = minBox.x + jitter;
    else if (p.x > maxBox.x)
        p.x = maxBox.x - jitter;

    if (p.y < minBox.y)
        p.y = minBox.y + jitter;
    else if (p.y > maxBox.y)
        p.y = maxBox.y - jitter;

    if (p.z < minBox.z)
        p.z = minBox.z + jitter;
    else if (p.z > maxBox.z)
        p.z = maxBox.z - jitter;

    g_RW_PosPred[id] = p;
}