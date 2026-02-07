cbuffer Params : register(b1)
{
    float g_Scale;
    float g_TileCount;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD;
};

float4 main(VSOutput input) : SV_TARGET
{
    float2 scaledUV = input.UV * g_TileCount;

    float checker = fmod(floor(scaledUV.x) + floor(scaledUV.y), 2.0);

    float3 baseColor;
    if (input.UV.x < 0.5)
    {
        if (input.UV.y < 0.5)
            baseColor = float3(0.5, 0.8, 0.5); // LD - Green
        else
            baseColor = float3(1.0, 0.6, 0.6); // LU - Pink
    }
    else
    {
        if (input.UV.y < 0.5)
            baseColor = float3(0.9, 0.8, 0.4); // RD - Yellow
        else
            baseColor = float3(0.6, 0.4, 0.8); // RU - Purple
    }
    
    float3 finalColor = baseColor * (0.8 + 0.2 * checker);

    return float4(finalColor, 1.0);
}