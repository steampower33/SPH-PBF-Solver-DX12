struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 viewPos : TEXCOORD1;
    float Density : TEXCOORD2;
};

float3 GetHeatmapColor(float value)
{
    float3 colorLow = float3(0.0f, 0.0f, 1.0f);
    float3 colorMid = float3(0.0f, 1.0f, 0.0f);
    float3 colorHigh = float3(1.0f, 0.0f, 0.0f);

    // if value : 0~1 
    // 0.0 ~ 0.5 : Low -> Mid
    // 0.5 ~ 1.0 : Mid -> High

    float3 finalColor;

    if (value < 0.5f)
    {
        // 0~0.5 -> 0 ~ 1
        float t = value * 2.0f;
        finalColor = lerp(colorLow, colorMid, t);
    }
    else
    {
        // 0.5~1.0 -> 0~1 
        float t = (value - 0.5f) * 2.0f;
        finalColor = lerp(colorMid, colorHigh, t);
    }

    return finalColor;
}

float4 main(VSOutput input) : SV_Target
{
    // Convert UV [0, 1] to signed normalized coordinates [-1, 1]
    float2 centerOffset = input.uv * 2.0f - 1.0f;

    // Circular Culling (Imposter Logic)
    // Calculate squared distance from the center
    float distSq = dot(centerOffset, centerOffset);

    // Discard pixels outside the circle radius
    if (distSq > 1.0f)
    {
        discard;
    }

    // Simulate sphere curvature (Simple Shading)
    // Reconstruct Z component to get the surface normal
    float z = sqrt(1.0f - distSq);
    float3 normal = float3(centerOffset, z); // Normal in View Space

    float diffuse = max(dot(normal, float3(0, 0, 1)), 0.1f);

    float rho0 = 1000.0;
    
    float t = saturate(input.Density / (rho0 * 2.0f));

    float3 finalColor = GetHeatmapColor(t);

    return float4(finalColor * diffuse, 1.0f);
}