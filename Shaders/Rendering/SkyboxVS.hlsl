cbuffer Params : register(b0)
{
    matrix g_View;
    matrix g_Proj;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float3 TexCoord : TEXCOORD0;
};

VSOutput main(float3 pos : POSITION)
{
    VSOutput output;

    output.TexCoord = pos;

    float4 posW = mul(float4(pos, 0.0), g_View);
    float4 posH = mul(posW, g_Proj);
    
    output.Pos = posH.xyww;
    
    return output;
}