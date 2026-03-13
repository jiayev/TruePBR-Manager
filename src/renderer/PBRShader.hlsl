// PBR Preview Shader — Cook-Torrance BRDF + IBL + single directional light
// DX normal convention (Y+ down in tangent space)

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
    float _pad2;
    float _pad3;
    float4 g_SH[4];  // Pre-convolved SH2 radiance: [0]=DC, [1]=L1y, [2]=L1z, [3]=L1x
    float4 g_ZH3;    // Pre-convolved ZH3 zonal L2 coefficient (.xyz = RGB)
    row_major float4x4 g_InvViewProj; // Inverse view-projection (used by skybox)
};

cbuffer MaterialCB : register(b1)
{
    float g_SpecularLevel;
    float g_RoughnessScale;
    float _matPad0;
    float _matPad1;
};

// ─── Textures & Samplers ───────────────────────────────────

Texture2D g_Diffuse : register(t0);             // sRGB base color RGB + opacity A
Texture2D g_Normal : register(t1);              // Normal map (DX convention)
Texture2D g_RMAOS : register(t2);               // R=Roughness G=Metallic B=AO A=Specular
TextureCube g_PrefilteredMap : register(t3);     // Specular IBL (prefiltered cubemap with mips)
Texture2D g_BRDFLut : register(t4);             // BRDF integration LUT

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

// ─── PBR Functions ─────────────────────────────────────────

static const float PI = 3.14159265359;

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + 0.0001);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 oneMinusR = float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness);
    return F0 + (max(oneMinusR, F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
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
    // Sample textures
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

    roughness = clamp(roughness, 0.04, 1.0);

    // Build TBN and transform normal
    float3 N = normalize(input.normalWS);
    float3 T = normalize(input.tangentWS);
    float3 B = normalize(input.bitangentWS);
    float3x3 TBN = float3x3(T, B, N);
    N = normalize(mul(normalTS, TBN));

    float3 V = normalize(g_CameraPos - input.positionWS);
    float NdotV = max(dot(N, V), 0.0001);

    // F0
    float3 F0 = lerp(float3(1, 1, 1) * g_SpecularLevel * specularMap, albedo, metallic);

    // ── Direct lighting (Cook-Torrance) ────────────────────
    float3 L = normalize(g_LightDir);
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(HdotV, F0);

    float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * albedo / PI;

    float3 radiance = g_LightColor * g_LightIntensity;
    float3 directColor = (diffuse + specular) * radiance * NdotL;

    // ── Image-Based Lighting ───────────────────────────────
    float3 iblColor = float3(0, 0, 0);

    if (g_IBLIntensity > 0)
    {
        float3 F_ibl = FresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kD_ibl = (1.0 - F_ibl) * (1.0 - metallic);

        // Diffuse IBL: ZH3 evaluation (Roughton et al. 2024)
        // Luminance zonal axis from pre-convolved SH2 (A_1 cancels in normalize)
        const float3 lumCoeffs = float3(0.2126, 0.7152, 0.0722);
        float3 l1_lum = float3(
            dot(g_SH[3].xyz, lumCoeffs),  // x
            dot(g_SH[1].xyz, lumCoeffs),  // y
            dot(g_SH[2].xyz, lumCoeffs)   // z
        );
        float l1Len = length(l1_lum);
        float3 zonalAxis = (l1Len > 1e-6) ? (l1_lum / l1Len) : float3(0, 1, 0);

        // Linear SH irradiance (pre-convolved coefficients × basis functions)
        float3 irradiance = g_SH[0].xyz * 0.282095
                          + g_SH[1].xyz * (0.488603 * N.y)
                          + g_SH[2].xyz * (0.488603 * N.z)
                          + g_SH[3].xyz * (0.488603 * N.x);

        // ZH3 quadratic zonal term
        float fZ = dot(zonalAxis, N);
        float zhBasis = sqrt(5.0 / (16.0 * PI)) * (3.0 * fZ * fZ - 1.0);
        irradiance += g_ZH3.xyz * zhBasis;
        irradiance = max(irradiance, float3(0, 0, 0));
        float3 iblDiffuse = kD_ibl * albedo * irradiance;

        // Specular IBL: sample prefiltered map + BRDF LUT
        float3 R = reflect(-V, N);
        float mipLevel = roughness * g_MaxPrefilteredMip;
        float3 prefilteredColor = g_PrefilteredMap.SampleLevel(g_Sampler, R, mipLevel).rgb;
        float2 brdfSample = g_BRDFLut.Sample(g_ClampSampler, float2(NdotV, roughness)).rg;
        float3 iblSpecular = prefilteredColor * (F_ibl * brdfSample.x + brdfSample.y);

        iblColor = (iblDiffuse + iblSpecular) * g_IBLIntensity;
    }
    else
    {
        // Fallback ambient when no IBL
        iblColor = albedo * 0.03;
    }

    // Final color
    float3 color = directColor + iblColor;
    color *= ao;

    // AgX tone mapping
    color = max(float3(0, 0, 0), color);
    color = agxToneMap(color);
    color = agxEotf(color);

    return float4(saturate(color), alpha);
}
