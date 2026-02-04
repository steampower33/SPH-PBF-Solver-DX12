float3 GetSmoothHeatmap(float t)
{
    t = saturate(t);
    t = t * t * (3.0 - 2.0 * t);

    float3 c0 = float3(0.05, 0.05, 0.20); // 0.0: Deep Navy (밀도 부족)
    float3 c1 = float3(0.00, 0.20, 0.80); // 0.25: Blue
    float3 c2 = float3(0.00, 0.90, 0.90); // 0.50: Cyan (Rest Density 안정권)
    float3 c3 = float3(1.00, 0.90, 0.00); // 0.75: Yellow (압축됨)
    float3 c4 = float3(1.00, 0.10, 0.00); // 1.00: Red (폭발 직전)

    t *= 4.0; // 4개의 구간으로 확장
    float i = floor(t);
    float f = frac(t);

    if (i < 1.0)
        return lerp(c0, c1, f);
    if (i < 2.0)
        return lerp(c1, c2, f);
    if (i < 3.0)
        return lerp(c2, c3, f);
    return lerp(c3, c4, f);
}

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 viewPos : TEXCOORD1;
    float Density : TEXCOORD2;
};

static const float3 LightDir = normalize(float3(0.5, 1.0, 1.0));
static const float3 ViewDir = float3(0, 0, 1);

float4 main(VSOutput input) : SV_Target
{
    float2 centerOffset = input.uv * 2.0 - 1.0;
    float distSq = dot(centerOffset, centerOffset);
    if (distSq > 1.0)
        discard;

    float z = sqrt(1.0 - distSq);
    float3 normal = float3(centerOffset, z);

    float NdotL = dot(normal, LightDir);
    float diffuse = NdotL * 0.5 + 0.5;

    float3 halfVector = normalize(LightDir - ViewDir);
    float NdotH = max(dot(normal, halfVector), 0.0);
    float specular = pow(NdotH, 64.0);

    float fresnel = pow(1.0 - max(dot(normal, float3(0, 0, 1)), 0.0), 3.0);
    fresnel *= 0.5;

    float t = input.Density / 2000.0f;

    float3 baseColor = GetSmoothHeatmap(t);

    float3 finalColor = baseColor * diffuse + float3(1, 1, 1) * specular + baseColor * fresnel;

    finalColor = pow(finalColor, 1.0 / 2.2);

    return float4(finalColor, 1.0);
}