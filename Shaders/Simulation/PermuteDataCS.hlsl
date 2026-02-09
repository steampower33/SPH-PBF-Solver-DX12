StructuredBuffer<float3> g_PosPred_In : register(t0);
StructuredBuffer<float3> g_PosOld_In : register(t1);
StructuredBuffer<float3> g_Vel_In : register(t2);
StructuredBuffer<uint> g_SortedIndices : register(t3);

RWStructuredBuffer<float3> g_TempPosPred : register(u0);
RWStructuredBuffer<float3> g_TempPosOld : register(u1);
RWStructuredBuffer<float3> g_TempVel : register(u2);

cbuffer SimParams : register(b0)
{
    float g_DeltaTime;
    uint g_NumParticles;
    float g_CellSize;
    uint g_GridDim;
    
    float g_Mass;
    float g_RestDensity;
    float g_Viscosity;
    float g_GravityY;

    float2 g_BoxX;
    float2 g_BoxY;

    float2 g_BoxZ;
    float g_epsilon;
    float g_k;

    float g_n;
    float g_dqScale;
    float g_vorticityEpsilon;
    float g_externalAccel;
};

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    uint oldID = g_SortedIndices[id];

    g_TempPosPred[id] = g_PosPred_In[oldID];
    g_TempPosOld[id] = g_PosOld_In[oldID];
    g_TempVel[id] = g_Vel_In[oldID];
}