// IBLCubemapMipGen.hlsl — Cubemap mip chain generation with wide 9-tap filter
// Uses TextureCube SampleLevel for hardware filtering and seamless cubemap edge handling.
// Dispatch: (dstSize+7)/8, (dstSize+7)/8, 6

cbuffer MipGenCB : register(b0)
{
    uint g_DstSize;       // Destination mip face resolution
    uint g_NumMips;       // Total mip levels of the cubemap
    uint g_DstMipIndex;   // Destination mip level index
    uint _pad;
};

TextureCube<float4> g_SrcCubemap : register(t0);
RWTexture2DArray<float4> g_DstMip : register(u0);
SamplerState g_LinearSampler : register(s0);

float3 getCubeFaceDir(uint face, float u, float v)
{
    // u,v in [-1,1] — D3D standard convention.
    // Must match hardware TextureCube SRV face mapping.
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
    return dir;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_DstSize || id.y >= g_DstSize)
        return;

    uint face = id.z;
    float2 ScaledUVs = ((float2(id.xy) + 0.5) / float(g_DstSize)) * 2.0 - 1.0;

    float3 CubeCoordinates = getCubeFaceDir(face, ScaledUVs.x, ScaledUVs.y);

    // Build tangent frame for offset sampling
    float3 TangentZ = normalize(CubeCoordinates);
    float3 neighborDir = getCubeFaceDir(face, ScaledUVs.x, ScaledUVs.y + 1.0);
    float3 TangentX = normalize(cross(neighborDir, TangentZ));
    float3 TangentY = cross(TangentZ, TangentX);

    // Offset scale: 2 texels wide in the face UV space at destination resolution
    float SampleOffset = 2.0 * 2.0 / float(g_DstSize);

    // 8 surrounding offsets: 4 diagonals at 0.7 distance + 4 axis at 1.0
    float2 Offsets[8] =
    {
        float2(-1, -1) * 0.7,
        float2( 1, -1) * 0.7,
        float2(-1,  1) * 0.7,
        float2( 1,  1) * 0.7,
        float2( 0, -1),
        float2(-1,  0),
        float2( 1,  0),
        float2( 0,  1),
    };

    // Center sample (weight 1.0) — SRV MostDetailedMip = srcMip, so SampleLevel(0) reads it
    float4 OutColor = g_SrcCubemap.SampleLevel(g_LinearSampler, CubeCoordinates, 0);

    [unroll]
    for (uint i = 0; i < 8; i++)
    {
        float3 SampleDir = CubeCoordinates;
        SampleDir += TangentX * (Offsets[i].x * SampleOffset);
        SampleDir += TangentY * (Offsets[i].y * SampleOffset);
        OutColor += g_SrcCubemap.SampleLevel(g_LinearSampler, SampleDir, 0) * 0.375;
    }

    // Total weight = 1.0 + 8*0.375 = 4.0
    OutColor *= 0.25;

    g_DstMip[uint3(id.xy, face)] = OutColor;
}
