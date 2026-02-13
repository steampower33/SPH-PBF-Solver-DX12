cbuffer DiffuseConstants : register(b0)
{
    float4x4 ViewProj;
    float4x4 InvView;
    float4x4 InvProj;
    float Scale;
    float BaseAlpha;
};

struct DiffuseParticle
{
    float4 PositionLife;
    float4 VelocityScale;
};
StructuredBuffer<DiffuseParticle> g_Particles : register(t0);

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};

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

static const float NaN = 0.0f / 0.0f;

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    
    DiffuseParticle p = g_Particles[instanceID];
    
    if (p.PositionLife.w <= 0.0f)
    {
        output.Pos = float4(NaN, NaN, NaN, NaN);
        return output;
    }

    float3 centerWorld = p.PositionLife.xyz;
    float life = p.PositionLife.w;
    const float remainingLifetimeDissolveStart = 3.0;
    
    float dissolveScaleT = saturate(life / remainingLifetimeDissolveStart);
    float speed = length(p.VelocityScale.xyz);
    float velScale = lerp(0.6, 1, Remap01(speed, 1, 3));
    float vertScale = Scale * 2 * dissolveScaleT * p.VelocityScale.w * velScale;
    //float vertScale = 0.01;
    
    float3 right = normalize(InvView[0].xyz);
    float3 up = normalize(InvView[1].xyz);

    float2 offset = kQuadVerts[vertexID];
    float3 posWorld = centerWorld + (right * offset.x + up * offset.y) * vertScale;

    output.Pos = mul(float4(posWorld, 1.0), ViewProj);
    output.UV = offset * 0.5 + 0.5;
    
    float fadeAlpha = saturate(life / 0.5);
    
    output.Color = float4(0.9, 0.9, 0.9, BaseAlpha * fadeAlpha);

    //float4 color = 1.0;
    //if (p.VelocityScale.w <= 0.1)
    //    color = float4(1.0, 0.0, 0.0, 1.0);
    //else if (p.VelocityScale.w <= 1.1)
    //    color = float4(0.0, 1.0, 0.0, 1.0);
    //else if (p.VelocityScale.w <= 2.1)
    //    color = float4(0.0, 0.0, 1.0, 1.0);
    
    //output.Color = color;
    
    return output;
}