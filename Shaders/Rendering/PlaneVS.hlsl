
cbuffer Params : register(b0)
{
    matrix g_View;
    matrix g_Proj;
    matrix g_ShadowTransform;
    float3 g_LightPos;
    float g_TileScale;
    float3 g_LightDir;
    float g_TileCount;
    float g_SpotAngleCos;
    float3 g_pad;
};

struct VSInput
{
    float3 Pos : POSITION;
    float2 UV : TEXCOORD;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD;
    float3 PosW : POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    
    float4 posWorldScale = float4(input.Pos * g_TileScale, 1.0);
    output.Pos = mul(mul(posWorldScale, g_View), g_Proj);
    output.UV = input.UV;
    output.PosW = posWorldScale.xyz;
    return output;
}