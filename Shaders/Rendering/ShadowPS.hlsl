struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD;
};

void main(VSOutput input)
{
    float2 center = float2(0.5, 0.5);
    float dist = length(input.UV - center);

    if (dist > 0.5f)
    {
        discard;
    }
}