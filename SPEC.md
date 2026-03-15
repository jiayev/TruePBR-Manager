# TruePBR Manager - Current Specification

This document describes the repository as it exists now. It is implementation-oriented, not a forward-looking design draft.

## 1. Overview

| Item | Value |
|------|-------|
| Project Name | TruePBR Manager |
| Type | Desktop GUI application |
| Language | C++20 |
| Build System | CMake 3.21+ |
| GUI Framework | Qt 6 Widgets |
| Package Manager | vcpkg |
| Target Platform | Windows x64 |
| License | CC BY-NC 4.0 |

## 2. Purpose

TruePBR Manager is a desktop authoring tool for Skyrim modding workflows built around Community Shaders True PBR and PGPatcher.

The current implementation is designed to:

1. Manage multiple PBR texture sets in a single project.
2. Map each set to one vanilla diffuse path.
3. Import required and optional textures into True PBR slots.
4. Support RMAOS authoring either as a pre-packed texture or as split Roughness, Metallic, AO, and Specular sources.
5. Edit feature flags and material parameters.
6. Export textures as DDS into a mod folder and generate a PGPatcher JSON file.

Primary references:

- True PBR spec: https://github.com/doodlum/skyrim-community-shaders/wiki/True-PBR
- PGPatcher Mod Authors Guide: https://github.com/hakasapl/PGPatcher/wiki/Mod-Authors

## 3. Implemented Feature Surface

### 3.1 Project lifecycle

- New project in memory
- Save project to `.tpbr`
- Load project from `.tpbr`
- Add and remove texture sets

### 3.2 Texture authoring

- Import slot textures from DDS and raster formats (PNG, TGA, BMP, JPG/JPEG)
- Import split RMAOS channels independently
- Drag-and-drop import onto slot and channel controls
- Separate Import and Clear buttons per slot and per channel row
- Click a slot's DropZone to preview that texture (does not open import dialog)
- Persist imported file metadata: source path, dimensions, channel count, format

### 3.3 Material authoring

- Edit feature flags in the UI
- Edit base, emissive, parallax, subsurface, coat, fuzz, and glint parameters in the UI
- Store per-slot export compression overrides
- Store RMAOS source mode per texture set

### 3.4 2D preview

- Image preview with zoom, pan, and per-channel isolation (R/G/B/A)
- Click any slot's texture to preview it; channel buttons appear for multi-channel textures
- Default display: diffuse → normal → empty

### 3.5 3D material preview

- D3D12-based real-time PBR renderer with Cook-Torrance BRDF
- Loads Diffuse, Normal, RMAOS, and feature textures (Emissive, Coat/Fuzz, Subsurface/CoatColor) from the current texture set
- RMAOS composed from split channels in SeparateChannels mode
- Four mesh shapes: Sphere, Plane (double-sided), Cube, Rounded Cube
- Orbit camera (left-drag to rotate, scroll to zoom)
- Directional light rotation (right-drag)
- HDRI environment rotation (middle-drag)
- Image-Based Lighting (IBL) with GPU compute pipeline:
  - HDRI loading from EXR, HDR, and DDS files
  - HDRI color space detection (Rec709, ACEScg, ACES2065-1, Rec2020) with automatic conversion to ACEScg
  - 4 GPU compute passes: equirect-to-cubemap, ZH3 diffuse irradiance, GGX prefiltered specular, BRDF LUT
  - Configurable prefilter resolution and sample count
- Skybox rendering from loaded HDRI
- GT7 tone mapping with exposure compensation (EV)
- All shader computation in ACEScg working color space
- Temporal Anti-Aliasing (TAA) with per-frame jitter and velocity reprojection
- HDR output support (scRGB) with paper-white and peak-brightness controls
- VSync toggle (DXGI_PRESENT_ALLOW_TEARING when disabled)
- Render flags: Horizon Occlusion, Multi-Bounce AO, Specular Occlusion
- Full PBR feature support in preview: emissive, subsurface, coat, fuzz, glint, hair
- Automatic GPU selection (discrete adapter preferred)
- Device-lost detection and recovery

### 3.6 Export

- DDS export for each assigned slot
- Split-channel packing into `_rmaos.dds` during export
- Per-slot export size override (power-of-two downscale or original)
- Per-slot path override for custom PGPatcher `slotN` paths
- PGPatcher JSON export to `PBRNIFPatcher/<project>.json`
- Automatic export directory creation under `textures/pbr/...`
- Pre-export validation with error/warning reporting

### 3.7 Batch import

- Scan a folder and auto-assign textures to slots by suffix convention
- Recognized slot suffixes: `_n`, `_g`, `_p`, `_rmaos`, `_cnr`, `_f`, `_s`
- Recognized channel suffixes: `_roughness`, `_metallic`, `_ao`, `_specular` (and abbreviations)
- Files with no recognized suffix are assigned as Diffuse
- If channel maps are found, RMAOS source mode is automatically set to SeparateChannels

### 3.8 Input validation

- Pre-export validation checks per texture set:
  - Missing required slots (Diffuse, Normal, RMAOS)
  - Resolution mismatches between slot textures
  - Non-power-of-two resolutions
  - Enabled features with missing corresponding textures
  - NIF slot conflicts (TX06: CoatNormalRoughness vs Fuzz; TX07: Subsurface vs CoatColor)
  - Empty vanilla match texture path
- Errors block export; warnings allow continue with user confirmation

### 3.9 Landscape support

- Any texture set can optionally have one or more Landscape TXST EDIDs
- When EDIDs are present, the exporter generates `PBRTextureSets/<edid>.json` per EDID
- The JSON contains only material parameters (roughnessScale, displacementScale, specularLevel, subsurfaceColor, subsurfaceOpacity)
- Textures are shared with NIF export — no separate landscape texture output
- This is an additive option, not a separate type: the same set can serve both NIF and Landscape

### 3.10 Known current limitations

- No undo/redo
- No localization support

## 4. Texture Slot Model

### 4.1 Slot reference

| Enum | NIF Slot | Suffix | Content | Status |
|------|----------|--------|---------|--------|
| Diffuse | TX00 | `.dds` | Base Color RGB + Opacity A | Implemented |
| Normal | TX01 | `_n.dds` | Normal Map RGB | Implemented |
| Emissive | TX02 | `_g.dds` | Emissive / Glow RGB | Implemented |
| Displacement | TX03 | `_p.dds` | Height / Parallax | Implemented |
| RMAOS | TX05 | `_rmaos.dds` | Roughness R, Metallic G, AO B, Specular A | Implemented |
| CoatNormalRoughness | TX06 | `_cnr.dds` | Coat Normal RGB + Coat Roughness A | Implemented |
| Fuzz | TX06 | `_f.dds` | Fuzz RGB + Fuzz Mask A | Implemented |
| Subsurface | TX07 | `_s.dds` | Subsurface RGB + Opacity A | Implemented |
| CoatColor | TX07 | `_s.dds` | Coat Color RGB + Strength A | Implemented |

Notes:

- TX06 is shared by coat normal roughness and fuzz.
- TX07 is shared by subsurface and coat color.
- The code allows both enums to exist in the data model; conflict handling remains a workflow responsibility.

### 4.2 RMAOS channels

The current channel packing model supports:

| Channel | Output Channel |
|---------|----------------|
| Roughness | R |
| Metallic | G |
| AO | B |
| Specular | A |

When a split channel is missing, exporter defaults are used:

- Roughness: 255
- Metallic: 0
- AO: 255
- Specular: 255

## 5. Core Data Model

### 5.1 Enumerations

Implemented enums in the model layer:

- `PBRTextureSlot` (9 values)
- `ChannelMap` (4 values)
- `RMAOSSourceMode` with `PackedTexture` and `SeparateChannels`
- `TextureMatchMode` with `Auto`, `Diffuse`, and `Normal`
- `DDSCompressionMode` with BC7, BC6H, BC5, BC4, BC3, BC1, and RGBA8 variants (10 total, both sRGB and Linear where applicable)
- `TextureAlphaMode` with `Unknown`, `None`, `Opaque`, and `Transparent`

### 5.2 Texture entry types

`TextureEntry` stores:

- `sourcePath`
- `slot`
- `width`
- `height`
- `channels`
- `alphaMode` (detected during import: Unknown, None, Opaque, Transparent)
- `format`

`ChannelMapEntry` stores the same metadata except for slot and alphaMode.

### 5.3 Feature flags

The current project model stores these flags:

- `emissive`
- `parallax`
- `subsurface`
- `subsurfaceFoliage`
- `multilayer`
- `coatDiffuse`
- `coatParallax`
- `coatNormal`
- `fuzz`
- `glint`
- `hair`

### 5.4 Parameters

The current parameter model stores:

- `specularLevel`
- `roughnessScale`
- `displacementScale`
- `subsurfaceOpacity`
- `subsurfaceColor`
- `emissiveScale`
- `coatStrength`
- `coatRoughness`
- `coatSpecularLevel`
- `fuzzColor`
- `fuzzWeight`
- `glintScreenSpaceScale`
- `glintLogMicrofacetDensity`
- `glintMicrofacetRoughness`
- `glintDensityRandomization`
- `vertexColors`
- `vertexColorLumMult`
- `vertexColorSatMult`

### 5.5 Texture set

Each `PBRTextureSet` currently contains:

- Display name
- Vanilla match texture path
- Vanilla match mode: auto, diffuse, or normal
- Imported textures map
- Per-slot export compression map
- Per-slot export size override map (`{0,0}` = original, otherwise power-of-two target)
- Per-slot path override map (custom PGPatcher `slotN` export paths)
- Active RMAOS source mode
- Split channel map entries
- Feature flags
- Parameters
- Landscape TXST EDIDs (optional, one JSON per EDID)
- Tags and notes

### 5.6 Project

`Project` currently contains:

- `name`
- `outputModFolder`
- `textureSets`

It also implements:

- `addTextureSet`
- `removeTextureSet`
- `save`
- `load`

## 6. Project File Format

Projects are saved as JSON using the `.tpbr` extension.

Top-level fields currently written by the app:

```json
{
  "version": "1.0",
  "name": "ExampleProject",
  "output_mod_folder": "D:/Mods/Example",
  "texture_sets": []
}
```

Each texture set currently serializes:

- Name and match texture
- Match texture mode
- Tags and notes
- `features`
- `params`
- `rmaos_source_mode`
- `textures`
- `export_compression`
- `channel_maps`

Compression overrides are stored using stable keys such as:

- `bc7_srgb`
- `bc7_linear`
- `bc3_srgb`
- `bc6h_uf16`
- `bc5_linear`
- `bc4_linear`
- `bc1_srgb`
- `bc1_linear`
- `rgba8_srgb`
- `rgba8_linear`

RMAOS source mode uses:

- `packed`
- `split`

Match texture mode uses:

- `auto`
- `diffuse`
- `normal`

## 7. PGPatcher JSON Output

The exporter currently writes a JSON array, not a `default` plus `entries` object.

Output path:

```text
<mod_folder>/PBRNIFPatcher/<project_name>.json
```

Representative entry shape:

```json
[
  {
    "texture": "architecture\\whiterun\\wrwoodplank01",
    "emissive": false,
    "parallax": true,
    "subsurface_foliage": false,
    "subsurface": false,
    "specular_level": 0.04,
    "roughness_scale": 1.0,
    "subsurface_opacity": 1.0,
    "displacement_scale": 1.0,
    "subsurface_color": [1.0, 1.0, 1.0]
  }
]
```

Conditional fields currently emitted by implementation:

- `emissive_scale` when emissive is enabled
- `match_normal` when a set is configured to match vanilla normal instead of diffuse
- `rename` when exported PBR texture base name differs from the matched vanilla base name
- Explicit `slotN` paths when the generated path differs from what PGPatcher would infer by convention
- `lock_diffuse`, `lock_normal`, `lock_emissive`, `lock_parallax`, `lock_rmaos`, `lock_subsurface`, `lock_cnr` when corresponding slots have no exported texture
- Coat fields when multilayer or coat normal is enabled
- `fuzz` object when fuzz is enabled
- `glint` object when glint is enabled
- `hair` when hair is enabled
- Vertex-color override fields when they differ from defaults
- All float values are rounded to 3 decimal places

## 8. Export Behavior

### 8.1 Output layout

```text
<mod_folder>/
├── PBRNIFPatcher/
│   └── <project_name>.json
└── textures/
    └── pbr/
        └── <match_texture_parent>/
            ├── <stem>.dds
            ├── <stem>_n.dds
            ├── <stem>_rmaos.dds
            └── ...
```

Example:

If `matchTexture` is `architecture\whiterun\wrwoodplank01`, the diffuse export path is:

```text
textures/pbr/architecture/whiterun/wrwoodplank01.dds
```

If the texture set name is changed, the exporter keeps the vanilla match directory but uses the texture set name as the PBR file base name. The PGPatcher JSON uses `rename` when possible, and falls back to explicit `slotN` paths when needed.

### 8.2 Export rules

- Each assigned texture slot is exported to DDS.
- Source DDS files whose format and mipmap count already match the target compression mode are copied as-is without re-encoding (copy-through optimization).
- Other source DDS files are decoded and re-encoded using the selected export compression.
- Raster sources are loaded and encoded to DDS.
- Alpha mode is detected during import and influences compression availability: BC1 is only offered when alpha is None or Opaque. If a texture with real alpha data is assigned to a BC1-configured slot, the exporter falls back to BC7.
- If `rmaosSourceMode` is `SeparateChannels`, the assigned RMAOS slot texture is ignored and a new `_rmaos.dds` is generated from channels.
- If required source files are missing, export continues and reports failures through logging and return status.

### 8.3 Default compression policy

Current defaults from the code:

| Slot | Default Compression |
|------|---------------------|
| Diffuse | BC7 sRGB |
| Subsurface | BC7 sRGB |
| Fuzz | BC7 sRGB |
| CoatColor | BC7 sRGB |
| Emissive | BC6H UF16 |
| Displacement | BC4 Linear |
| Normal | BC7 Linear |
| RMAOS | BC7 Linear |
| CoatNormalRoughness | BC7 Linear |

## 9. UI Architecture

### 9.1 Main window

`MainWindow` coordinates:

- File menu actions: new, open, save, export
- Current project state
- Current texture set selection
- Refresh of editor and preview panels

### 9.2 Panels

Current UI composition:

- `TextureSetPanel`: list of texture sets with add/rename/remove actions
- `SlotEditorWidget`: match path, match mode, slot imports, RMAOS mode, split-channel rows, compression selectors
- `FeatureTogglePanel`: feature checkboxes
- `ParameterPanel`: parameter editors grouped by feature
- `TexturePreviewWidget`: basic image display with wheel zoom and drag pan
- `DropZoneLabel`: custom widget for drag-and-drop with thumbnail preview, click-to-browse, and DDS thumbnail loading

Note: `ExportDialog` exists in the codebase but is currently unused; `MainWindow` uses an inline export folder row instead. `ImportDialog` is a thin wrapper over `QFileDialog::getOpenFileName`.

### 9.3 Preview

The preview area is a `QStackedWidget` toggled via 2D/3D buttons.

**2D Mode** (default):
1. Show the current set's diffuse texture if present.
2. Otherwise show the normal map if present.
3. Otherwise clear the preview.
4. Channel isolation (R/G/B/A) available via buttons when a texture is shown.
5. Click any slot's DropZone to preview that specific texture.

**3D Mode**:
- `MaterialPreviewWidget` wraps the D3D12 renderer in a Qt widget.
- Loads Diffuse, Normal, RMAOS, and feature textures (Emissive, Coat/Fuzz, Subsurface/CoatColor).
- RMAOS composed from split channels when in SeparateChannels mode.
- Input: left-drag orbit, right-drag rotate light, middle-drag rotate HDRI, scroll zoom.
- Shape selector: Sphere, Plane, Cube, Rounded Cube.

**3D Control Bar** (visible only in 3D mode):
- Light intensity slider (0–10) and color picker button
- Exposure slider (EV compensation)
- HDRI selector combo (scans a folder for .exr/.hdr/.dds files)
- IBL intensity slider, prefilter resolution combo, sample count combo
- Render flag checkboxes: Horizon Occlusion, Multi-Bounce AO, Specular Occlusion
- VSync, TAA, and HDR checkboxes
- Paper-white and peak-brightness sliders (HDR mode only)

### 9.4 D3D12 Renderer

`D3D12Renderer` provides the GPU backend:

- Double-buffered frame management with per-frame command allocators and fence values
- Dedicated async copy queue (`D3D12UploadQueue`) with 64 MB ring buffer for texture uploads
- `DescriptorHeap` helper for linear SRV/CBV/UAV and RTV/DSV allocation
- Cook-Torrance PBR pipeline state with precompiled vertex and pixel shaders
- Skybox pipeline for HDRI environment background
- GT7 tone-mapping post-process pass
- TAA resolve compute pass with velocity reprojection
- HDR (scRGB) and SDR swap chain modes
- GPU adapter selection preferring discrete GPUs
- Device-lost detection (`checkDeviceLost()`, `isDeviceLost()`)

### 9.5 IBL Pipeline

`IBLPipeline` orchestrates GPU-based image-based lighting:

1. Load HDRI file (EXR/HDR/DDS) via `IBLPipeline::loadHDRI()` with color space detection
2. Convert pixels to ACEScg working space
3. Run 4 GPU compute passes:
   - Equirect → Cubemap (`IBLEquirectToCube.hlsl`)
   - ZH3 Diffuse Irradiance (`IBLDiffuseIrradiance.hlsl`)
   - GGX Specular Prefilter (`IBLPrefilter.hlsl`) with configurable mip chain
   - BRDF Integration LUT (`IBLBrdfLut.hlsl`)
4. Results: ZH3 irradiance coefficients, prefiltered cubemap, intermediate cubemap for skybox, BRDF LUT

CPU fallback (`computeZH3CPU()`) available when GPU processing is not possible.

### 9.6 Shader files

| File | Type | Purpose |
|------|------|---------|
| `PBRShader.hlsl` | VS+PS | Cook-Torrance PBR with IBL, feature textures, TAA velocity |
| `SkyboxShader.hlsl` | VS+PS | Fullscreen HDRI background with Y-axis rotation |
| `ToneMapShader.hlsl` | VS+PS | GT7 tone mapping, exposure, SDR/HDR output |
| `TAAResolve.hlsl` | CS | Temporal resolve with history reprojection |
| `IBLEquirectToCube.hlsl` | CS | Equirectangular to 6-face cubemap |
| `IBLCubemapMipGen.hlsl` | CS | Wide 9-tap cubemap mip generation |
| `IBLDiffuseIrradiance.hlsl` | CS | ZH3 projection for diffuse irradiance |
| `IBLPrefilter.hlsl` | CS | GGX importance-sampled specular prefilter |
| `IBLBrdfLut.hlsl` | CS | Split-sum BRDF integration LUT |

Shared HLSL includes under `Common/`: Math, BRDF, Shading, PBRMath, PBR, Random, ColorSpaces, GT7ToneMap, Glints2023.

## 10. Source Tree

```text
TruePBR-Manager/
├── CMakeLists.txt
├── CMakePresets.json
├── build.bat
├── builddebug.bat
├── buildrelease.bat
├── README.md
├── SPEC.md
├── docs/
├── resources/
└── src/
    ├── app/
    ├── core/
    ├── renderer/
    ├── ui/
    ├── third_party/
    └── utils/
```

Important implementation modules:

- `core/Project.*`: project serialization and CRUD
- `core/PBRTextureSet.*`: enums, slot metadata, compression metadata
- `core/TextureImporter.*`: import metadata inspection
- `core/ChannelPacker.*`: split-channel RMAOS generation
- `core/JsonExporter.*`: PGPatcher JSON generation
- `core/ModExporter.*`: DDS and JSON export orchestration
- `core/LandscapeExporter.*`: Landscape TXST JSON generation
- `core/TextureSetValidator.*`: pre-export validation checks
- `renderer/D3D12Renderer.*`: D3D12 GPU backend, double-buffered rendering
- `renderer/D3D12UploadQueue.*`: async texture upload via copy queue
- `renderer/DescriptorHeap.*`: descriptor heap allocation helper
- `renderer/IBLPipeline.*`: GPU IBL compute orchestration
- `renderer/MeshGenerator.*`: procedural mesh generation (Sphere, Plane, Cube, RoundedCube)
- `ui/MaterialPreviewWidget.*`: D3D12-based 3D preview Qt widget
- `ui/SlotEditorWidget.*`: slot/channel authoring UI
- `ui/ParameterPanel.*`: numeric material parameter UI

## 11. Build and Packaging

### 11.1 Prerequisites

- Visual Studio 2022 or newer with Desktop C++ workload
- CMake 3.21 or newer
- vcpkg with `VCPKG_ROOT` configured

### 11.2 Supported build entry points

`build.bat`:

- Accepts `debug` or `release` argument (defaults to `debug`)
- Delegates to `builddebug.bat` or `buildrelease.bat`
- Validates `VCPKG_ROOT`
- Locates Visual Studio via `vswhere` when needed
- Uses Ninja when available, otherwise falls back to NMake Makefiles
- Configures the build in `build/`
- Auto-cleans CMake cache when platform triplet changes

CMake presets:

- `default` configure preset for Release
- `debug` configure preset for Debug
- `release` and `debug` build presets

### 11.3 Shader compilation

- HLSL shaders are precompiled to `.cso` files during build via `dxc.exe`
- Compiled shader objects are copied to the output directory alongside the executable
- Shader source files live in `src/renderer/` with shared includes in `src/renderer/Common/`

### 11.4 Packaged output

The CMake target currently places the runtime package in:

```text
dist/TruePBR-Manager/
```

Post-build steps currently do the following:

- Copy runtime DLLs next to the executable
- Run `windeployqt` when available, otherwise copy `qwindows.dll`
- Copy `LICENSE`

## 12. Dependencies

| Library | Purpose | Source |
|---------|---------|--------|
| Qt 6 Widgets | UI framework | vcpkg (qtbase) |
| nlohmann/json | JSON serialization | vcpkg (nlohmann-json) |
| spdlog | Logging | vcpkg (spdlog) |
| DirectXTex | DDS metadata, decode, encode, compression | vcpkg (directxtex) |
| stb_image | Raster image loading | vcpkg (stb) |
| tinyexr | EXR image loading for HDRI | Vendored (v1.0.9, header-only) |
| D3D12 / DXGI | GPU rendering (3D preview) | Windows SDK (system) |
| d3dcompiler | HLSL shader compilation fallback | Windows SDK (system) |

## 13. CI/CD

### 13.1 GitHub Actions

Two workflows are configured:

- `ci.yml`: runs on PRs and pushes to `main`. Performs pre-commit checks (`prek`), then builds on `windows-latest` with MSVC and uploads `dist/TruePBR-Manager` as an artifact.
- `release.yml`: triggers on tags matching `v*`. Builds, zips `dist/TruePBR-Manager`, and uploads to GitHub Releases with auto-generated release notes.

### 13.2 Code formatting

- `.clang-format` based on LLVM style with IndentWidth 4, ColumnLimit 120, Allman braces.
- `.pre-commit-config.yaml` enforces clang-format on `src/` plus standard text hygiene hooks.

## 14. Conventions

- Types use PascalCase.
- Functions and variables use camelCase.
- Public headers use `#pragma once`.
- Public APIs are documented with Doxygen-style comments in headers.
- Core logic is kept separate from Qt UI widgets.

## 15. Roadmap

Planned features not yet implemented:

- [ ] Import existing PBR mod (导入已有 PBR mod)
- [ ] Built-in vanilla texture set conversion (内置 vanilla texture set 转换)
- [ ] Export progress bar (导出进度条)
- [ ] Skip unchanged textures on export (导出跳过状态没有改变的已有贴图)
- [ ] Undo/redo
- [ ] Localization support
