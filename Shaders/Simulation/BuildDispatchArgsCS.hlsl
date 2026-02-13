#include "DiffuseCommon.hlsli"

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint countIndex = (g_ArgType == 0) ? 0 : 1;
    uint particleCount = g_Counters[countIndex];

    uint groupCount = (particleCount + 255) / 256;

    g_DispatchArgs[0].ThreadGroupCountX = groupCount;
    g_DispatchArgs[0].ThreadGroupCountY = 1;
    g_DispatchArgs[0].ThreadGroupCountZ = 1;
}