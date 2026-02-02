struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 viewPos : TEXCOORD1;
    float SortRatio : TEXCOORD2;
};

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

    // Simple directional lighting (assuming light comes from the camera)
    float diffuse = max(dot(normal, float3(0, 0, 1)), 0.0f);
    
    // [NEW] Debug Color Mapping
    // Low Index (0) -> Red
    // High Index (1) -> Blue
    float3 debugColor = lerp(float3(1, 0, 0), float3(0, 0, 1), input.SortRatio);

    // Apply lighting to the debug color
    float3 finalColor = debugColor * diffuse;

    return float4(finalColor, 1.0f);
}