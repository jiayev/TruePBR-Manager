// IBLIrradiance.hlsl — Convolve cubemap to irradiance map (diffuse IBL)
// Dispatch: one thread per output texel, per face

cbuffer IrradianceCB : register(b0)
{
    uint g_FaceIndex;
    uint g_OutputSize;   // Output irradiance face resolution (e.g. 64)
    uint g_SampleCount;  // Number of hemisphere samples (e.g. 512)
    uint _pad;
};

TextureCube<float4> g_InputCubemap : register(t0);
RWTexture2DArray<float4> g_OutputIrradiance : register(u0);
SamplerState g_LinearSampler : register(s0);

static const float PI = 3.14159265359;

float3 cubemapDirection(uint face, float u, float v)
{
    float3 dir;
    switch (face)
    {
    case 0: dir = float3( 1,  v, -u); break;
    case 1: dir = float3(-1,  v,  u); break;
    case 2: dir = float3( u,  1, -v); break;
    case 3: dir = float3( u, -1,  v); break;
    case 4: dir = float3( u,  v,  1); break;
    case 5: dir = float3(-u,  v, -1); break;
    default: dir = float3(0, 0, 0);
    }
    return normalize(dir);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchID : SV_DispatchThreadID)
{
    if (dispatchID.x >= g_OutputSize || dispatchID.y >= g_OutputSize)
        return;

    float u = (float(dispatchID.x) + 0.5) / float(g_OutputSize) * 2.0 - 1.0;
    float v = (float(dispatchID.y) + 0.5) / float(g_OutputSize) * 2.0 - 1.0;
    float3 N = cubemapDirection(g_FaceIndex, u, v);

    // Build TBN from N
    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    float3 irradiance = float3(0, 0, 0);

    for (uint s = 0; s < g_SampleCount; ++s)
    {
        // Stratified sampling
        float xi1 = (float(s) + 0.5) / float(g_SampleCount);
        float xi2 = float((s * 7u + 3u) % g_SampleCount) / float(g_SampleCount);

        float phi = 2.0 * PI * xi1;
        float cosTheta = sqrt(1.0 - xi2); // Cosine-weighted
        float sinTheta = sqrt(xi2);

        // Tangent space to world
        float3 H;
        H.x = sinTheta * cos(phi);
        H.y = sinTheta * sin(phi);
        H.z = cosTheta;

        float3 sampleDir = H.x * T + H.y * B + H.z * N;
        float3 sampleColor = g_InputCubemap.SampleLevel(g_LinearSampler, sampleDir, 0).rgb;
        irradiance += sampleColor;
    }

    irradiance /= float(g_SampleCount);
    g_OutputIrradiance[uint3(dispatchID.x, dispatchID.y, g_FaceIndex)] = float4(irradiance, 1.0);
}
