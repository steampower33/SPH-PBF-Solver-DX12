#ifndef COMMON_HLSLI
#define COMMON_HLSLI

struct SimParams
{
    uint NumParticles;
    float DeltaTime;
    float CellSize;
    uint GridDim;
    
    float Mass;
    float RestDensity;
    float Viscosity;
    float GravityY;

    float2 BoxX;
    float2 BoxY;

    float2 BoxZ;
    float Epsilon;
    float k;

    float n;
    float DqScale;
    float VorticityEpsilon;
    float ExternalAccel;
    
    float JitterFactor;
};

#include "Hash.hlsli"

#define DIMENSION_3D
#define PI 3.14159265359

#ifdef DIMENSION_3D
// -----------------------------------------------------------------------------
// 1. Poly6 Kernel
// Used for: Density Estimation
// Pros: Smooth, accurate for density.
// Cons: Gradient vanishes at r=0 (Bad for forces/pressure), so we don't use it for Lambda.
// -----------------------------------------------------------------------------
float Poly6Kernel(float rSq, float h)
{
    float hSq = h * h;
    if (rSq >= 0.0f && rSq <= hSq)
    {
        // Coeff = 315 / (64 * PI * h^9)
        float coeff = 315.0f / (64.0f * PI * pow(h, 9.0f));
        float term = hSq - rSq;
        return coeff * term * term * term;
    }
    return 0.0f;
}

// -----------------------------------------------------------------------------
// 2. Spiky Kernel Gradient (Magnitude)
// Used for: Gradient Calculation (Lambda & Position Delta)
// Pros: Non-zero gradient at r=0, prevents clustering/clumping.
// Returns: The scalar magnitude. You must multiply this by normalize(rVec).
// -----------------------------------------------------------------------------
float SpikyKernelGrad(float r, float h)
{
    // Note: r is distance (sqrt(rSq)), not squared distance.
    if (r > 0.0f && r <= h)
    {
        // Coeff = -45 / (PI * h^6)
        float coeff = -45.0f / (PI * pow(h, 6.0f));
        float term = h - r;
        return coeff * term * term;
    }
    return 0.0f;
}
#endif

#ifdef DIMENSION_2D
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
#endif

#endif