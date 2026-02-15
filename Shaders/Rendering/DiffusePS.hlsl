#include "RenderDiffuseCommon.hlsli"

float4 main(PSInput input) : SV_TARGET
{
    float2 uvCenter = input.UV - 0.5;
    float distSq = dot(uvCenter, uvCenter);
    if (distSq > 0.25)
        discard;

    float shapeAlpha = smoothstep(0.5, 0.3, length(uvCenter));

    int3 screenPos = int3(input.Pos.xy, 0);
    float waterDepth = g_FluidDepth.Load(screenPos).r;
    float myDepth = input.LinearDepth;

    float depthFade = 1.0;
    float3 tintColor = float3(1, 1, 1);

    if (waterDepth < 10000.0 && myDepth > waterDepth)
    {
        float depthDiff = myDepth - waterDepth;

        depthFade = exp(-depthDiff * Turbidity);

        if (depthFade < 0.05)
            discard;
            
        tintColor = float3(0.6, 0.8, 0.9);
    }

    float finalAlpha = input.Color.a * shapeAlpha * depthFade;
    
    return float4(input.Color.rgb * tintColor, finalAlpha);
}
