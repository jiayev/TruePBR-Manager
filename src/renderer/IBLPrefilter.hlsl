// IBLPrefilter.hlsl — Prefilter cubemap for specular IBL
// Filtered importance sampling (Karis 2014)
// Algorithm aligned with Epic Games / Unreal Engine ReflectionEnvironmentShaders.usf
// All math functions used are standard public-domain algorithms.
//
// Roughness per mip is computed by C++ using UE's heuristic:
//   roughness = exp2((1.0 - (maxMip - 1 - mip)) / 1.2)
// Sample counts are adaptive: 32 for rough, 64 for smooth surfaces.

cbuffer PrefilterCB : register(b0)
{
    uint g_FaceIndex;
    uint g_OutputSize;     // Output face resolution for this mip
    uint g_SampleCount;    // Number of importance samples (32 or 64, set by C++)
    float g_Roughness;     // Roughness for this mip level (computed by C++ via UE heuristic)
    uint g_InputSize;      // Input cubemap face resolution (mip 0)
};

TextureCube<float4> g_InputCubemap : register(t0);
RWTexture2DArray<float4> g_OutputPrefiltered : register(u0);
SamplerState g_LinearSampler : register(s0);

static const float PI = 3.14159265359;

// ── Cubemap face direction ──────────────────────────────────

// D3D / UE standard convention — matches hardware TextureCube SRV face mapping.
float3 cubemapDirection(uint face, float u, float v)
{
    float3 dir;
    switch (face)
    {
    case 0: dir = float3( 1, -v, -u); break; // +X
    case 1: dir = float3(-1, -v,  u); break; // -X
    case 2: dir = float3( u,  1,  v); break; // +Y
    case 3: dir = float3( u, -1, -v); break; // -Y
    case 4: dir = float3( u, -v,  1); break; // +Z
    case 5: dir = float3(-u, -v, -1); break; // -Z
    default: dir = float3(0, 0, 0); break;
    }
    return normalize(dir);
}

// ── Quasi-random sequence ───────────────────────────────────

float2 hammersley(uint i, uint N)
{
    float E1 = float(i) / float(N);
    float E2 = float(reversebits(i)) * 2.3283064365386963e-10;
    return float2(E1, E2);
}

// ── Tangent basis — Duff et al. 2017, "Building an Orthonormal Basis, Revisited" ──

float3x3 getTangentBasis(float3 N)
{
    float Sign = N.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (Sign + N.z);
    float b = N.x * N.y * a;
    float3 T = float3(1.0 + Sign * a * N.x * N.x, Sign * b, -Sign * N.x);
    float3 B = float3(b, Sign + a * N.y * N.y, -N.y);
    return float3x3(T, B, N);
}

// ── GGX NDF (Walter et al. 2007) ────────────────────────────

float D_GGX(float a2, float NoH)
{
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}

// ── GGX importance sampling in tangent space ────────────────
// a2 = alpha^2 = Pow4(roughness)
// Returns float4(H_tangentSpace, PDF)

float4 importanceSampleGGX(float2 E, float a2)
{
    float Phi = 2.0 * PI * E.x;
    float CosTheta = sqrt((1.0 - E.y) / (1.0 + (a2 - 1.0) * E.y));
    float SinTheta = sqrt(1.0 - CosTheta * CosTheta);

    float3 H;
    H.x = SinTheta * cos(Phi);
    H.y = SinTheta * sin(Phi);
    H.z = CosTheta;

    float d = (CosTheta * a2 - CosTheta) * CosTheta + 1.0;
    float PDF = a2 / (PI * d * d) * CosTheta;

    return float4(H, PDF);
}

// ── Cosine hemisphere sampling in tangent space ─────────────

float4 cosineSampleHemisphere(float2 E)
{
    float Phi = 2.0 * PI * E.x;
    float CosTheta = sqrt(E.y);
    float SinTheta = sqrt(1.0 - CosTheta * CosTheta);

    float3 H;
    H.x = SinTheta * cos(Phi);
    H.y = SinTheta * sin(Phi);
    H.z = CosTheta;

    float PDF = CosTheta / PI;

    return float4(H, PDF);
}

// ═════════════════════════════════════════════════════════════

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchID : SV_DispatchThreadID)
{
    if (dispatchID.x >= g_OutputSize || dispatchID.y >= g_OutputSize)
        return;

    float u = (float(dispatchID.x) + 0.5) / float(g_OutputSize) * 2.0 - 1.0;
    float v = (float(dispatchID.y) + 0.5) / float(g_OutputSize) * 2.0 - 1.0;
    float3 N = cubemapDirection(g_FaceIndex, u, v);

    float Roughness = g_Roughness;

    // Mirror-like surface: just copy source
    if (Roughness < 0.01)
    {
        float4 color = g_InputCubemap.SampleLevel(g_LinearSampler, N, 0);
        color = -min(-color, 0);
        g_OutputPrefiltered[uint3(dispatchID.x, dispatchID.y, g_FaceIndex)] = color;
        return;
    }

    float3x3 TangentToWorld = getTangentBasis(N);

    uint CubeSize = g_InputSize;
    float SolidAngleTexel = 4.0 * PI / (6.0 * float(CubeSize) * float(CubeSize)) * 2.0;

    uint NumSamples = g_SampleCount;

    float4 FilteredColor = float4(0, 0, 0, 0);

    if (Roughness > 0.99)
    {
        // GGX at roughness~1 approaches constant; use cosine distribution instead
        for (uint i = 0; i < NumSamples; i++)
        {
            float2 E = hammersley(i, NumSamples);

            float3 L = cosineSampleHemisphere(E).xyz;
            float NoL = L.z;

            float PDF = NoL / PI;
            float SolidAngleSample = 1.0 / (NumSamples * PDF);
            float Mip = 0.5 * log2(SolidAngleSample / SolidAngleTexel);

            L = mul(L, TangentToWorld);
            FilteredColor += g_InputCubemap.SampleLevel(g_LinearSampler, L, Mip);
        }

        FilteredColor /= float(NumSamples);
    }
    else
    {
        float Weight = 0;

        // a2 = alpha^2 = roughness^4 (standard GGX remapping)
        float a2 = Roughness * Roughness;
        a2 *= a2;

        for (uint i = 0; i < NumSamples; i++)
        {
            float2 E = hammersley(i, NumSamples);
            E.y *= 0.995; // Avoid perfectly specular edge case

            float3 H = importanceSampleGGX(E, a2).xyz;
            float3 L = 2.0 * H.z * H - float3(0, 0, 1);

            float NoL = L.z;
            float NoH = H.z;

            if (NoL > 0)
            {
                // PDF simplified for V=N assumption: D*NoH/(4*VoH) = D*0.25
                float PDF = D_GGX(a2, NoH) * 0.25;
                float SolidAngleSample = 1.0 / (NumSamples * PDF);
                float Mip = 0.5 * log2(SolidAngleSample / SolidAngleTexel);

                L = mul(L, TangentToWorld);
                FilteredColor += g_InputCubemap.SampleLevel(g_LinearSampler, L, Mip) * NoL;
                Weight += NoL;
            }
        }

        FilteredColor /= Weight;
    }

    // NaN prevention
    FilteredColor = -min(-FilteredColor, 0);

    g_OutputPrefiltered[uint3(dispatchID.x, dispatchID.y, g_FaceIndex)] = FilteredColor;
}
