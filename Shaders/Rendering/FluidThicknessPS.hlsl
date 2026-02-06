cbuffer Params : register(b0)
{
    matrix g_View;
    matrix g_Proj;
    float g_VisualRadius;
    float g_ThicknessCoeff;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

float4 main(VSOutput input) : SV_Target
{
    float2 centerOffset = input.UV * 2.0 - 1.0;
    float distSq = dot(centerOffset, centerOffset);
    if (distSq > 1.0)
        discard;

    return float4(g_ThicknessCoeff, 0.0, 0.0, 1.0);
}