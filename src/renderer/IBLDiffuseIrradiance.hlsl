// IBLDiffuseIrradiance.hlsl — Project cubemap to SH3 and extract ZH3 irradiance coefficients
// Single-pass compute shader replacing IBLProjectSH + IBLReduceSH.
// Samples cubemap uniformly on the sphere, then reduces via shared memory.
//
// Algorithm aligned with UE ComputeSkyEnvMapDiffuseIrradianceCS:
//   - Uniform sphere sampling on cubemap (not equirect pixels)
//   - Shared memory parallel reduction (256 threads)
//   - ZH3 extraction with luminance zonal axis
//
// Reference: Roughton et al., "ZH3: Quadratic Zonal Harmonics", I3D 2024

#define GROUP_SIZE 256
#define NUM_SH 9

static const float PI = 3.14159265358979323846;

cbuffer CB : register(b0)
{
    uint g_CubeSize;     // Face resolution of the cubemap
    float g_MipLevel;    // Mip level to sample (log2(cubeSize) - 5 for ~32x32 effective)
    uint _pad0;
    uint _pad1;
};

TextureCube<float4> g_Cubemap : register(t0);
SamplerState g_LinearSampler : register(s0);
RWStructuredBuffer<float4> g_ZH3Output : register(u0); // 5 entries: SH2[0..3] + ZH3

groupshared float3 s_SH[NUM_SH][GROUP_SIZE];

// Uniform sphere sampling: map (u,v) in [0,1)^2 to unit sphere direction
float3 uniformSampleSphere(float2 E)
{
    float phi = 2.0 * PI * E.x;
    float cosTheta = 1.0 - 2.0 * E.y;
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    return float3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
}

// Radical inverse (Van der Corput sequence) for Hammersley
float2 hammersley(uint i, uint N)
{
    float E1 = float(i) / float(N);
    float E2 = float(reversebits(i)) * 2.3283064365386963e-10;
    return float2(E1, E2);
}

[numthreads(GROUP_SIZE, 1, 1)]
void CSMain(uint3 gtid : SV_GroupThreadID)
{
    uint threadIdx = gtid.x;

    // Each thread takes one sample on the sphere via stratified Hammersley sequence
    // Use multiple passes per thread for better convergence (8 passes × 256 threads = 2048 samples)
    float3 accum[NUM_SH];
    [unroll] for (int k = 0; k < NUM_SH; k++)
        accum[k] = float3(0, 0, 0);

    static const uint SAMPLES_PER_THREAD = 8;
    static const uint TOTAL_SAMPLES = GROUP_SIZE * SAMPLES_PER_THREAD;

    // Solid angle per sample: 4π / totalSamples (uniform sphere)
    float sampleWeight = (4.0 * PI) / float(TOTAL_SAMPLES);

    [unroll]
    for (uint s = 0; s < SAMPLES_PER_THREAD; s++)
    {
        uint sampleIdx = threadIdx + s * GROUP_SIZE;
        float2 E = hammersley(sampleIdx, TOTAL_SAMPLES);
        float3 dir = uniformSampleSphere(E);

        // Sample cubemap at the computed direction
        float3 rgb = g_Cubemap.SampleLevel(g_LinearSampler, dir, g_MipLevel).rgb;

        // SH3 basis functions (real, orthonormal)
        float x = dir.x, y = dir.y, z = dir.z;
        float basis[9];
        basis[0] = 0.282095;                           // Y_0^0
        basis[1] = 0.488603 * y;                        // Y_1^0  (y-up)
        basis[2] = 0.488603 * z;                        // Y_1^1
        basis[3] = 0.488603 * x;                        // Y_1^-1
        basis[4] = 1.092548 * x * y;                    // Y_2^-2
        basis[5] = 1.092548 * y * z;                    // Y_2^-1
        basis[6] = 0.315392 * (3.0 * z * z - 1.0);     // Y_2^0
        basis[7] = 1.092548 * x * z;                    // Y_2^1
        basis[8] = 0.546274 * (x * x - y * y);          // Y_2^2

        [unroll] for (int k = 0; k < NUM_SH; k++)
            accum[k] += rgb * basis[k] * sampleWeight;
    }

    // Store to shared memory
    [unroll] for (int k = 0; k < NUM_SH; k++)
        s_SH[k][threadIdx] = accum[k];
    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction within group
    for (uint stride = GROUP_SIZE / 2; stride > 0; stride >>= 1)
    {
        if (threadIdx < stride)
        {
            [unroll] for (int k = 0; k < NUM_SH; k++)
                s_SH[k][threadIdx] += s_SH[k][threadIdx + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Thread 0: extract ZH3 and output pre-convolved coefficients
    if (threadIdx == 0)
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
