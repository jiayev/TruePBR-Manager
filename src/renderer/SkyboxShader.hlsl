// Skybox shader — renders HDRI environment as background
// Full-screen triangle from SV_VertexID, samples prefiltered cubemap at mip 0

// ─── Constant Buffer (must match SceneCB layout in PBRShader.hlsl) ─────

cbuffer SceneCB : register(b0)
{
    row_major float4x4 g_WorldViewProj;
    row_major float4x4 g_World;
    row_major float4x4 g_WorldInvTranspose;
    float3 g_CameraPos;
    float _pad0;
    float3 g_LightDir;
    float _pad1;
    float3 g_LightColor;
    float g_LightIntensity;
    float g_IBLIntensity;
    float g_MaxPrefilteredMip;
    float _pad2;
    float _pad3;
    float4 g_SH[4];
    float4 g_ZH3;
    row_major float4x4 g_InvViewProj;
};

// ─── Textures & Samplers ───────────────────────────────────

TextureCube g_PrefilteredMap : register(t3);
SamplerState g_Sampler : register(s0);

// ─── Structures ────────────────────────────────────────────

struct SkyboxOutput
{
    float4 positionCS : SV_POSITION;
    float3 viewDir : TEXCOORD0;
};

// ─── Vertex Shader ─────────────────────────────────────────

SkyboxOutput SkyboxVS(uint vertexID : SV_VertexID)
{
    // Full-screen triangle: vertices at (-1,-1), (3,-1), (-1,3)
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    float4 posNDC = float4(uv * 2.0 - 1.0, 1.0, 1.0);

    // Unproject far-plane corners to world space
    float4 worldPos = mul(posNDC, g_InvViewProj);
    float3 worldPt = worldPos.xyz / worldPos.w;

    SkyboxOutput output;
    output.positionCS = posNDC;
    output.viewDir = worldPt - g_CameraPos;
    return output;
}

// ─── AgX Tone Mapping (matches PBRShader.hlsl) ─────────────

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

float4 SkyboxPS(SkyboxOutput input) : SV_TARGET
{
    float3 dir = normalize(input.viewDir);
    float3 color = g_PrefilteredMap.SampleLevel(g_Sampler, dir, 0).rgb;
    color *= g_IBLIntensity;

    // AgX tone mapping (same as PBR shader)
    color = max(float3(0, 0, 0), color);
    color = agxToneMap(color);
    color = agxEotf(color);

    return float4(saturate(color), 1.0);
}
