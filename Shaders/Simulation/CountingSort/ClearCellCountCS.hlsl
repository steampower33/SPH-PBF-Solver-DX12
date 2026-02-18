#include "CountingSortCommon.hlsli"

#define DISPATCH_X 1024

[numthreads(DISPATCH_X, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    g_RW_CellCount[DTid.x] = 0;
}