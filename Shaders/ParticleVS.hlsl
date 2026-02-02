cbuffer Params : register(b0)
{
    matrix g_View;
    matrix g_Proj;
    float g_Radius;
};

struct Particle
{
    float3 Position;
    float Density;
    float3 Velocity;
    float Pressure;
};

StructuredBuffer<Particle> g_Particles : register(t0);

struct VSInput
{
    float3 Pos : POSITION;
    float2 UV : TEXCOORD;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 ViewPos : TEXCOORD1;
};

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput output;

    float3 particleWorldPos = g_Particles[instanceID].Position;

    float4 viewPos4 = mul(float4(particleWorldPos, 1.0f), g_View);
    float3 centerViewPos = viewPos4.xyz;

    float2 offset = input.Pos.xy * (g_Radius * 2.0f);

    float3 finalViewPos = centerViewPos;
    finalViewPos.xy += offset;

    output.PosH = mul(float4(finalViewPos, 1.0f), g_Proj);
    output.UV = input.UV;
    output.ViewPos = finalViewPos;

    return output;
}