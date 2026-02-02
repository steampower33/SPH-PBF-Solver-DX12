
struct Particle
{
    float3 Position;
    float Density;
    float3 Velocity;
    float Pressure;
    float3 OldPosition;
    float Padding;
};

// Simulation Constants
cbuffer SimParams : register(b0)
{
    float g_DeltaTime;
    uint g_NumParticles;
    // Grid Setup
    float g_CellSize; // e.g., 2.0 * ParticleRadius
    uint g_GridDim; // Grid Dimensions (e.g., 128x128x128)
    float g_Mass;
    float g_RestDensity;
    float g_Viscosity;
    float p0;
    float4 g_Box;
};

// [Core Logic] 3D Position -> 1D Grid Hash
// This maps a 3D coordinate to a unique 1D cell ID.
// Using Prime Numbers to minimize collisions.
uint GetGridHash(float3 pos)
{
    int3 gridPos = (int3) floor(pos / g_CellSize);
    
    // Handle negative coordinates simply by adding a large offset or abs (Simple approach)
    // Better: use unsigned wrap-around logic
    gridPos += int3(1000, 1000, 1000);
    
    // Primes: 73856093, 19349663, 83492791
    return ((uint) (gridPos.x * 73856093) ^
            (uint) (gridPos.y * 19349663) ^
            (uint) (gridPos.z * 83492791)) % (g_GridDim * g_GridDim * g_GridDim);
}

//#define PI 3.14159265359f

//// -----------------------------------------------------------------------------
//// 1. Poly6 Kernel
//// Used for: Density Estimation
//// Pros: Smooth, accurate for density.
//// Cons: Gradient vanishes at r=0 (Bad for forces/pressure), so we don't use it for Lambda.
//// -----------------------------------------------------------------------------
//float Poly6Kernel(float rSq, float h)
//{
//    float hSq = h * h;
//    if (rSq >= 0.0f && rSq <= hSq)
//    {
//        // Coeff = 315 / (64 * PI * h^9)
//        float coeff = 315.0f / (64.0f * PI * pow(h, 9.0f));
//        float term = hSq - rSq;
//        return coeff * term * term * term;
//    }
//    return 0.0f;
//}

//// -----------------------------------------------------------------------------
//// 2. Spiky Kernel Gradient (Magnitude)
//// Used for: Gradient Calculation (Lambda & Position Delta)
//// Pros: Non-zero gradient at r=0, prevents clustering/clumping.
//// Returns: The scalar magnitude. You must multiply this by normalize(rVec).
//// -----------------------------------------------------------------------------
//float SpikyKernelGrad(float r, float h)
//{
//    // Note: r is distance (sqrt(rSq)), not squared distance.
//    if (r > 0.0f && r <= h)
//    {
//        // Coeff = -45 / (PI * h^6)
//        float coeff = -45.0f / (PI * pow(h, 6.0f));
//        float term = h - r;
//        return coeff * term * term;
//    }
//    return 0.0f;
//}

#define PI 3.14159265359f

// -----------------------------------------------------------------------------
// [2D Version] Poly6 Kernel
// -----------------------------------------------------------------------------
float Poly6Kernel(float rSq, float h)
{
    float hSq = h * h;
    if (rSq >= 0.0f && rSq <= hSq)
    {
        // [3D] 315 / (64 * PI * h^9)
        // [2D] 4 / (PI * h^8)
        float coeff = 4.0f / (PI * pow(h, 8.0f));
        float term = hSq - rSq;
        return coeff * term * term * term;
    }
    return 0.0f;
}

// -----------------------------------------------------------------------------
// [2D Version] Spiky Kernel Gradient
// -----------------------------------------------------------------------------
float SpikyKernelGrad(float r, float h)
{
    if (r > 0.0f && r <= h)
    {
        // [3D] -45 / (PI * h^6)
        // [2D] -30 / (PI * h^5)
        float coeff = -30.0f / (PI * pow(h, 5.0f));
        float term = h - r;
        return coeff * term * term;
    }
    return 0.0f;
}