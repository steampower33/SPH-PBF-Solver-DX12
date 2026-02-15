#ifndef DIFFUSE_COMMON_HLSLI
#define DIFFUSE_COMMON_HLSLI

#include "Common.hlsli"

StructuredBuffer<float3> g_PosPred : register(t0);
//StructuredBuffer<float3> g_PosOld : register(t1);
//StructuredBuffer<float3> g_VelIn : register(t2);
//StructuredBuffer<uint> g_SortedIndices : register(t3);
StructuredBuffer<float3> g_VelOut : register(t4);
StructuredBuffer<uint2> g_GridIndices : register(t5);

struct DiffuseParticle
{
    float4 PositionLife;
    float4 VelocityScale;
};
RWStructuredBuffer<DiffuseParticle> g_DiffuseParticles : register(u0);
RWStructuredBuffer<DiffuseParticle> g_DiffuseParticlesCompacted : register(u1);
RWStructuredBuffer<uint> g_Counters : register(u2);

struct DispatchIndirectCommand
{
    uint ThreadGroupCountX;
    uint ThreadGroupCountY;
    uint ThreadGroupCountZ;
};
RWStructuredBuffer<DispatchIndirectCommand> g_DispatchArgs : register(u3);

struct DrawIndirectCommand
{
    uint VertexCountPerInstance;
    uint InstanceCount;
    uint StartVertexLocation;
    uint StartInstanceLocation;
};
RWStructuredBuffer<DrawIndirectCommand> g_DrawArgs : register(u4);

cbuffer CB_SimParams : register(b0)
{
    SimParams g_SP;
};

struct DiffuseParams
{
    uint MaxDiffuseParticles;
    float DiffuseDeltaTime;
    float TrappedAirMin;
    float TrappedAirMax;

    float kTa;
    float WaveCrestMin;
    float WaveCrestMax;
    float kWc;

    float EnergyMin;
    float EnergyMax;
    float MaxLifeTime;
    float CellSizeScale;

    float BubbleScale;
    float BubbleScaleChangeSpeed;
    int SprayClassifyMaxNeighbours;
    int BubbleClassifyMinNeighbours;
    
    float BubbleBuoyancy;
    float FluidAccelMul;
    int GeneratePerFrame;
};

cbuffer CB_DiffuseParams : register(b1)
{
    DiffuseParams g_DP;
};

cbuffer Consts : register(b2)
{
    uint g_ArgType;
}; 

#endif