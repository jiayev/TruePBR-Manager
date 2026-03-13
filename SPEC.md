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
- Persist imported file metadata: source path, dimensions, channel count, format

### 3.3 Material authoring

- Edit feature flags in the UI
- Edit base, emissive, parallax, subsurface, coat, fuzz, and glint parameters in the UI
- Store per-slot export compression overrides
- Store RMAOS source mode per texture set

### 3.4 Preview and export

- Image preview with zoom, pan, and per-channel isolation (R/G/B/A)
- Click any slot's texture to preview it; channel buttons appear for multi-channel textures
- DDS export for each assigned slot
- Split-channel packing into `_rmaos.dds` during export
- PGPatcher JSON export to `PBRNIFPatcher/<project>.json`
- Automatic export directory creation under `textures/pbr/...`
- Pre-export validation with error/warning reporting

### 3.5 Batch import

- Scan a folder and auto-assign textures to slots by suffix convention
- Recognized slot suffixes: `_n`, `_g`, `_p`, `_rmaos`, `_cnr`, `_f`, `_s`
- Recognized channel suffixes: `_roughness`, `_metallic`, `_ao`, `_specular` (and abbreviations)
- Files with no recognized suffix are assigned as Diffuse
- If channel maps are found, RMAOS source mode is automatically set to SeparateChannels

### 3.6 Input validation

- Pre-export validation checks per texture set:
  - Missing required slots (Diffuse, Normal, RMAOS)
  - Resolution mismatches between slot textures
  - Non-power-of-two resolutions
  - Enabled features with missing corresponding textures
  - NIF slot conflicts (TX06: CoatNormalRoughness vs Fuzz; TX07: Subsurface vs CoatColor)
  - Empty vanilla match texture path
- Errors block export; warnings allow continue with user confirmation

### 3.5 Known current limitations

- No undo/redo
- No 3D shaded material preview
- No localization support
- No Landscape texture set support (PBRTextureSets JSON)
- Vertex-color tuning fields exist in the data/export layer but are not exposed in the current UI

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
- Active RMAOS source mode
- Split channel map entries
- Feature flags
- Parameters
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

### 9.3 Current preview behavior

Preview selection logic is simple:

1. Show the current set's diffuse texture if present.
2. Otherwise show the normal map if present.
3. Otherwise clear the preview.

## 10. Source Tree

```text
TruePBR-Manager/
├── CMakeLists.txt
├── CMakePresets.json
├── build.bat
├── README.md
├── SPEC.md
├── resources/
└── src/
    ├── app/
    ├── core/
    ├── ui/
    └── utils/
```

Important implementation modules:

- `core/Project.*`: project serialization and CRUD
- `core/PBRTextureSet.*`: enums, slot metadata, compression metadata
- `core/TextureImporter.*`: import metadata inspection
- `core/ChannelPacker.*`: split-channel RMAOS generation
- `core/JsonExporter.*`: PGPatcher JSON generation
- `core/ModExporter.*`: DDS and JSON export orchestration
- `core/TextureSetValidator.*`: pre-export validation checks
- `ui/SlotEditorWidget.*`: slot/channel authoring UI
- `ui/ParameterPanel.*`: numeric material parameter UI

## 11. Build and Packaging

### 11.1 Prerequisites

- Visual Studio 2022 or newer with Desktop C++ workload
- CMake 3.21 or newer
- vcpkg with `VCPKG_ROOT` configured

### 11.2 Supported build entry points

`build.bat`:

- Validates `VCPKG_ROOT`
- Locates Visual Studio via `vswhere` when needed
- Uses Ninja when available, otherwise falls back to NMake Makefiles
- Configures the build in `build/`

CMake presets:

- `default` configure preset for Release
- `debug` configure preset for Debug
- `release` and `debug` build presets

### 11.3 Packaged output

The CMake target currently places the runtime package in:

```text
dist/TruePBR-Manager/
```

Post-build steps currently do the following:

- Copy runtime DLLs next to the executable
- Run `windeployqt` when available, otherwise copy `qwindows.dll`
- Copy `LICENSE`

## 12. Dependencies

| Library | Purpose | vcpkg Port |
|---------|---------|------------|
| Qt 6 Widgets | UI framework | qtbase |
| nlohmann/json | JSON serialization | nlohmann-json |
| spdlog | Logging | spdlog |
| DirectXTex | DDS metadata, decode, encode, compression | directxtex |
| stb_image | Raster image loading | stb |

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
