
struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VSOutput main(uint vI : SV_VERTEXID)
{
    VSOutput output;
    
    float2 texcoord = float2((vI << 1) & 2, vI & 2);
    output.Pos = float4(texcoord.x * 2.0 - 1.0, -texcoord.y * 2.0 + 1.0, 0, 1);
    output.UV = texcoord;
    return output;
}