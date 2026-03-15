// Tone Map Post-Process Shader — fullscreen triangle that applies GT7 tone mapping
// to the resolved HDR color buffer and outputs to the swap chain.

// ─── Constant Buffer (subset of SceneCB — only HDR params needed) ──────

cbuffer ToneMapCB : register(b0)
{
    uint   g_HDREnabled;
    float  g_PaperWhiteNits;
    float  g_PeakBrightnessNits;
    float  _pad0;
};

// ─── Textures & Samplers ───────────────────────────────────

Texture2D g_HDRColor : register(t0);        // Resolved HDR scene color
SamplerState g_PointSampler : register(s0);  // Point sampler (1:1 texel fetch)

// ─── Structures ────────────────────────────────────────────

struct VSOutput
{
    float4 positionCS : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// ─── Vertex Shader ─────────────────────────────────────────

VSOutput ToneMapVS(uint vertexID : SV_VertexID)
{
    // Full-screen triangle: vertices at (-1,-1), (3,-1), (-1,3)
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);

    VSOutput output;
    output.positionCS = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv = float2(uv.x, 1.0 - uv.y); // Flip Y for texture sampling
    return output;
}

// ─── GT7 Tone Mapping ─────────────────────────────────────

#include "Common/GT7ToneMap.hlsli"

// ─── Pixel Shader ──────────────────────────────────────────

float4 ToneMapPS(VSOutput input) : SV_TARGET
{
    float4 hdr = g_HDRColor.Sample(g_PointSampler, input.uv);
    float3 color = max(hdr.rgb, float3(0, 0, 0));

    [branch] if (g_HDREnabled)
    {
        color = GT7ToneMap(color, true, g_PaperWhiteNits, g_PeakBrightnessNits);
        return float4(color, hdr.a);
    }
    else
    {
        color = GT7ToneMap(color, false, 0, 0);
        color = pow(max(color, 0.0), 1.0 / 2.2);
        return float4(saturate(color), hdr.a);
    }
}
