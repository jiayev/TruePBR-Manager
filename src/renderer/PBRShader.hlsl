// PBR Preview Shader — Cook-Torrance BRDF with single directional light
// DX normal convention (Y+ down in tangent space)

// ─── Constant Buffers ──────────────────────────────────────

cbuffer SceneCB : register(b0)
{
    float4x4 g_WorldViewProj;
    float4x4 g_World;
    float4x4 g_WorldInvTranspose;
    float3 g_CameraPos;
    float _pad0;
    float3 g_LightDir;      // normalized, towards light
    float _pad1;
    float3 g_LightColor;
    float g_LightIntensity;
};

cbuffer MaterialCB : register(b1)
{
    float g_SpecularLevel;
    float g_RoughnessScale;
    float _matPad0;
    float _matPad1;
};

// ─── Textures & Samplers ───────────────────────────────────

Texture2D g_Diffuse  : register(t0);   // sRGB base color RGB + opacity A
Texture2D g_Normal   : register(t1);   // Normal map (DX convention)
Texture2D g_RMAOS    : register(t2);   // R=Roughness G=Metallic B=AO A=Specular

SamplerState g_Sampler : register(s0);

// ─── Structures ────────────────────────────────────────────

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 tangent  : TANGENT;  // xyz = tangent, w = handedness sign
    float2 uv       : TEXCOORD0;
};

struct PSInput
{
    float4 positionCS : SV_POSITION;
    float3 positionWS : TEXCOORD0;
    float3 normalWS   : TEXCOORD1;
    float3 tangentWS  : TEXCOORD2;
    float3 bitangentWS: TEXCOORD3;
    float2 uv         : TEXCOORD4;
};

// ─── Vertex Shader ─────────────────────────────────────────

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.positionCS  = mul(float4(input.position, 1.0), g_WorldViewProj);
    output.positionWS  = mul(float4(input.position, 1.0), g_World).xyz;
    output.normalWS    = normalize(mul(float4(input.normal, 0.0), g_WorldInvTranspose).xyz);
    output.tangentWS   = normalize(mul(float4(input.tangent.xyz, 0.0), g_World).xyz);
    output.bitangentWS = cross(output.normalWS, output.tangentWS) * input.tangent.w;
    output.uv          = input.uv;
    return output;
}

// ─── PBR Functions ─────────────────────────────────────────

static const float PI = 3.14159265359;

// GGX/Trowbridge-Reitz normal distribution
float DistributionGGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + 0.0001);
}

// Schlick-GGX geometry function
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + 0.0001);
}

// Smith's method combining two Schlick-GGX terms
float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick approximation
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// ─── Pixel Shader ──────────────────────────────────────────

float4 PSMain(PSInput input) : SV_TARGET
{
    // Sample textures
    float4 albedoSample = g_Diffuse.Sample(g_Sampler, input.uv);
    float3 albedo       = albedoSample.rgb;
    float  alpha        = albedoSample.a;

    float3 normalTS     = g_Normal.Sample(g_Sampler, input.uv).rgb;
    normalTS            = normalTS * 2.0 - 1.0;
    // DX normal convention: Y is already correct (no flip needed)

    float4 rmaos        = g_RMAOS.Sample(g_Sampler, input.uv);
    float  roughness    = rmaos.r * g_RoughnessScale;
    float  metallic     = rmaos.g;
    float  ao           = rmaos.b;
    float  specularMap  = rmaos.a;

    // Clamp roughness to avoid division by zero
    roughness = clamp(roughness, 0.04, 1.0);

    // Build TBN matrix and transform normal to world space
    float3 N = normalize(input.normalWS);
    float3 T = normalize(input.tangentWS);
    float3 B = normalize(input.bitangentWS);
    float3x3 TBN = float3x3(T, B, N);
    N = normalize(mul(normalTS, TBN));

    // View and light vectors
    float3 V = normalize(g_CameraPos - input.positionWS);
    float3 L = normalize(g_LightDir);
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // F0: reflectance at normal incidence
    // For dielectrics: specularLevel * specularMap (from RMAOS alpha)
    // For metals: albedo color
    float3 F0 = lerp(float3(1, 1, 1) * g_SpecularLevel * specularMap, albedo, metallic);

    // Cook-Torrance BRDF
    float  D = DistributionGGX(NdotH, roughness);
    float  G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(HdotV, F0);

    float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    // Diffuse: only for non-metallic parts
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * albedo / PI;

    // Final color
    float3 radiance = g_LightColor * g_LightIntensity;
    float3 color = (diffuse + specular) * radiance * NdotL;

    // Apply AO
    color *= ao;

    // Simple ambient (very low, just to prevent pure black shadows)
    color += albedo * 0.03 * ao;

    // Tone mapping (Reinhard)
    color = color / (color + 1.0);

    // Gamma correction (linear -> sRGB)
    color = pow(color, 1.0 / 2.2);

    return float4(color, alpha);
}
