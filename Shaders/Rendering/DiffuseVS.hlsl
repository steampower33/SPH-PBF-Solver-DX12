#include "RenderDiffuseCommon.hlsli"

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    
    DiffuseParticle p = g_Particles[instanceID];

    float3 centerWorld = p.PositionLife.xyz;
    float life = p.PositionLife.w;
    float3 velocity = p.VelocityScale.xyz;
    float speed = length(velocity);
    
    float stretchFactor = saturate(speed * 0.2);
    float scaleX = 1.0 / (1.0 + stretchFactor * 0.5);
    float scaleY = 1.0 + stretchFactor;

    float baseSize = Scale * p.VelocityScale.w;
    
    float fade = smoothstep(0.0, 0.2, life) * smoothstep(3.0, 2.5, life);
    float finalSize = baseSize * fade;
    
    float3 camRight = normalize(InvView[0].xyz);
    float3 camUp = normalize(InvView[1].xyz);
    
    float3 particleRight = camRight;
    float3 particleUp = camUp;

    float2 offset = kQuadVerts[vertexID % 6];
    
    float3 vertexPos = centerWorld
                     + (particleRight * offset.x * scaleX + particleUp * offset.y * scaleY) * finalSize;

    output.Pos = mul(float4(vertexPos, 1.0), ViewProj);
    output.UV = offset * 0.5 + 0.5; // -1~1 -> 0~1
    
    output.LinearDepth = output.Pos.w;

    output.Color = float4(1.0, 1.0, 1.0, BaseAlpha * saturate(life));

    //float4 color = 1.0;
    //int w = int(p.VelocityScale.w);
    //if (w == 1)
    //    color = float4(1.0, 0.0, 0.0, 1.0);
    //else if (w == 2)
    //    color = float4(0.0, 1.0, 0.0, 1.0);
    //else if (w == 3)
    //    color = float4(1.0, 1.0, 0.0, 1.0);
    
    //output.Color = color;
    
    return output;
}