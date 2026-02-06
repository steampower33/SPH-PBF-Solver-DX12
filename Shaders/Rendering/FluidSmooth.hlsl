Texture2D<float> g_InputMap : register(t0);
SamplerState g_PointClamp : register(s0);

cbuffer cbBlurParams : register(b0)
{
    float2 g_InvScreenSize; // (1/Width, 1/Height)
    float2 g_BlurDir; // (1, 0) or (0, 1)
    float g_Radius;
    float g_SigmaSpatial;
    float g_SigmaRange;
    float g_pad;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

float main(VSOutput input) : SV_Target
{
    float centerDepth = g_InputMap.Sample(g_PointClamp, input.UV);

    // Skip background (Assuming 1e9 is clear value)
    if (centerDepth > 10000.0)
        return centerDepth;

    float sum = 0.0;
    float wsum = 0.0;

    // Pre-computed Gaussian weights could be used here for optimization
    for (int r = -g_Radius; r <= g_Radius; ++r)
    {
        float2 uv = input.UV + g_BlurDir * (r * g_InvScreenSize);
        float sampleDepth = g_InputMap.Sample(g_PointClamp, uv);

        if (sampleDepth > 10000.0)
            continue;

        // 1. Spatial Weight (Gaussian)
        float spatialDist = (float) r;
        float wSpatial = exp(-(spatialDist * spatialDist) / (2.0 * g_SigmaSpatial * g_SigmaSpatial));

        // 2. Range Weight (Edge Preservation)
        // Check difference between center depth and neighbor depth
        float rangeDist = centerDepth - sampleDepth;
        float wRange = exp(-(rangeDist * rangeDist) / (2.0 * g_SigmaRange * g_SigmaRange));

        float w = wSpatial * wRange;

        sum += sampleDepth * w;
        wsum += w;
    }

    if (wsum > 0.0)
        return sum / wsum;
    
    return centerDepth;
}