cbuffer Params : register(b0)
{
    matrix g_View;
    matrix g_Proj;
    float g_VisualRadius;
    float g_ThicknessCoeff;
};

StructuredBuffer<float3> g_Pos : register(t0);
StructuredBuffer<float> g_Density : register(t1);

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
    float Density : TEXCOORD2;
};

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput output;

    float3 particleWorldPos = g_Pos[instanceID];

    float4 viewPos4 = mul(float4(particleWorldPos, 1.0), g_View);
    float3 centerViewPos = viewPos4.xyz;

    float2 offset = input.Pos.xy * (g_VisualRadius * 2.0);

    float3 finalViewPos = centerViewPos;
    finalViewPos.xy += offset;

    output.PosH = mul(float4(finalViewPos, 1.0), g_Proj);
    output.UV = input.UV;
    output.ViewPos = finalViewPos;
    output.Density = g_Density[instanceID];

    return output;
}