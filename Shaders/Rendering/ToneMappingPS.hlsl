cbuffer Params : register(b0)
{
    float g_Gamma;
    float g_Exposure;
    float2 g_Padding;
};

Texture2D g_HDRTexture : register(t0);
SamplerState g_LinearSampler : register(s0);

float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

float4 main(VSOutput input) : SV_Target
{
    float3 hdrColor = g_HDRTexture.Sample(g_LinearSampler, input.UV).rgb;

    hdrColor *= g_Exposure;

    float3 ldrColor = ACESFilm(hdrColor);

    ldrColor = pow(ldrColor, 1.0 / g_Gamma);

    return float4(ldrColor, 1.0);
}