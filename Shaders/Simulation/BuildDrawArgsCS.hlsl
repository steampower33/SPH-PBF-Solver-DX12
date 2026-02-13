#include "DiffuseCommon.hlsli"

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    g_DrawArgs[0].VertexCountPerInstance = 6;
    g_DrawArgs[0].InstanceCount = g_Counters[0];
    g_DrawArgs[0].StartVertexLocation = 0;
    g_DrawArgs[0].StartInstanceLocation = 0;
}