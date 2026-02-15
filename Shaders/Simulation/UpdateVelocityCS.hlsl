#include "SolverCommon.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    
    uint numParticles = g_SP.NumParticles;
    
    if (id >= numParticles)
        return;
    
    float dt = g_SP.DeltaTime;
    
    float3 pi = g_PosPred[id];
    float3 old_pi = g_PosOld[id];
    float3 disp = pi - old_pi;
    
    float3 vi = 0.0;
    if (dt > 1e-6)
        vi = disp / dt;
    else
        vi = 0.0;

    float3 viscosityForce = float3(0, 0, 0);
    float3 eta = float3(0, 0, 0);
    
    float3 myOmega = g_Vorticity[id];
    float myOmegaLen = length(myOmega);

    float h = g_SP.CellSize;
    float hSq = h * h;
    
    float c = g_SP.Viscosity; // XSPH coefficient
    float epsilon = g_SP.Epsilon; // Vorticity confinement coefficient

    int3 myGridPos = (int3) floor(pi / h) + int3(1000, 1000, 1000);

    for (int z = -1; z <= 1; ++z)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                int3 neighborGridPos = myGridPos + int3(x, y, z);
                
                uint gridDim = g_SP.GridDim;
                uint neighborHash = ((uint) (neighborGridPos.x * 73856093) ^
                                     (uint) (neighborGridPos.y * 19349663) ^
                                     (uint) (neighborGridPos.z * 83492791)) % (gridDim * gridDim * gridDim);

                uint2 cellRange = g_GridIndices[neighborHash];
                
                for (uint j = cellRange.x; j < cellRange.y; ++j)
                {
                    if (id == j)
                        continue;

                    float3 pj = g_PosPred[j];
                    float3 rVec = pi - pj;
                    float rSq = dot(rVec, rVec);

                    if (rSq < hSq && rSq > 1e-6)
                    {
                        float r = sqrt(rSq);
                        
                        float3 vj = g_VelIn[j];
                        
                        // --- [A] XSPH Viscosity Accumulation ---
                        // Formula: v_new = v + c * Sum((v_j - v_i) * W)
                        float3 v_diff = vj - vi;
                        viscosityForce += v_diff * Poly6Kernel(rSq, h);
                        
                        // --- [B] Vorticity Gradient Accumulation ---
                        // Formula: eta = Sum( (|omega_j| - |omega_i|) * GradW )
                        float3 omegaJ = g_Vorticity[j];
                        float omegaLenJ = length(omegaJ);
                        
                        // Gradient of W points towards self (normalized rVec)
                        float3 gradW = (rVec / r) * SpikyKernelGrad(r, h);
                        eta += (omegaLenJ - myOmegaLen) * gradW;
                    }
                }
            }
        }
    }

    // [A] Apply XSPH Viscosity
    // Multiply by volume (mass / rest_density) for physical correctness
    float volume = g_SP.Mass / g_SP.RestDensity;
    vi += c * viscosityForce * volume;

    // [B] Apply Vorticity Confinement
    float etaLen = length(eta);
    if (etaLen > 1e-6)
    {
        // Calculate correction force direction N
        float3 N = eta / etaLen;
        
        // Force F = epsilon * (N x omega)
        float3 vorticityForce = g_SP.VorticityEpsilon * cross(N, myOmega);
        vi -= vorticityForce * dt;
    }
    
    g_RW_VelOut[id] = vi;
}