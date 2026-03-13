// IBLBrdfLut.hlsl — Compute BRDF integration LUT for split-sum approximation
// Dispatch: one thread per output texel (2D LUT)

cbuffer BrdfLutCB : register(b0)
{
    uint g_LutSize;       // Output resolution (e.g. 256)
    uint g_SampleCount;   // Number of integration samples (e.g. 1024)
    uint2 _pad;
};

RWTexture2D<float2> g_OutputLut : register(u0);

static const float PI = 3.14159265359;

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

    // N = (0, 0, 1) for LUT generation, so TBN is identity
    return H;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchID : SV_DispatchThreadID)
{
    if (dispatchID.x >= g_LutSize || dispatchID.y >= g_LutSize)
        return;

    float NdotV = (float(dispatchID.x) + 0.5) / float(g_LutSize);
    NdotV = max(NdotV, 0.001);
    float roughness = (float(dispatchID.y) + 0.5) / float(g_LutSize);
    roughness = max(roughness, 0.01);

    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0;
    V.z = NdotV;

    float3 N = float3(0, 0, 1);

    float A = 0;
    float B = 0;

    for (uint i = 0; i < g_SampleCount; ++i)
    {
        float2 xi = hammersley(i, g_SampleCount);
        float3 H = importanceSampleGGX(xi, roughness, N);

        float VdotH = max(dot(V, H), 0.0);
        float3 L = 2.0 * VdotH * H - V;

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        VdotH = max(VdotH, 0.0);

        if (NdotL > 0)
        {
            float r2 = roughness * roughness;
            float k = r2 / 2.0;

            float G_V = NdotV / (NdotV * (1.0 - k) + k);
            float G_L = NdotL / (NdotL * (1.0 - k) + k);
            float G = G_V * G_L;

            float G_Vis = (G * VdotH) / (NdotH * NdotV + 0.0001);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    A /= float(g_SampleCount);
    B /= float(g_SampleCount);

    g_OutputLut[dispatchID.xy] = float2(A, B);
}
