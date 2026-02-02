struct Particle
{
    float3 Position;
    float Density;
    float3 Velocity;
    float Pressure;
};

RWStructuredBuffer<Particle> gParticles : register(u0);

cbuffer SimParams : register(b0)
{
    float g_DeltaTime;
    uint g_ParticleCount;
    float2 g_Padding;
};

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_ParticleCount)
        return;

    Particle p = gParticles[id];

    float3 gravity = float3(0.0f, -9.81f, 0.0f);
    p.Velocity += gravity * g_DeltaTime;
    p.Position += p.Velocity * g_DeltaTime;

    if (p.Position.y < -5.0f)
    {
        p.Position.y = -5.0f;
        p.Velocity.y *= -0.5f; // Damping
        p.Velocity.x *= 0.9f; // Friction
        //p.Velocity.z *= 0.9f;
    }

    gParticles[id] = p;
}