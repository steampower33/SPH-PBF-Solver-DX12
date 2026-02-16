Texture2D<float> g_DepthMap : register(t0);
Texture2D<float> g_Thickness : register(t2);
Texture2D<float4> g_SceneTex : register(t3);
Texture2D<float4> g_SceneDepth : register(t4);

TextureCube g_EnvMap : register(t5);
TextureCube g_SpecularMap : register(t6);
TextureCube g_DiffuseMap : register(t7);
Texture2D g_BrdfMap : register(t8);

SamplerState g_PointClamp : register(s0);
SamplerState g_LinearClamp : register(s1);

cbuffer cbParams : register(b0)
{
    matrix g_ViewInverse;
    matrix g_ProjInverse;
    float3 g_CamPos;
    float pad0;
    float2 g_InvScreenSize;
    float pad1[2];
    float3 g_LightDir;
    float g_LightIntensity;
};

static const float g_Roughness = 0.02f;
static const float g_Metallic = 0.0f;
static const float3 g_F0 = float3(0.02f, 0.02f, 0.02f);
static const float3 g_AbsorptionCoef = float3(1.5f, 0.8f, 0.1f);
static const float g_DistortionScale = 0.05f;

#define PI 3.14159265359

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

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

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 GetViewPos(float2 uv)
{
    float z_view = g_DepthMap.Sample(g_PointClamp, uv).r;
    float4 clipPos = float4(uv.xy * 2.0 - 1.0, 1.0, 1.0);
    float4 viewPosRaw = mul(clipPos, g_ProjInverse);
    viewPosRaw /= viewPosRaw.w;
    return viewPosRaw.xyz * (z_view / viewPosRaw.z);
}

float GetSceneLinearDepth(float2 uv)
{
    float hwDepth = g_SceneDepth.Sample(g_PointClamp, uv).r;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, hwDepth, 1.0);
    float4 viewPos = mul(clipPos, g_ProjInverse);
    
    return -viewPos.z / viewPos.w;
}

float3 ReconstructViewNormal(float2 uv, float3 centerPos)
{
    float3 posRight = GetViewPos(uv + float2(g_InvScreenSize.x, 0.0));
    float3 posUp = GetViewPos(uv + float2(0.0, g_InvScreenSize.y));
    
    float3 ddx = posRight - centerPos;
    float3 ddy = posUp - centerPos;
    
    float3 N = cross(ddy, ddx);
    N.xy *= 2.0; // Normal Strength
    return normalize(N);
}

float2 CalculateRefractionUV(float2 baseUV, float3 normalView, float thickness, float myDepthDist)
{
    float2 offset = normalView.xy * g_DistortionScale * saturate(thickness);
    float2 refractUV = baseUV + offset;
    
    float bgDepthDist = GetSceneLinearDepth(refractUV);
    if (bgDepthDist < myDepthDist)
        return baseUV;
    return refractUV;
}

float3 ComputeSpecular(float3 N, float3 V, float3 L, float3 H)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float D = DistributionGGX(N, H, g_Roughness);
    float G = GeometrySmith(N, V, L, g_Roughness);
    float3 F = FresnelSchlick(HdotV, g_F0);

    float3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.001;
    return numerator / denominator;
}

float4 main(VSOutput input) : SV_Target
{
    float depth = g_DepthMap.Sample(g_PointClamp, input.UV).r;
    if (depth > 1000.0)
    {
        return g_SceneTex.Sample(g_PointClamp, input.UV);
    }

    float3 posView = GetViewPos(input.UV);
    float3 N_view = ReconstructViewNormal(input.UV, posView);
    float3 N_world = normalize(mul(N_view, (float3x3) g_ViewInverse));
    
    float4 posWorld4 = mul(float4(posView, 1.0), g_ViewInverse);
    float3 posWorld = posWorld4.xyz / posWorld4.w;

    float3 L = normalize(-g_LightDir);
    float3 V = normalize(g_CamPos - posWorld);
    float3 H = normalize(L + V);
    
    float myDist = -posView.z;
    float2 finalRefractUV = CalculateRefractionUV(input.UV, N_view, g_Thickness.Sample(g_LinearClamp, input.UV).r, myDist);
    float3 refractedColor = g_SceneTex.Sample(g_LinearClamp, finalRefractUV).rgb;

    float thickness = g_Thickness.Sample(g_LinearClamp, input.UV).r;
    float3 transmittance = exp(-g_AbsorptionCoef * thickness);
    
    float diffuseLight = max(dot(N_world, L) * 0.5 + 0.5, g_LightIntensity);
    float3 finalTransmittance = transmittance * diffuseLight;

    float3 g_ScatterColor = float3(0.0, 0.05, 0.1);
    float3 waterBodyColor = lerp(g_ScatterColor, refractedColor, transmittance);

    float3 directSpecular = ComputeSpecular(N_world, V, L, H) * g_LightIntensity;

    float NdotL = max(dot(N_world, L), 0.0);
    directSpecular *= NdotL;
    
    float3 R = reflect(-V, N_world);
    float3 reflectionColor = g_SpecularMap.SampleLevel(g_LinearClamp, R, 0).rgb;
    
    reflectionColor *= 0.8f;
    
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), g_F0);

    float3 finalColor = lerp(waterBodyColor, reflectionColor, F);
    finalColor += directSpecular;

    return float4(finalColor, 1.0);
}