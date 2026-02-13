#ifndef SOLVER_COMMON_HLSLI
#define SOLVER_COMMON_HLSLI

#include "Common.hlsli"

RWStructuredBuffer<float3> g_PosPred : register(u0);
RWStructuredBuffer<float3> g_PosOld : register(u1);
RWStructuredBuffer<float3> g_VelIn : register(u2);
RWStructuredBuffer<float3> g_VelOut : register(u3);
RWStructuredBuffer<float> g_Density : register(u4);
RWStructuredBuffer<float> g_Lambda : register(u5);
RWStructuredBuffer<float3> g_DeltaPos : register(u6);
RWStructuredBuffer<float3> g_Vorticity : register(u7);
RWStructuredBuffer<uint2> g_GridIndices : register(u8);
RWStructuredBuffer<uint> g_SortedIndices : register(u9);

cbuffer CB_SimParams : register(b0)
{
    SimParams g_SP;
};

#endif