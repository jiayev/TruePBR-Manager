// IBLBrdfLut.hlsl — Compute BRDF integration LUT for split-sum approximation
// Dispatch: one thread per output texel (2D LUT)
//
// Visibility function: Smith height-correlated GGX (Heitz 2014)
// Alpha mapping: a = roughness^2, a2 = roughness^4 (standard GGX remapping)
// Generates the standard split-sum BRDF integration LUT.

cbuffer BrdfLutCB : register(b0)
{
    uint g_LutSize;       // Output resolution (e.g. 256)
    uint g_SampleCount;   // Number of integration samples (e.g. 1024)
    uint2 _pad;
};

RWTexture2D<float2> g_OutputLut : register(u0);

static const float PI = 3.14159265359;

float2 hammersley(uint i, uint N)
{
    float E1 = float(i) / float(N);
    float E2 = float(reversebits(i)) * 2.3283064365386963e-10;
    return float2(E1, E2);
}

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
    float PDF = a2 / (PI * d * d);

    return float4(H, PDF);
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

    // Alpha mapping: a2 = roughness^4 (standard GGX remapping)
    float a = roughness * roughness;
    float a2 = a * a;

    float A = 0;
    float B = 0;

    for (uint i = 0; i < g_SampleCount; ++i)
    {
        float2 xi = hammersley(i, g_SampleCount);
        float3 H = importanceSampleGGX(xi, a2).xyz;

        float VdotH = max(dot(V, H), 0.0);
        float3 L = 2.0 * VdotH * H - V;

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        VdotH = max(VdotH, 0.0);

        if (NdotL > 0)
        {
            // Smith height-correlated GGX visibility (Heitz 2014)
            // Vis = 0.5 / (SmithV + SmithL)
            // SmithV = NdotL * sqrt(NdotV^2 * (1-a2) + a2)
            // SmithL = NdotV * sqrt(NdotL^2 * (1-a2) + a2)
            float Vis_SmithV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
            float Vis_SmithL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
            float Vis = 0.5 / (Vis_SmithV + Vis_SmithL + 1e-7);

            // G_Vis = Vis * 4 * NdotL * VdotH / NdotH
            float G_Vis = Vis * (4.0 * NdotL * VdotH / (NdotH + 1e-7));
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    A /= float(g_SampleCount);
    B /= float(g_SampleCount);

    g_OutputLut[dispatchID.xy] = float2(A, B);
}
