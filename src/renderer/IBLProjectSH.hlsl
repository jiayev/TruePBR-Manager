// IBLProjectSH.hlsl — Project equirectangular HDR map into SH3 partial sums
// Each thread group processes up to GROUP_SIZE pixels, reduces via shared memory,
// and outputs 9 × float4 partial sums to a structured buffer.

#define GROUP_SIZE 256
#define NUM_SH 9

static const float PI = 3.14159265358979323846;

cbuffer CB : register(b0)
{
    uint g_Width;
    uint g_Height;
    uint g_TotalPixels;
    uint _pad0;
};

Texture2D<float4> g_Equirect : register(t0);
RWStructuredBuffer<float4> g_PartialSums : register(u0); // numGroups × 9 entries

groupshared float3 s_SH[NUM_SH][GROUP_SIZE];

[numthreads(GROUP_SIZE, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID,
            uint3 gtid : SV_GroupThreadID,
            uint3 gid  : SV_GroupID)
{
    float3 contrib[NUM_SH];
    [unroll] for (int k = 0; k < NUM_SH; k++)
        contrib[k] = float3(0, 0, 0);

    uint idx = dtid.x;
    if (idx < g_TotalPixels)
    {
        uint px = idx % g_Width;
        uint py = idx / g_Width;

        float u = ((float)px + 0.5) / (float)g_Width;
        float v = ((float)py + 0.5) / (float)g_Height;

        // Match dirToEquirectUV convention in IBLEquirectToCube.hlsl
        float azimuth   = (2.0 * u - 1.0) * PI;   // -PI to +PI
        float elevation = (0.5 - v) * PI;           // +PI/2 to -PI/2

        float cosEl = cos(elevation);
        float sinEl = sin(elevation);

        float x = cosEl * cos(azimuth);
        float y = sinEl;
        float z = cosEl * sin(azimuth);

        float dOmega = cosEl * (2.0 * PI / (float)g_Width) * (PI / (float)g_Height);

        float3 rgb = g_Equirect[uint2(px, py)].rgb;

        // SH3 basis functions (positive convention, 9 functions)
        float basis[9];
        basis[0] = 0.282095;                           // Y_0^0
        basis[1] = 0.488603 * y;                        // Y_1 (y)
        basis[2] = 0.488603 * z;                        // Y_1 (z)
        basis[3] = 0.488603 * x;                        // Y_1 (x)
        basis[4] = 1.092548 * x * y;                    // Y_2^{-2}
        basis[5] = 1.092548 * y * z;                    // Y_2^{-1}
        basis[6] = 0.315392 * (3.0 * z * z - 1.0);     // Y_2^0
        basis[7] = 1.092548 * x * z;                    // Y_2^1
        basis[8] = 0.546274 * (x * x - y * y);          // Y_2^2

        [unroll] for (int k = 0; k < NUM_SH; k++)
            contrib[k] = rgb * basis[k] * dOmega;
    }

    // Store to shared memory
    [unroll] for (int k = 0; k < NUM_SH; k++)
        s_SH[k][gtid.x] = contrib[k];
    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction within group
    for (uint stride = GROUP_SIZE / 2; stride > 0; stride >>= 1)
    {
        if (gtid.x < stride)
        {
            [unroll] for (int k = 0; k < NUM_SH; k++)
                s_SH[k][gtid.x] += s_SH[k][gtid.x + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Thread 0 writes group partial sum
    if (gtid.x == 0)
    {
        [unroll] for (int k = 0; k < NUM_SH; k++)
            g_PartialSums[gid.x * NUM_SH + k] = float4(s_SH[k][0], 0.0);
    }
}
