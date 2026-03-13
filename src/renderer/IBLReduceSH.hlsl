// IBLReduceSH.hlsl — Reduce SH3 partial sums and extract ZH3 irradiance coefficients
// Single thread group reduces all partial sums from IBLProjectSH, then computes
// the ZH3 representation: pre-convolved SH2 (4 × float4) + ZH3 coefficient (1 × float4).
//
// Reference: Roughton et al., "ZH3: Quadratic Zonal Harmonics", I3D 2024

#define GROUP_SIZE 256
#define NUM_SH 9

static const float PI = 3.14159265358979323846;

cbuffer CB : register(b0)
{
    uint g_NumGroups;
    uint3 _pad;
};

StructuredBuffer<float4> g_PartialInput : register(t0);  // numGroups × 9 entries
RWStructuredBuffer<float4> g_ZH3Output  : register(u0);  // 5 entries: SH2[0..3] + ZH3

groupshared float3 s_SH[NUM_SH][GROUP_SIZE];

[numthreads(GROUP_SIZE, 1, 1)]
void CSMain(uint3 gtid : SV_GroupThreadID)
{
    // Each thread sequentially accumulates a subset of group partial sums
    float3 localSum[NUM_SH];
    [unroll] for (int k = 0; k < NUM_SH; k++)
        localSum[k] = float3(0, 0, 0);

    for (uint g = gtid.x; g < g_NumGroups; g += GROUP_SIZE)
    {
        [unroll] for (int k = 0; k < NUM_SH; k++)
            localSum[k] += g_PartialInput[g * NUM_SH + k].xyz;
    }

    [unroll] for (int k = 0; k < NUM_SH; k++)
        s_SH[k][gtid.x] = localSum[k];
    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction
    for (uint stride = GROUP_SIZE / 2; stride > 0; stride >>= 1)
    {
        if (gtid.x < stride)
        {
            [unroll] for (int k = 0; k < NUM_SH; k++)
                s_SH[k][gtid.x] += s_SH[k][gtid.x + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Thread 0: extract ZH3 and output pre-convolved coefficients
    if (gtid.x == 0)
    {
        // Final SH3 radiance coefficients
        float3 raw[9];
        [unroll] for (int k = 0; k < NUM_SH; k++)
            raw[k] = s_SH[k][0];

        // ── Compute luminance zonal axis from L1 band ──
        // Shared luminance axis avoids per-channel color fringing (Section 3.2)
        const float3 lumW = float3(0.2126, 0.7152, 0.0722);
        float3 l1Vec = float3(
            dot(raw[3], lumW),   // x (positive basis convention)
            dot(raw[1], lumW),   // y
            dot(raw[2], lumW)    // z
        );
        float l1Len = length(l1Vec);
        float3 axis = (l1Len > 1e-12) ? (l1Vec / l1Len) : float3(0, 1, 0);

        // ── Compute ZH3 f_2^0 (stored approach, Section 3.1) ──
        // Evaluate L2 basis at axis direction: q = Y2(axis)
        float ax = axis.x, ay = axis.y, az = axis.z;
        float q[5];
        q[0] = 1.092548 * ax * ay;
        q[1] = 1.092548 * ay * az;
        q[2] = 0.315392 * (3.0 * az * az - 1.0);
        q[3] = 1.092548 * ax * az;
        q[4] = 0.546274 * (ax * ax - ay * ay);

        // f_2^0 = K_2^0 × k2 = K_2^0 × (4π/5) × dot(q, f2)   per channel
        // K_2^0 = sqrt(5/(4π)),  combined factor = sqrt(5/(4π)) × 4π/5
        float factor = sqrt(5.0 / (4.0 * PI)) * (4.0 * PI / 5.0);

        float3 f20;
        f20.r = factor * (q[0]*raw[4].r + q[1]*raw[5].r + q[2]*raw[6].r + q[3]*raw[7].r + q[4]*raw[8].r);
        f20.g = factor * (q[0]*raw[4].g + q[1]*raw[5].g + q[2]*raw[6].g + q[3]*raw[7].g + q[4]*raw[8].g);
        f20.b = factor * (q[0]*raw[4].b + q[1]*raw[5].b + q[2]*raw[6].b + q[3]*raw[7].b + q[4]*raw[8].b);

        // ── Pre-convolve with irradiance cosine kernel ──
        // Ramamoorthi & Hanrahan 2001: A_0 = π, A_1 = 2π/3, A_2 = π/4
        float3 sh0 = PI * raw[0];                    // DC band
        float3 sh1 = (2.0 * PI / 3.0) * raw[1];      // L1 y
        float3 sh2 = (2.0 * PI / 3.0) * raw[2];      // L1 z
        float3 sh3 = (2.0 * PI / 3.0) * raw[3];      // L1 x
        float3 zh3 = (PI / 4.0) * f20;                // ZH3 zonal L2

        // ── Write output ──
        g_ZH3Output[0] = float4(sh0, 0);
        g_ZH3Output[1] = float4(sh1, 0);
        g_ZH3Output[2] = float4(sh2, 0);
        g_ZH3Output[3] = float4(sh3, 0);
        g_ZH3Output[4] = float4(zh3, 0);
    }
}
