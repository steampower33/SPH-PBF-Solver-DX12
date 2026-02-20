TextureCube g_EnvMap : register(t0);
TextureCube g_SpecularMap : register(t1);
TextureCube g_DiffuseMap : register(t2);
Texture2D g_BrdfMap : register(t3);

SamplerState g_LinearSampler : register(s0);

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float3 TexCoord : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET
{
    return g_EnvMap.SampleLevel(g_LinearSampler, input.TexCoord, 0);
}