#include "RenderCommon.hlsli"

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
    float g_ShadowIntensity;
    float2 g_pad;
};

Texture2D g_ShadowMap : register(t0);
SamplerComparisonState g_ShadowSampler : register(s0);

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD;
    float3 PosW : POSITION;
};

float4 main(VSOutput input) : SV_TARGET
{
    float2 scaledUV = input.UV * g_TileCount;

    float checker = fmod(floor(scaledUV.x) + floor(scaledUV.y), 2.0);

    float3 baseColor;
    if (input.UV.x < 0.5)
    {
        if (input.UV.y < 0.5)
            baseColor = float3(0.5, 0.8, 0.5); // LD - Green
        else
            baseColor = float3(1.0, 0.6, 0.6); // LU - Pink
    }
    else
    {
        if (input.UV.y < 0.5)
            baseColor = float3(0.9, 0.8, 0.4); // RD - Yellow
        else
            baseColor = float3(0.6, 0.4, 0.8); // RU - Purple
    }
    
    float4 shadowPosH = mul(float4(input.PosW, 1.0f), g_ShadowTransform);

    float shadowFactor = CalcShadowFactor(shadowPosH, g_ShadowMap, g_ShadowSampler);

    float3 floorColor = baseColor * (0.8 + 0.2 * checker);
    
    float finalLight = max(shadowFactor, g_ShadowIntensity);

    float3 finalColor = floorColor * finalLight;
    finalColor += floorColor * 0.1;
    return float4(finalColor, 1.0);
}