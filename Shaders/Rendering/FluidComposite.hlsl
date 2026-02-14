#include "RenderCommon.hlsli"

Texture2D<float> g_DepthMap : register(t0);
Texture2D<float> g_Thickness : register(t2);
Texture2D<float4> g_SceneTex : register(t3);
Texture2D<float4> g_SceneDepth : register(t4);

SamplerState g_PointClamp : register(s0);
SamplerState g_LinearClamp : register(s1);

cbuffer cbParams : register(b0)
{
    matrix g_ViewInverse;
    matrix g_ProjInverse;
    matrix g_ShadowTransform;
    float3 g_CamPos;
    float g_ShadowIntensity;
    float2 g_InvScreenSize;
};

static const float3 g_LightPos = float3(0.0f, 10.0f, 10.0f);

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

// ===============================================================================================
// PBR Helper Functions (Cook-Torrance BRDF)
// ===============================================================================================

#define PI 3.14159265359

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / max(denom, 0.0000001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / max(denom, 0.0000001);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 ViewPos(float2 uv)
{
    float z_view = g_DepthMap.Sample(g_PointClamp, uv).r;

    float4 clipPos = float4(uv.xy * 2.0 - 1.0, 1.0, 1.0);
    float4 viewPosRaw = mul(clipPos, g_ProjInverse);
    viewPosRaw /= viewPosRaw.w;
    float3 viewPos = viewPosRaw.xyz * (z_view / viewPosRaw.z);

    return viewPos;
}

float3 GetProceduralSky(float3 dir)
{
    float t = 0.5 * (dir.y + 1.0);
    
    float3 topColor = float3(0.1, 0.4, 0.8);
    float3 botColor = float3(1.0, 1.0, 1.0);
    
    return lerp(botColor, topColor, dir.y * 0.5 + 0.5);
}

float4 main(VSOutput input) : SV_Target
{
    float2 uv = input.UV;
    
    float depth = g_DepthMap.Sample(g_PointClamp, uv).r;
    if (depth > 1000.0)
    {
        return g_SceneTex.Sample(g_PointClamp, uv);
    }

    float2 texelSize = g_InvScreenSize;
    float3 posCenter = ViewPos(uv);
    float3 posRight = ViewPos(uv + float2(texelSize.x, 0.0));
    float3 posUp = ViewPos(uv + float2(0.0, texelSize.y));

    float3 ddx = posRight - posCenter;
    float3 ddy = posUp - posCenter;

    float3 N_view = cross(ddy, ddx);
    float normalStrength = 2.0;
    N_view.xy *= normalStrength;
    N_view = normalize(N_view);
    
    float3 N_world = mul(N_view, (float3x3) g_ViewInverse);
    N_world = normalize(N_world);

    float4 worldPos4 = mul(float4(posCenter, 1.0), g_ViewInverse);
    float3 worldPos = worldPos4.xyz / worldPos4.w;

    float3 LightDirection = normalize(float3(0.0, 0.0, 0.0) - g_LightPos);

    float3 L = normalize(-LightDirection);
    
    float3 V = normalize(g_CamPos - worldPos);
    float3 H = normalize(L + V);
    float3 N = N_world;
    
    float roughness = 0.15;
    float F0 = 0.04;
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.001;
    float3 specular = numerator / denominator;
    float spotEffect = 1.0;
    float3 lightColor = float3(1.0, 1.0, 1.0);

    float thickness = g_Thickness.Sample(g_LinearClamp, input.UV).r;

    float3 absorptionCoef = float3(4.0, 1.5, 0.8);
    float3 transmittance = exp(-absorptionCoef * thickness);

    float diffuseWrap = (dot(N, L) * 0.5 + 0.5);
    
    float lightIntensity = max(diffuseWrap, g_ShadowIntensity);
    
    float3 finalTransmittance = transmittance * lightIntensity;

    float distortionStrength = 0.2;
    float2 refractionUV = input.UV + N_view.xy * distortionStrength * saturate(thickness);
    float3 sceneColor = g_SceneTex.Sample(g_LinearClamp, refractionUV).rgb;

    float3 refractedColor = sceneColor * finalTransmittance;

    float3 viewDir = normalize(worldPos - g_CamPos);
    float3 R = reflect(viewDir, N);
    float3 reflectionColor = GetProceduralSky(R);

    float3 finalSpecular = specular * lightColor;

    float3 finalColor = lerp(refractedColor, reflectionColor, F) + finalSpecular;

    return float4(finalColor, 1.0);
}