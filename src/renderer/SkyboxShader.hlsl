// Skybox shader — renders HDRI environment as background
// Full-screen triangle from SV_VertexID, samples full-resolution cubemap

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
    uint g_FrameCount;
    float g_EnvRotation; // HDRI Y-axis rotation (radians)
    float4 g_SH[4];
    float4 g_ZH3;
    row_major float4x4 g_InvViewProj;
    uint   g_HDREnabled;
    float  g_PaperWhiteNits;
    float  g_PeakBrightnessNits;
    float  _padHDR;
};

// ─── Textures & Samplers ───────────────────────────────────

TextureCube g_SkyboxMap : register(t9);
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

// ─── GT7 Tone Mapping ─────────────────────────────────────

#include "Common/GT7ToneMap.hlsli"

// ─── Pixel Shader ──────────────────────────────────────────

float4 SkyboxPS(SkyboxOutput input) : SV_TARGET
{
    float3 dir = normalize(input.viewDir);

    // Rotate direction around Y axis by g_EnvRotation
    float cs = cos(g_EnvRotation);
    float sn = sin(g_EnvRotation);
    dir = float3(cs * dir.x + sn * dir.z, dir.y, -sn * dir.x + cs * dir.z);

    float3 color = g_SkyboxMap.SampleLevel(g_Sampler, dir, 0).rgb;
    color *= g_IBLIntensity;

    // Tone mapping & output
    color = max(float3(0, 0, 0), color);
    [branch] if (g_HDREnabled)
    {
        color = GT7ToneMap(color, true, g_PaperWhiteNits, g_PeakBrightnessNits);
        return float4(color, 1.0);
    }
    else
    {
        color = GT7ToneMap(color, false, 0, 0);
        color = pow(max(color, 0.0), 1.0 / 2.2);
        return float4(saturate(color), 1.0);
    }
}
