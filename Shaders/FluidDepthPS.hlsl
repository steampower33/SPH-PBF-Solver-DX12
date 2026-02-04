cbuffer Params : register(b0)
{
    matrix g_View;
    matrix g_Proj;
    float g_VisualRadius;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 ViewPos : TEXCOORD1;
    float Density : TEXCOORD2;
};

float main(VSOutput input) : SV_Target
{
    float2 centerOffset = input.UV * 2.0 - 1.0;
    float distSq = dot(centerOffset, centerOffset);
    if (distSq > 1.0)
        discard;

    float z = sqrt(1.0 - distSq);

    float linearDepth = -input.ViewPos.z - (z * g_VisualRadius);

    return linearDepth;
}