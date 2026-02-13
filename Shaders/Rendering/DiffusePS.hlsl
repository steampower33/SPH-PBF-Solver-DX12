struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};

float4 main(PSInput input) : SV_TARGET
{
    float2 center = float2(0.5, 0.5);
    float dist = distance(input.UV, center);
    
    if (dist > 0.5)
        discard;

    float alpha = smoothstep(0.5, 0.3, dist);
    
    return float4(input.Color.rgb, input.Color.a * alpha);
}