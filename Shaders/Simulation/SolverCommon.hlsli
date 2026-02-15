#ifndef SOLVER_COMMON_HLSLI
#define SOLVER_COMMON_HLSLI

#include "Common.hlsli"

RWStructuredBuffer<float3> g_RW_PosPred : register(u0);
RWStructuredBuffer<float3> g_RW_PosOld : register(u1);
RWStructuredBuffer<float3> g_RW_VelIn : register(u2);
RWStructuredBuffer<float3> g_RW_VelOut : register(u3);
RWStructuredBuffer<float> g_RW_Density : register(u4);
RWStructuredBuffer<float> g_RW_Lambda : register(u5);
RWStructuredBuffer<float3> g_RW_DeltaPos : register(u6);
RWStructuredBuffer<float3> g_RW_Vorticity : register(u7);
RWStructuredBuffer<uint2> g_RW_GridIndices : register(u8);
RWStructuredBuffer<uint> g_RW_SortedIndices : register(u9);

StructuredBuffer<float3> g_PosPred : register(t0);
StructuredBuffer<float3> g_PosOld : register(t1);
StructuredBuffer<float3> g_VelIn : register(t2);
StructuredBuffer<uint> g_SortedIndices : register(t3);
StructuredBuffer<float3> g_VelOut : register(t4);
StructuredBuffer<uint2> g_GridIndices : register(t5);
StructuredBuffer<float> g_Lambda : register(t6);
StructuredBuffer<float3> g_Vorticity : register(t7);

cbuffer CB_SimParams : register(b0)
{
    SimParams g_SP;
};

#endif