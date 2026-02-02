
struct Particle
{
    float3 Position;
    float Density;
    float3 Velocity;
    float Pressure;
};

// Simulation Constants
cbuffer SimParams : register(b0)
{
    float g_DeltaTime;
    uint g_NumParticles;
    // Grid Setup
    float g_CellSize; // e.g., 2.0 * ParticleRadius
    uint g_GridDim; // Grid Dimensions (e.g., 128x128x128)
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