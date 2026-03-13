// IBLPrefilter.hlsl — Prefilter cubemap for specular IBL (GGX importance sampling)
// Dispatch: one thread per output texel, per face, per mip level

cbuffer PrefilterCB : register(b0)
{
    uint g_FaceIndex;
    uint g_OutputSize;     // Output face resolution for this mip
    uint g_SampleCount;    // Number of importance samples (e.g. 256)
    float g_Roughness;     // Roughness for this mip level
};

TextureCube<float4> g_InputCubemap : register(t0);
RWTexture2DArray<float4> g_OutputPrefiltered : register(u0);
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
    default: dir = float3(0, 0, 0); break;
    }
    return normalize(dir);
}

// Van der Corput radical inverse
float radicalInverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), radicalInverse(i));
}

// GGX importance sampling
float3 importanceSampleGGX(float2 xi, float roughness, float3 N)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = sinTheta * cos(phi);
    H.y = sinTheta * sin(phi);
    H.z = cosTheta;

    // Build TBN
    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    return normalize(H.x * T + H.y * B + H.z * N);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchID : SV_DispatchThreadID)
{
    if (dispatchID.x >= g_OutputSize || dispatchID.y >= g_OutputSize)
        return;

    float u = (float(dispatchID.x) + 0.5) / float(g_OutputSize) * 2.0 - 1.0;
    float v = (float(dispatchID.y) + 0.5) / float(g_OutputSize) * 2.0 - 1.0;
    float3 N = cubemapDirection(g_FaceIndex, u, v);
    float3 V = N; // V = N for prefiltering (view = reflection direction)

    float3 prefilteredColor = float3(0, 0, 0);
    float totalWeight = 0;

    float roughness = max(g_Roughness, 0.01);

    for (uint i = 0; i < g_SampleCount; ++i)
    {
        float2 xi = hammersley(i, g_SampleCount);
        float3 H = importanceSampleGGX(xi, roughness, N);

        // Reflect V around H to get L
        float VdotH = max(dot(V, H), 0.0);
        float3 L = 2.0 * VdotH * H - V;

        float NdotL = dot(N, L);
        if (NdotL > 0)
        {
            float3 sampleColor = g_InputCubemap.SampleLevel(g_LinearSampler, L, 0).rgb;
            prefilteredColor += sampleColor * NdotL;
            totalWeight += NdotL;
        }
    }

    if (totalWeight > 0)
        prefilteredColor /= totalWeight;

    g_OutputPrefiltered[uint3(dispatchID.x, dispatchID.y, g_FaceIndex)] = float4(prefilteredColor, 1.0);
}
