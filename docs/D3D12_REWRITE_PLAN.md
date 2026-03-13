# D3D12 Renderer Rewrite Plan

## Status: IN PROGRESS — Core infrastructure implemented

## Problem

Current D3D12Renderer has fundamental issues:
- Fence management is fragile (timeout/hang on RTX 5070 Blackwell)
- Single command allocator shared across uploads and rendering
- No proper frame-in-flight management
- IBL processing done on CPU (slow, blocks UI)
- Texture uploads block on fence per-texture (serial, wasteful)

## New Architecture

### Core Infrastructure

```
D3D12Renderer
├── Device + DXGI Factory
├── Direct CommandQueue (rendering + compute)
├── Copy CommandQueue (async texture upload)
├── Fence per queue (monotonically increasing values)
│
├── FrameContext[2]  (double-buffered)
│   ├── CommandAllocator (direct)
│   ├── fence value for this frame
│   └── per-frame dynamic CB data
│
├── Upload Ring Buffer
│   ├── Single large UPLOAD heap (~64 MB)
│   ├── Ring-buffer offset tracking
│   └── Used by copy queue for texture uploads
│
└── DescriptorHeap Management
    ├── Shader-visible SRV/CBV/UAV heap (single, large)
    ├── Non-visible RTV heap
    ├── Non-visible DSV heap
    └── Staging CPU-only heap for building descriptors
```

### Fence Strategy (Standard Double-Buffer)

```
Frame N:
  1. Wait for frame N-2's fence (FrameContext[(N)%2].fenceValue)
  2. Reset FrameContext[N%2].allocator
  3. Record commands
  4. Execute
  5. Signal fence with current value → store in FrameContext[N%2].fenceValue
  6. Present
```

No per-upload fence waits. Uploads use a separate copy queue:
```
Upload:
  1. Copy data to ring buffer
  2. Record CopyTextureRegion on copy queue
  3. Execute copy queue
  4. Direct queue waits on copy queue's fence before using the resource
```

### IBL Pipeline (GPU Compute Shaders)

All IBL processing moves to GPU compute passes:

| Pass | Input | Output | Shader |
|------|-------|--------|--------|
| Equirect → Cubemap | Equirect 2D (HDRI) | Cubemap 6-face (512³) | `IBLEquirectToCube.hlsl` |
| Irradiance | Cubemap | Irradiance Cubemap (64³) | `IBLIrradiance.hlsl` |
| Prefilter Specular | Cubemap | Prefiltered Cubemap (256³, N mips) | `IBLPrefilter.hlsl` |
| BRDF LUT | — | 2D LUT (256²) | `IBLBrdfLut.hlsl` |

Each pass:
1. Set compute root signature + PSO
2. Bind input SRV + output UAV
3. Dispatch thread groups
4. Resource barrier (UAV → SRV for next pass)

### File Structure

```
src/renderer/
├── D3D12Renderer.h/.cpp        — Rewritten from scratch
├── D3D12UploadQueue.h/.cpp     — Copy queue + ring buffer
├── DescriptorHeap.h/.cpp       — Descriptor allocation helper
├── IBLPipeline.h/.cpp          — GPU IBL compute orchestration
├── MeshGenerator.h/.cpp        — Keep as-is
├── PBRShader.hlsl              — Keep (minor CB layout update)
├── IBLEquirectToCube.hlsl      — NEW compute shader
├── IBLIrradiance.hlsl          — NEW compute shader
├── IBLPrefilter.hlsl           — NEW compute shader
├── IBLBrdfLut.hlsl             — NEW compute shader
│
├── IBLProcessor.h/.cpp         — Simplified: HDRI file loading only
│                                  (GPU processing moved to IBLPipeline)
└── third_party/
    └── tinyexr.h + impl        — Keep
```

### Constant Buffer Layout

```hlsl
cbuffer SceneCB : register(b0)  // 256-byte aligned
{
    float4x4 WorldViewProj;     // 64 bytes
    float4x4 World;             // 64 bytes
    float4x4 WorldInvTranspose; // 64 bytes
    float3   CameraPos;         // 12 bytes
    float    _pad0;             // 4 bytes
    float3   LightDir;          // 12 bytes
    float    _pad1;             // 4 bytes
    float3   LightColor;        // 12 bytes
    float    LightIntensity;    // 4 bytes
    float    IBLIntensity;      // 4 bytes
    float    MaxPrefilteredMip; // 4 bytes
    float2   _pad2;             // 8 bytes
};                              // Total: 256 bytes

cbuffer MaterialCB : register(b1)
{
    float SpecularLevel;
    float RoughnessScale;
    float2 _pad;
};
```

### SRV Layout

| Slot | Type | Content |
|------|------|---------|
| t0 | Texture2D | Diffuse (sRGB) |
| t1 | Texture2D | Normal (linear) |
| t2 | Texture2D | RMAOS (linear) |
| t3 | TextureCube | Irradiance (HDR float) |
| t4 | TextureCube | Prefiltered Specular (HDR float, mipped) |
| t5 | Texture2D | BRDF LUT (RG float) |

### Root Signature

```
Param 0: CBV b0 (SceneCB) — inline root CBV
Param 1: CBV b1 (MaterialCB) — inline root CBV
Param 2: Descriptor table [t0..t5] — 6 SRVs
Static Sampler s0: Linear Wrap
Static Sampler s1: Linear Clamp
```

### Migration Checklist

- [x] Delete current D3D12Renderer.cpp (keep .h for reference)
- [x] Delete IBLProcessor.cpp compute logic (keep HDRI loading)
- [x] Write D3D12UploadQueue
- [x] Write DescriptorHeap helper
- [x] Write new D3D12Renderer with double-buffered frames
- [x] Write 4 IBL compute shaders
- [x] Write IBLPipeline orchestrator
- [x] Wire up GPU IBL compute passes (currently CPU fallback)
- [ ] Update MaterialPreviewWidget if API changed
- [ ] Update MainWindow connections if API changed
- [ ] Test on RTX 5070
- [ ] Remove debug logging clutter
- [ ] Update SPEC + README
