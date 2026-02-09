static const float2 g_ShadowMapSize = float2(2048.0, 2048.0);

float CalcShadowFactor(float4 shadowPosH, Texture2D shadowMap, SamplerComparisonState shadowSampler)
{
    float3 shadowPos = shadowPosH.xyz / shadowPosH.w;

    if (shadowPos.z > 1.0 || shadowPos.z < 0.0 ||
        shadowPos.x > 1.0 || shadowPos.x < 0.0 ||
        shadowPos.y > 1.0 || shadowPos.y < 0.0)
    {
        return 1.0;
    }

    float2 texelSize = 1.0 / g_ShadowMapSize;

    float percentLit = 0.0;
    
    float blurSpread = 1.5;

    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize * blurSpread;
            
            percentLit += shadowMap.SampleCmpLevelZero(
                shadowSampler,
                shadowPos.xy + offset,
                shadowPos.z - 0.00001
            );
        }
    }

    return percentLit / 9.0;
}