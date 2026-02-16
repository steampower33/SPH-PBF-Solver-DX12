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
static const float g_Roughness = 0.05f;
static const float3 g_F0 = float3(0.02f, 0.02f, 0.02f);
static const float3 g_AbsorptionCoef = float3(1.5f, 0.6f, 0.1f);
static const float g_DistortionScale = 0.05f;
static const float3 g_ScatterColor = float3(0.0f, 0.2f, 0.3f);

#define PI 3.14159265359

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

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

// ===============================================================================================
// Helper Functions (Math & Depth)
// ===============================================================================================

// 뷰 공간 위치 복원
float3 GetViewPos(float2 uv)
{
    float z_view = g_DepthMap.Sample(g_PointClamp, uv).r;
    float4 clipPos = float4(uv.xy * 2.0 - 1.0, 1.0, 1.0);
    float4 viewPosRaw = mul(clipPos, g_ProjInverse);
    viewPosRaw /= viewPosRaw.w;
    return viewPosRaw.xyz * (z_view / viewPosRaw.z);
}

// 배경의 선형 깊이(양수 거리) 가져오기
float GetSceneLinearDepth(float2 uv)
{
    float hwDepth = g_SceneDepth.Sample(g_PointClamp, uv).r;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, hwDepth, 1.0);
    float4 viewPos = mul(clipPos, g_ProjInverse);
    
    // RH 좌표계에서는 z가 음수이므로 -를 붙여 양수 거리로 반환
    return -viewPos.z / viewPos.w;
}

// 스크린 스페이스 노멀 계산
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

// Procedural Skybox
float3 GetSkyColor(float3 dir)
{
    float t = 0.5 * (dir.y + 1.0);
    return lerp(float3(1.0, 1.0, 1.0), float3(0.1, 0.4, 0.8), t);
}

// ===============================================================================================
// 3. Lighting & Physics Logic
// ===============================================================================================

// 굴절 UV 계산 및 깊이 판정
float2 CalculateRefractionUV(float2 baseUV, float3 normalView, float thickness, float myDepthDist)
{
    // 굴절 오프셋 계산
    float2 offset = normalView.xy * g_DistortionScale * saturate(thickness);
    float2 refractUV = baseUV + offset;
    
    // 깊이 테스트
    // RH 좌표계: 내 깊이(myDepthDist)는 양수 거리
    float bgDepthDist = GetSceneLinearDepth(refractUV);

    // 배경(bg)이 나(my)보다 값이 작다 = 더 가깝다(앞에 있다)
    if (bgDepthDist < myDepthDist)
    {
        return baseUV; // 굴절 취소
    }
    
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
    // [Early Exit] 배경이면 바로 리턴
    float depth = g_DepthMap.Sample(g_PointClamp, input.UV).r;
    if (depth > 1000.0) // Far Plane Check
    {
        return g_SceneTex.Sample(g_PointClamp, input.UV);
    }

    // --------------------------------------------------------
    // A. 지오메트리 복원 (Position & Normal)
    // --------------------------------------------------------
    float3 posView = GetViewPos(input.UV);
    float3 N_view = ReconstructViewNormal(input.UV, posView);
    float3 N_world = normalize(mul(N_view, (float3x3) g_ViewInverse));
    
    float4 posWorld4 = mul(float4(posView, 1.0), g_ViewInverse);
    float3 posWorld = posWorld4.xyz / posWorld4.w;

    // --------------------------------------------------------
    // B. 벡터 준비 (Light, View, Half)
    // --------------------------------------------------------
    float3 L = normalize(-normalize(float3(0, 0, 0) - g_LightPos)); // Directional Light Dir
    float3 V = normalize(g_CamPos - posWorld);
    float3 H = normalize(L + V);

    // --------------------------------------------------------
    // C. 물리 연산 (굴절, 흡수, 반사, 라이팅)
    // --------------------------------------------------------
    
    // 1. 굴절 (Refraction)
    // posView.z는 RH에서 음수이므로 -를 붙여 양수 거리로 변환해서 넘김
    float myDist = -posView.z;
    float2 finalRefractUV = CalculateRefractionUV(input.UV, N_view, g_Thickness.Sample(g_LinearClamp, input.UV).r, myDist);
    float3 refractedColor = g_SceneTex.Sample(g_LinearClamp, finalRefractUV).rgb;

    // 2. 흡수 (Absorption / Transmittance)
    float thickness = g_Thickness.Sample(g_LinearClamp, input.UV).r;
    float3 transmittance = exp(-g_AbsorptionCoef * thickness);
    
    // 라이팅에 따른 투과율 보정 (그림자 등)
    float diffuseLight = max(dot(N_world, L) * 0.5 + 0.5, g_ShadowIntensity);
    float3 finalTransmittance = transmittance * diffuseLight;

    // 굴절된 색상에 산란(Scatter) 색 섞기
    float3 waterBodyColor = lerp(g_ScatterColor, refractedColor, transmittance);

    // 3. 스펙큘러 (Specular)
    float3 specular = ComputeSpecular(N_world, V, L, H) * g_ShadowIntensity;

    // 4. 반사 (Reflection / Skybox)
    float3 R = reflect(-V, N_world); // ViewDir은 카메라->픽셀 방향이어야 reflect 함수랑 맞음 (여기선 V가 픽셀->카메라라 -V)
    float3 reflectionColor = GetSkyColor(R);
    
    // 프레넬 계산 (반사/굴절 비율)
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), g_F0);

    // --------------------------------------------------------
    // D. 최종 합성 (Compositing)
    // --------------------------------------------------------
    float3 finalColor = lerp(waterBodyColor, reflectionColor, F);
    finalColor += specular;

    return float4(finalColor, 1.0);
}