
cbuffer DiffuseConstants : register(b0)
{
    float4x4 ViewProj;
    float4x4 InvView;
    float4x4 InvProj;
    float Scale;
    float BaseAlpha;
    float2 InvScreenSize;
    float Turbidity;
};

Texture2D<float> g_FluidDepth : register(t0);
struct DiffuseParticle
{
    float4 PositionLife;
    float4 VelocityScale;
};
StructuredBuffer<DiffuseParticle> g_Particles : register(t3);

// Quad Vertices (Triangle List Order)
static const float2 kQuadVerts[6] =
{
    float2(-1, 1), float2(1, 1), float2(-1, -1),
    float2(-1, -1), float2(1, 1), float2(1, -1)
};

float Remap01(float val, float minVal, float maxVal)
{
    return saturate((val - minVal) / (maxVal - minVal));
}

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
    float LinearDepth : TEXCOORD1;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
    float LinearDepth : TEXCOORD1;
};
