Texture2D<float> g_DepthMap : register(t0);
Texture2D<float> g_Thickness : register(t2);

SamplerState g_PointClamp : register(s0);
SamplerState g_LinearClamp : register(s1);

cbuffer cbParams : register(b0)
{
    float2 g_InvScreenSize;
    float2 g_pad0;
    float3 g_LightDir;
    float g_pad1;
};

#define PI 3.14159265359

// ===============================================================================================
// PBR Helper Functions (Cook-Torrance BRDF)
// ===============================================================================================

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


struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

float4 main(VSOutput input) : SV_Target
{
    // Depth Check
    float depth = g_DepthMap.Sample(g_PointClamp, input.UV);
    if (depth > 10000.0)
        discard;

    // Normal Reconstruction
    float2 uv = input.UV;
    float2 texelSize = g_InvScreenSize;
    float depthRight = g_DepthMap.Sample(g_PointClamp, uv + float2(texelSize.x, 0));
    float depthUp = g_DepthMap.Sample(g_PointClamp, uv + float2(0, texelSize.y));
    if (depthRight > 10000.0)
        depthRight = depth;
    if (depthUp > 10000.0)
        depthUp = depth;
    
    float3 N = normalize(float3(depth - depthRight, depth - depthUp, 2.0 * texelSize.x));

    float thickness = g_Thickness.Sample(g_LinearClamp, input.UV).r;
    
    float3 fakeBackground = lerp(float3(0.1, 0.1, 0.15), float3(0.6, 0.8, 0.9), input.UV.y);
    
    float3 backgroundColor = fakeBackground;

    float3 absorptionCoeff = float3(0.8, 0.2, 0.05);
    float3 transmittance = exp(-absorptionCoeff * thickness * 3.0);

    float3 L = normalize(g_LightDir);
    float3 V = float3(0, 0, 1);
    float3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 128.0) * 2.0;

    float3 finalColor = (backgroundColor * transmittance) + spec;

    return float4(finalColor, 1.0);
}