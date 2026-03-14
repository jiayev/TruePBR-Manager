// PBR Preview Shader — Feature-aware with CS-aligned BRDF + IBL
// Uses Common/*.hlsli adapted from skyrim-community-shaders

#include "Common/PBR.hlsli"

// ─── Constant Buffers ──────────────────────────────────────

cbuffer SceneCB : register(b0)
{
    row_major float4x4 g_WorldViewProj;
    row_major float4x4 g_World;
    row_major float4x4 g_WorldInvTranspose;
    float3 g_CameraPos;
    float _pad0;
    float3 g_LightDir; // normalized, towards light
    float _pad1;
    float3 g_LightColor;
    float g_LightIntensity;
    float g_IBLIntensity; // IBL environment intensity (0 = disabled)
    float g_MaxPrefilteredMip;
    uint g_FrameCount;
    float g_EnvRotation; // HDRI Y-axis rotation (radians)
    float4 g_SH[4];  // Pre-convolved SH2 radiance: [0]=DC, [1]=L1y, [2]=L1z, [3]=L1x
    float4 g_ZH3;    // Pre-convolved ZH3 zonal L2 coefficient (.xyz = RGB)
    row_major float4x4 g_InvViewProj; // Inverse view-projection (used by skybox)
};

cbuffer MaterialCB : register(b1)
{
    float g_SpecularLevel;
    float g_RoughnessScale;
    uint g_RenderFlags; // bit0=HorizonOcclusion, bit1=MultiBounceAO, bit2=SpecularOcclusion
    uint g_FeatureFlags; // PBR::Flags bitmask

    float3 g_SubsurfaceColor;
    float g_SubsurfaceOpacity;

    float g_CoatStrength;
    float g_CoatRoughness;
    float g_CoatSpecularLevel;
    float g_EmissiveScale;

    float3 g_FuzzColor;
    float g_FuzzWeight;

    float g_GlintScreenSpaceScale;
    float g_GlintLogMicrofacetDensity;
    float g_GlintMicrofacetRoughness;
    float g_GlintDensityRandomization;
};

// ─── Textures & Samplers ───────────────────────────────────

Texture2D g_Diffuse : register(t0);             // sRGB base color RGB + opacity A
Texture2D g_Normal : register(t1);              // Normal map (DX convention)
Texture2D g_RMAOS : register(t2);               // R=Roughness G=Metallic B=AO A=Specular
TextureCube g_PrefilteredMap : register(t3);     // Specular IBL (prefiltered cubemap with mips)
Texture2D g_BRDFLut : register(t4);             // BRDF integration LUT
Texture2D g_Emissive : register(t5);            // Emissive RGB
Texture2D g_FeatureTex0 : register(t6);         // CoatNormalRoughness / Fuzz
Texture2D g_FeatureTex1 : register(t7);         // Subsurface / CoatColor
// t8 = GlintNoiseMap is declared inside Common/Glints/Glints2023.hlsli

SamplerState g_Sampler : register(s0);           // Linear wrap
SamplerState g_ClampSampler : register(s1);      // Linear clamp (for BRDF LUT)

// ─── Structures ────────────────────────────────────────────

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

struct PSInput
{
    float4 positionCS : SV_POSITION;
    float3 positionWS : TEXCOORD0;
    float3 normalWS : TEXCOORD1;
    float3 tangentWS : TEXCOORD2;
    float3 bitangentWS : TEXCOORD3;
    float2 uv : TEXCOORD4;
};

// ─── Vertex Shader ─────────────────────────────────────────

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.positionCS = mul(float4(input.position, 1.0), g_WorldViewProj);
    output.positionWS = mul(float4(input.position, 1.0), g_World).xyz;
    output.normalWS = normalize(mul(input.normal, (float3x3) g_WorldInvTranspose));
    output.tangentWS = normalize(mul(input.tangent.xyz, (float3x3) g_World));
    output.bitangentWS = cross(output.normalWS, output.tangentWS) * input.tangent.w;
    output.uv = input.uv;
    return output;
}

// ─── AgX Tone Mapping ──────────────────────────────────────

static const float3x3 AgXInsetMatrix = {
    0.842479062253094,  0.0423282422610123, 0.0423756549057051,
    0.0784335999999992, 0.878468636469772,  0.0784336,
    0.0792237451477643, 0.0791661274605434, 0.879142973793104,
};

static const float3x3 AgXOutsetMatrix = {
     1.19687900512017,  -0.0528968517574562, -0.0529716355144438,
    -0.0980208811401368, 1.15190312990417,   -0.0980434066481422,
    -0.0990297440797205,-0.0989611768448433,  1.15107367264116,
};

float3 agxDefaultContrastApprox(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return +15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4
           - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

float3 agxToneMap(float3 color)
{
    const float minEv = -12.47393f;
    const float maxEv = 4.026069f;
    color = mul(color, AgXInsetMatrix);
    color = clamp(log2(color), minEv, maxEv);
    color = (color - minEv) / (maxEv - minEv);
    color = agxDefaultContrastApprox(color);
    return color;
}

float3 agxEotf(float3 color)
{
    color = mul(color, AgXOutsetMatrix);
    color = pow(max(float3(0, 0, 0), color), 2.2);
    color = pow(max(float3(0, 0, 0), color), 1.0 / 2.2);
    return color;
}

// ─── Pixel Shader ──────────────────────────────────────────

float4 PSMain(PSInput input) : SV_TARGET
{
    // ── Sample textures ────────────────────────────────────
    float4 albedoSample = g_Diffuse.Sample(g_Sampler, input.uv);
    float3 albedo = albedoSample.rgb;
    float alpha = albedoSample.a;

    float3 normalTS = g_Normal.Sample(g_Sampler, input.uv).rgb;
    normalTS = normalTS * 2.0 - 1.0;

    float4 rmaos = g_RMAOS.Sample(g_Sampler, input.uv);
    float roughness = rmaos.r * g_RoughnessScale;
    float metallic = rmaos.g;
    float ao = rmaos.b;
    float specularMap = rmaos.a;

    roughness = clamp(roughness, PBR::Constants::MinRoughness, PBR::Constants::MaxRoughness);

    // ── Build TBN and transform normal ─────────────────────
    float3 N = normalize(input.normalWS);
    float3 T = normalize(input.tangentWS);
    float3 B = normalize(input.bitangentWS);
    float3x3 TBN = float3x3(T, B, N);
    N = normalize(mul(normalTS, TBN));

    float3 V = normalize(g_CameraPos - input.positionWS);
    float NdotV = max(dot(N, V), EPSILON_DOT_CLAMP);

    // ── Populate PBRMaterial ───────────────────────────────
    float3 F0 = lerp(float3(1, 1, 1) * g_SpecularLevel * specularMap, albedo, metallic);

    PBRMaterial mat = (PBRMaterial)0;
    mat.BaseColor = albedo;
    mat.Roughness = roughness;
    mat.F0 = F0;
    mat.AO = ao;
    mat.Metallic = metallic;

    // Subsurface
    [branch] if (g_FeatureFlags & PBR::Flags::Subsurface)
    {
        [branch] if (g_FeatureFlags & PBR::Flags::HasFeatureTexture1)
        {
            float4 ssTex = g_FeatureTex1.Sample(g_Sampler, input.uv);
            mat.SubsurfaceColor = ssTex.rgb * g_SubsurfaceColor;
            mat.Thickness = 1.0 - ssTex.a * g_SubsurfaceOpacity;
        }
        else
        {
            mat.SubsurfaceColor = g_SubsurfaceColor;
            mat.Thickness = 1.0 - g_SubsurfaceOpacity;
        }
    }

    // Two-Layer (Coat)
    [branch] if (g_FeatureFlags & PBR::Flags::TwoLayer)
    {
        mat.CoatF0 = float3(1, 1, 1) * g_CoatSpecularLevel;
        mat.CoatRoughness = clamp(g_CoatRoughness, PBR::Constants::MinRoughness, PBR::Constants::MaxRoughness);
        mat.CoatStrength = g_CoatStrength;
        mat.CoatColor = float3(1, 1, 1);

        [branch] if (g_FeatureFlags & PBR::Flags::HasFeatureTexture0)
        {
            float4 coatTex = g_FeatureTex0.Sample(g_Sampler, input.uv);
            mat.CoatRoughness = clamp(coatTex.a, PBR::Constants::MinRoughness, PBR::Constants::MaxRoughness);
        }

        [branch] if ((g_FeatureFlags & PBR::Flags::ColoredCoat) && (g_FeatureFlags & PBR::Flags::HasFeatureTexture1))
        {
            float4 coatColorTex = g_FeatureTex1.Sample(g_Sampler, input.uv);
            mat.CoatColor = coatColorTex.rgb;
            mat.CoatStrength *= coatColorTex.a;
        }
    }

    // Fuzz
    [branch] if (g_FeatureFlags & PBR::Flags::Fuzz)
    {
        mat.FuzzColor = g_FuzzColor;
        mat.FuzzWeight = g_FuzzWeight;

        [branch] if (g_FeatureFlags & PBR::Flags::HasFeatureTexture0)
        {
            float4 fuzzTex = g_FeatureTex0.Sample(g_Sampler, input.uv);
            mat.FuzzColor *= fuzzTex.rgb;
            mat.FuzzWeight *= fuzzTex.a;
        }
    }

    // Glint
    [branch] if (g_FeatureFlags & PBR::Flags::Glint)
    {
        mat.GlintLogMicrofacetDensity = clamp(PBR::Constants::MaxGlintDensity - g_GlintLogMicrofacetDensity,
            PBR::Constants::MinGlintDensity, PBR::Constants::MaxGlintDensity);
        mat.GlintMicrofacetRoughness = clamp(g_GlintMicrofacetRoughness,
            PBR::Constants::MinGlintRoughness, PBR::Constants::MaxGlintRoughness);
        mat.GlintDensityRandomization = clamp(g_GlintDensityRandomization,
            PBR::Constants::MinGlintDensityRandomization, PBR::Constants::MaxGlintDensityRandomization);
        mat.Tangent = T;

        float glintNoise = Random::R1Modified(float(g_FrameCount),
            (Random::pcg2d(uint2(input.positionCS.xy)) / 4294967296.0).x);
        mat.GlintNoise = glintNoise;

        float2 duvdx = ddx(input.uv);
        float2 duvdy = ddy(input.uv);
        Glints::PrecomputeGlints(glintNoise, input.uv, duvdx, duvdy,
            max(1, g_GlintScreenSpaceScale), mat.GlintCache);
    }

    // ── Direct lighting ────────────────────────────────────
    float3 L = normalize(g_LightDir);
    float3 radiance = g_LightColor * g_LightIntensity;

    PBR::DirectLightResult directResult = PBR::GetDirectLight(
        N, V, L, radiance, mat, g_FeatureFlags);

    float3 directDiffuse = directResult.diffuse;
    float3 directCoatDiffuse = 0;

    [branch] if ((g_FeatureFlags & PBR::Flags::TwoLayer) && (g_FeatureFlags & PBR::Flags::ColoredCoat))
    {
        directCoatDiffuse = directResult.coatDiffuse * mat.CoatColor * mat.CoatStrength;
    }

    float3 directColor = directDiffuse + directResult.specular + directResult.transmission + directCoatDiffuse;

    // ── Emissive ───────────────────────────────────────────
    float3 emissive = float3(0, 0, 0);
    [branch] if (g_FeatureFlags & PBR::Flags::HasEmissive)
    {
        emissive = g_Emissive.Sample(g_Sampler, input.uv).rgb * g_EmissiveScale;
    }

    // ── Image-Based Lighting ───────────────────────────────
    float3 iblColor = float3(0, 0, 0);

    if (g_IBLIntensity > 0)
    {
        float NdotV_ibl = saturate(dot(N, V));

        // Sample BRDF LUT for EnvBRDF (UE PreIntegratedGF)
        float2 envBRDF_AB = g_BRDFLut.SampleLevel(g_ClampSampler, float2(NdotV_ibl, roughness), 0).rg;

        PBR::IndirectLobeWeights lobes = PBR::GetIndirectLobeWeights(N, V, mat, g_FeatureFlags, envBRDF_AB);

        // Rotate directions into unrotated env space for IBL lookups
        float envCs = cos(g_EnvRotation);
        float envSn = sin(g_EnvRotation);
        float3 Nr = float3(envCs * N.x + envSn * N.z, N.y, -envSn * N.x + envCs * N.z);

        // Diffuse IBL: ZH3 evaluation
        const float3 lumCoeffs = float3(0.2126, 0.7152, 0.0722);
        float3 l1_lum = float3(
            dot(g_SH[3].xyz, lumCoeffs),
            dot(g_SH[1].xyz, lumCoeffs),
            dot(g_SH[2].xyz, lumCoeffs)
        );
        float l1Len = length(l1_lum);
        float3 zonalAxis = (l1Len > 1e-6) ? (l1_lum / l1Len) : float3(0, 1, 0);

        float3 irradiance = g_SH[0].xyz * 0.282095
                          + g_SH[1].xyz * (0.488603 * Nr.y)
                          + g_SH[2].xyz * (0.488603 * Nr.z)
                          + g_SH[3].xyz * (0.488603 * Nr.x);

        float fZ = dot(zonalAxis, Nr);
        float zhBasis = sqrt(5.0 / (16.0 * Math::PI)) * (3.0 * fZ * fZ - 1.0);
        irradiance += g_ZH3.xyz * zhBasis;
        irradiance = max(irradiance, float3(0, 0, 0));

        float3 iblDiffuse = lobes.diffuse * irradiance;

        // Diffuse AO
        if (g_RenderFlags & 2u)
            iblDiffuse *= MultiBounceAO(mat.BaseColor, ao);
        else
            iblDiffuse *= ao;

        // Specular IBL: use lobe weights from GetIndirectLobeWeights
        float3 R = reflect(-V, N);
        float3 Rr = float3(envCs * R.x + envSn * R.z, R.y, -envSn * R.x + envCs * R.z);
        // UE roughness-to-mip mapping (log2 heuristic)
        float specMip = g_MaxPrefilteredMip - 1.0 - (1.0 - 1.2 * log2(max(roughness, 0.001)));
        specMip = clamp(specMip, 0, g_MaxPrefilteredMip);
        float3 prefilteredColor = g_PrefilteredMap.SampleLevel(g_Sampler, Rr, specMip).rgb;
        float3 iblSpecular = prefilteredColor * lobes.specular;

        // Coat specular IBL (separate mip due to different roughness)
        [branch] if (g_FeatureFlags & PBR::Flags::TwoLayer)
        {
            float coatMip = g_MaxPrefilteredMip - 1.0 - (1.0 - 1.2 * log2(max(mat.CoatRoughness, 0.001)));
            coatMip = clamp(coatMip, 0, g_MaxPrefilteredMip);
            float3 coatPref = g_PrefilteredMap.SampleLevel(g_Sampler, Rr, coatMip).rgb;
            iblSpecular += coatPref * lobes.coatSpecular;
        }

        // Horizon occlusion
        if (g_RenderFlags & 1u)
        {
            float3 N_geo = normalize(input.normalWS);
            float horizon = saturate(1.0 + dot(R, N_geo));
            iblSpecular *= horizon * horizon;
        }

        // Specular occlusion from AO map
        if (g_RenderFlags & 4u)
        {
            float a2 = roughness * roughness;
            float specOcc = SpecularOcclusion(NdotV, a2, ao);
            iblSpecular *= specOcc;
        }

        iblColor = (iblDiffuse + iblSpecular) * g_IBLIntensity;
    }

    // ── Final color ────────────────────────────────────────
    float3 color = directColor + iblColor + emissive;

    // AgX tone mapping
    color = max(float3(0, 0, 0), color);
    color = agxToneMap(color);
    color = agxEotf(color);

    return float4(saturate(color), alpha);
}
