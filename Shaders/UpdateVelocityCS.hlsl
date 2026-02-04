#include "Common.hlsli"

// Resources
RWStructuredBuffer<Particle> g_Particles : register(u0);
RWStructuredBuffer<uint2> g_GridIndices : register(u1);
RWStructuredBuffer<float3> g_Vorticities : register(u4); // Input from VorticityCS

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= g_NumParticles)
        return;

    Particle p = g_Particles[id];

    float3 disp = p.Position - p.OldPosition;
    
    if (g_DeltaTime > 1e-6f) 
        p.Velocity = disp / g_DeltaTime;
    else
        p.Velocity = 0.0f;

    float3 viscosityForce = float3(0, 0, 0);
    float3 eta = float3(0, 0, 0);
    
    float3 myOmega = g_Vorticities[id];
    float myOmegaLen = length(myOmega);

    float h = g_CellSize;
    float hSq = h * h;
    
    float c = g_Viscosity; // XSPH coefficient
    float epsilon = g_epsilon; // Vorticity confinement coefficient

    int3 myGridPos = (int3) floor(p.Position / h) + int3(1000, 1000, 1000);

    for (int z = -1; z <= 1; ++z)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                int3 neighborGridPos = myGridPos + int3(x, y, z);
                
                uint neighborHash = ((uint) (neighborGridPos.x * 73856093) ^
                                     (uint) (neighborGridPos.y * 19349663) ^
                                     (uint) (neighborGridPos.z * 83492791)) % (g_GridDim * g_GridDim * g_GridDim);
                
                uint2 cellRange = g_GridIndices[neighborHash];
                
                for (uint j = cellRange.x; j < cellRange.y; ++j)
                {
                    if (id == j)
                        continue;

                    Particle pj = g_Particles[j];
                    float3 rVec = p.Position - pj.Position;
                    float rSq = dot(rVec, rVec);

                    if (rSq < hSq && rSq > 1e-6f)
                    {
                        float r = sqrt(rSq);

                        // --- [A] XSPH Viscosity Accumulation ---
                        // Formula: v_new = v + c * Sum((v_j - v_i) * W)
                        float3 v_diff = pj.Velocity - p.Velocity;
                        viscosityForce += v_diff * Poly6Kernel(rSq, h);
                        
                        // --- [B] Vorticity Gradient Accumulation ---
                        // Formula: eta = Sum( (|omega_j| - |omega_i|) * GradW )
                        float3 omegaJ = g_Vorticities[j];
                        float omegaLenJ = length(omegaJ);
                        
                        // Gradient of W points towards self (normalized rVec)
                        float3 gradW = normalize(rVec) * SpikyKernelGrad(r, h);
                        eta += (omegaLenJ - myOmegaLen) * gradW;
                    }
                }
            }
        }
    }

    // [A] Apply XSPH Viscosity
    // Multiply by volume (mass / rest_density) for physical correctness
    float volume = g_Mass / g_RestDensity;
    p.Velocity += c * viscosityForce * volume;

    // [B] Apply Vorticity Confinement
    float etaLen = length(eta);
    if (etaLen > 1e-6f)
    {
        // Calculate correction force direction N
        float3 N = eta / etaLen;
        
        // Force F = epsilon * (N x omega)
        // Apply force to velocity: v += F * dt
        float3 vorticityForce = g_vorticityEpsilon * cross(N, myOmega);
        p.Velocity += vorticityForce * g_DeltaTime;
    }

    g_Particles[id] = p;
}