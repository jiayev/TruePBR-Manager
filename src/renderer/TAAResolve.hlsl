// TAA Resolve Compute Shader — Temporal Anti-Aliasing with neighborhood clamping
//
// Input:  Current jittered HDR color, velocity buffer, previous TAA history
// Output: Resolved HDR color written to current history buffer
//
// Algorithm:
//   1. Reproject current pixel to previous frame using velocity
//   2. Sample previous history at reprojected location
//   3. Clamp history to current frame's 3×3 color neighborhood (variance clipping)
//   4. Blend: result = lerp(clampedHistory, currentColor, blendFactor)

cbuffer TAACBuffer : register(b0)
{
    uint2  g_Resolution;    // Render target dimensions
    float  g_BlendFactor;   // Current frame weight (0.05–0.2, default 0.1)
    uint   g_FirstFrame;    // 1 = no valid history, copy current directly
};

Texture2D<float4> g_CurrentColor  : register(t0); // Jittered scene HDR color
Texture2D<float2> g_Velocity      : register(t1); // Screen-space motion vectors
Texture2D<float4> g_HistoryColor  : register(t2); // Previous frame's resolved color

RWTexture2D<float4> g_Output      : register(u0); // Current frame's resolved output

SamplerState g_LinearClamp : register(s0);

// ─── Neighborhood clipping (variance clip, Salvi 2016) ────

float3 ClipAABB(float3 aabb_min, float3 aabb_max, float3 history)
{
    float3 center = 0.5 * (aabb_max + aabb_min);
    float3 extents = 0.5 * (aabb_max - aabb_min) + 0.001;

    float3 offset = history - center;
    float3 ts = abs(offset / extents);
    float t = max(max(ts.x, ts.y), ts.z);

    if (t > 1.0)
        return center + offset / t;
    return history;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchID : SV_DispatchThreadID)
{
    if (dispatchID.x >= g_Resolution.x || dispatchID.y >= g_Resolution.y)
        return;

    int2 pixelCoord = int2(dispatchID.xy);
    float2 uv = (float2(pixelCoord) + 0.5) / float2(g_Resolution);

    // Current color (center sample)
    float4 currentColor = g_CurrentColor.Load(int3(pixelCoord, 0));

    // If first frame, just copy
    if (g_FirstFrame)
    {
        g_Output[pixelCoord] = currentColor;
        return;
    }

    // Read velocity and compute reprojected UV
    float2 velocity = g_Velocity.Load(int3(pixelCoord, 0));
    float2 historyUV = uv - velocity;

    // Reject if reprojected UV is out of bounds
    bool outOfBounds = any(historyUV < 0.0) || any(historyUV > 1.0);

    // Sample history (bilinear)
    float3 historyColor = g_HistoryColor.SampleLevel(g_LinearClamp, historyUV, 0).rgb;

    // Neighborhood statistics (3×3 cross pattern for performance)
    float3 m1 = float3(0, 0, 0);
    float3 m2 = float3(0, 0, 0);

    static const int2 offsets[9] = {
        int2(-1, -1), int2(0, -1), int2(1, -1),
        int2(-1,  0), int2(0,  0), int2(1,  0),
        int2(-1,  1), int2(0,  1), int2(1,  1)
    };

    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        float3 c = g_CurrentColor.Load(int3(pixelCoord + offsets[i], 0)).rgb;
        m1 += c;
        m2 += c * c;
    }

    m1 /= 9.0;
    m2 /= 9.0;
    float3 sigma = sqrt(max(m2 - m1 * m1, 0.0));

    // Variance clipping: clamp history to [mean - gamma*sigma, mean + gamma*sigma]
    const float gamma = 1.25;
    float3 aabb_min = m1 - gamma * sigma;
    float3 aabb_max = m1 + gamma * sigma;
    float3 clampedHistory = ClipAABB(aabb_min, aabb_max, historyColor);

    // Adaptive blend factor: increase current weight for disoccluded pixels
    float blendFactor = g_BlendFactor;
    if (outOfBounds)
        blendFactor = 1.0; // No valid history

    // Blend
    float3 resolved = lerp(clampedHistory, currentColor.rgb, blendFactor);

    g_Output[pixelCoord] = float4(resolved, currentColor.a);
}
