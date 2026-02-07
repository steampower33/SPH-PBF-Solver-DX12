cbuffer Globals : register(b0)
{
    matrix g_View;
    matrix g_Proj;
};

cbuffer Params : register(b1)
{
    float g_Scale;
    float g_TileCount;
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
};

VSOutput main(VSInput input)
{
    VSOutput output;
    
    output.Pos = mul(mul(float4(input.Pos * g_Scale, 1.0), g_View), g_Proj);
    output.UV = input.UV;
    return output;

}