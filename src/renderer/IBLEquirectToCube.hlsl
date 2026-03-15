// IBLEquirectToCube.hlsl — Convert equirectangular HDR map to cubemap faces
// Dispatch: one thread per output texel, 6 faces via array slice
// Input is already in ACEScg (CPU-side color space conversion in IBLPipeline).

cbuffer EquirectToCubeCB : register(b0)
{
    uint g_FaceIndex;   // 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
    uint g_FaceSize;    // Output face resolution (e.g. 512)
    uint2 _pad;
};

Texture2D<float4> g_EquirectMap : register(t0);
RWTexture2DArray<float4> g_OutputCubemap : register(u0);
SamplerState g_LinearSampler : register(s0);

static const float PI = 3.14159265359;

// Get direction vector for a cubemap texel — D3D standard convention.
// Must match hardware TextureCube SRV face mapping so UAV writes and SRV reads are consistent.
float3 cubemapDirection(uint face, float u, float v)
{
    // u,v in [-1, 1]
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

// Convert direction to equirectangular UV
float2 dirToEquirectUV(float3 dir)
{
    float theta = atan2(dir.z, dir.x);       // [-PI, PI]
    float phi = asin(clamp(dir.y, -1.0, 1.0)); // [-PI/2, PI/2]
    float u = (theta / PI + 1.0) * 0.5;     // [0, 1]
    float v = 1.0 - (phi / PI + 0.5);       // [0, 1], flipped
    return float2(u, v);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchID : SV_DispatchThreadID)
{
    if (dispatchID.x >= g_FaceSize || dispatchID.y >= g_FaceSize)
        return;

    float u = (float(dispatchID.x) + 0.5) / float(g_FaceSize) * 2.0 - 1.0;
    float v = (float(dispatchID.y) + 0.5) / float(g_FaceSize) * 2.0 - 1.0;

    float3 dir = cubemapDirection(g_FaceIndex, u, v);
    float2 eqUV = dirToEquirectUV(dir);

    float4 color = g_EquirectMap.SampleLevel(g_LinearSampler, eqUV, 0);

    g_OutputCubemap[uint3(dispatchID.x, dispatchID.y, g_FaceIndex)] = color;
}
