RWStructuredBuffer<uint> g_Counters : register(u2);

[numthreads(1, 1, 1)]
void main()
{
    //g_Counters[0] = 0;
    g_Counters[1] = 0;
}