
uint WangHash(uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

float Random01(uint seed)
{
    return float(WangHash(seed)) * (1.0 / 4294967296.0);
}

// [Core Logic] 3D Position -> 1D Grid Hash
// This maps a 3D coordinate to a unique 1D cell ID.
// Using Prime Numbers to minimize collisions.
uint GetGridHash(float3 pos, float cellSize, uint gridDim)
{
    int3 gridPos = (int3) floor(pos / cellSize);
    
    // Handle negative coordinates simply by adding a large offset or abs (Simple approach)
    // Better: use unsigned wrap-around logic
    gridPos += int3(1000, 1000, 1000);
    
    // Primes: 73856093, 19349663, 83492791
    return ((uint) (gridPos.x * 73856093) ^
            (uint) (gridPos.y * 19349663) ^
            (uint) (gridPos.z * 83492791)) % (gridDim * gridDim * gridDim);
}
