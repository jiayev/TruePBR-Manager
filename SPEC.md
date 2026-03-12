# TruePBR Manager - Project Specification

## 1. Overview

| Item              | Value                        |
|-------------------|------------------------------|
| Project Name      | TruePBR Manager              |
| Type              | Desktop GUI Application      |
| Language          | C++20                        |
| Build System      | CMake 3.21+                  |
| GUI Framework     | Qt 6                         |
| Package Manager   | vcpkg                        |
| Target Platform   | Windows (x64)                |
| License           | CC BY-NC 4.0                 |

## 2. Purpose

TruePBR Manager is a desktop tool for **Skyrim modding artists** to:

1. **Manage PBR texture sets** — each set maps a group of PBR textures to one vanilla Skyrim texture path
2. **Import textures** — accept individual channel maps (Roughness, Metallic, AO, Specular) OR pre-packed RMAOS DDS
3. **Pack channels** — merge individual R/M/AO/S maps into a single RMAOS DDS for in-game use
4. **Configure PBR features** — toggle emissive, subsurface, parallax, multilayer, fuzz, glint, hair etc. and import corresponding textures
5. **Export to mod folder** — output DDS files with correct directory structure under `textures/pbr/...` and generate PGPatcher-compatible JSON

References:
- True PBR spec: https://github.com/doodlum/skyrim-community-shaders/wiki/True-PBR
- PGPatcher JSON spec: https://github.com/hakasapl/PGPatcher/wiki/Mod-Authors

## 3. True PBR Texture Slot Reference

Based on the Community Shaders True PBR specification:

| Slot | NIF Slot | Suffix       | Content                                     | Format   | Required |
|------|----------|--------------|---------------------------------------------|----------|----------|
| 1    | TX00     | `.dds`       | Base Color (RGB) + Opacity (A)              | BC7/BC1  | YES      |
| 2    | TX01     | `_n.dds`     | Normal Map (RGB), A unused                  | BC7      | YES      |
| 3    | TX02     | `_g.dds`     | Emissive/Glow Color (RGB)                   | BC1/BC7  | if emissive |
| 4    | TX03     | `_p.dds`     | Displacement/Height (R only)                | BC4      | if parallax |
| 5    | TX04     | —            | Unused                                      | —        | NO       |
| 6    | TX05     | `_rmaos.dds` | Roughness(R) Metallic(G) AO(B) Specular(A) | BC1/BC7  | YES      |
| 7    | TX06     | `_cnr.dds`   | Coat Normal(RGB) + Coat Roughness(A)        | BC7      | if multilayer |
|      |          | `_f.dds`     | Fuzz Color(RGB) + Fuzz Mask(A)              | BC7      | if fuzz  |
| 8    | TX07     | `_s.dds`     | Subsurface Color(RGB) + Opacity(A)          | BC7      | if subsurface |
|      |          | `_s.dds`     | Coat Color(RGB) + Coat Strength(A)          | BC7      | if multilayer coat_diffuse |

### PGPatcher Naming Convention

| Texture Type           | Suffix         |
|------------------------|----------------|
| Diffuse                | `_d.dds` / `.dds` |
| Normal                 | `_n.dds`       |
| Glow / Emissive        | `_g.dds`       |
| Height / Displacement  | `_p.dds`       |
| RMAOS                  | `_rmaos.dds`   |
| Subsurface Tint        | `_s.dds`       |
| Coat Normal Roughness  | `_cnr.dds`     |
| Fuzz                   | `_f.dds`       |

## 4. Core Data Model

### 4.1 PBRTextureSlot (enum)

```cpp
enum class PBRTextureSlot {
    Diffuse,        // Slot 1: Base Color + Opacity
    Normal,         // Slot 2: Normal Map
    Emissive,       // Slot 3: Glow/Emissive (optional)
    Displacement,   // Slot 4: Height/Parallax (optional)
    RMAOS,          // Slot 6: Roughness+Metallic+AO+Specular (packed)
    CoatNormalRoughness, // Slot 7: Multilayer coat normal+roughness (optional)
    Fuzz,           // Slot 7: Fuzz color+mask (optional, conflicts with CoatNormalRoughness)
    Subsurface,     // Slot 8: Subsurface color+opacity (optional)
    CoatColor,      // Slot 8: Coat color+strength (optional, conflicts with Subsurface)
};
```

### 4.2 Individual channel maps (for RMAOS packing)

```cpp
enum class ChannelMap {
    Roughness,
    Metallic,
    AO,
    Specular,
};
```

### 4.3 TextureEntry

```cpp
struct TextureEntry {
    std::filesystem::path sourcePath;  // Original imported file (png/dds)
    PBRTextureSlot        slot;
    int                   width = 0;
    int                   height = 0;
    int                   channels = 0;
    std::string           format;      // "png", "dds", etc.
};
```

### 4.4 PBRFeatureFlags

```cpp
struct PBRFeatureFlags {
    bool emissive       = false;
    bool parallax       = false;
    bool subsurface     = false;
    bool subsurfaceFoliage = false;
    bool multilayer     = false;   // Multilayer Parallax (coat)
    bool coatDiffuse    = false;   // Coat color layer
    bool coatParallax   = false;   // Coat parallax
    bool coatNormal     = false;   // Coat own normal
    bool fuzz           = false;
    bool glint          = false;
    bool hair           = false;
};
```

### 4.5 PBRParameters

```cpp
struct PBRParameters {
    float specularLevel     = 0.04f;
    float roughnessScale    = 1.0f;
    float displacementScale = 1.0f;
    float subsurfaceOpacity = 1.0f;
    std::array<float,3> subsurfaceColor = {1.0f, 1.0f, 1.0f};

    // Emissive
    float emissiveScale = 0.0f;

    // Multilayer / Coat
    float coatStrength       = 0.0f;
    float coatRoughness      = 0.0f;
    float coatSpecularLevel  = 0.04f;

    // Fuzz (only if fuzz enabled)
    std::array<float,3> fuzzColor = {1.0f, 1.0f, 1.0f};
    float fuzzWeight = 1.0f;

    // Glint (only if glint enabled)
    float glintScreenSpaceScale    = 0.0f;
    float glintLogMicrofacetDensity = 0.0f;
    float glintMicrofacetRoughness  = 0.0f;
    float glintDensityRandomization = 0.0f;

    // Mesh tweak
    bool  vertexColors = true;
    float vertexColorLumMult = 1.0f;
    float vertexColorSatMult = 1.0f;
};
```

### 4.6 PBRTextureSet

One PBR texture set corresponds to **one vanilla texture path** (the matching target).

```cpp
struct PBRTextureSet {
    std::string name;                       // Display name (e.g. "WhiterunWoodPlank01")
    std::string matchTexture;               // Vanilla diffuse to match (e.g. "architecture\\whiterun\\wrwoodplank01")

    // Imported textures per slot
    std::map<PBRTextureSlot, TextureEntry> textures;

    // Individual channel maps (before packing into RMAOS)
    std::map<ChannelMap, std::filesystem::path> channelMaps;

    // Feature toggles & parameters
    PBRFeatureFlags  features;
    PBRParameters    params;

    // Metadata
    std::string tags;
    std::string notes;
};
```

### 4.7 Project

```cpp
struct Project {
    std::string                   name;
    std::filesystem::path         outputModFolder;   // Target mod directory for export
    std::vector<PBRTextureSet>    textureSets;
};
```

## 5. PGPatcher JSON Output Format

The tool exports a JSON file to `<mod_folder>/PBRNIFPatcher/<project_name>.json`.

ALL PBR textures are output under `textures/pbr/` prefix.

### Simple format (array of entries):

```json
[
    {
        "texture": "architecture\\whiterun\\wrwoodplank01",
        "emissive": false,
        "parallax": true,
        "subsurface_foliage": false,
        "subsurface": false,
        "specular_level": 0.04,
        "subsurface_color": [1, 1, 1],
        "roughness_scale": 1.0,
        "subsurface_opacity": 1.0,
        "displacement_scale": 0.2
    }
]
```

### With defaults format:

```json
{
    "default": {
        "specular_level": 0.04,
        "roughness_scale": 1.0,
        "subsurface_opacity": 1.0,
        "displacement_scale": 1.0
    },
    "entries": [
        {
            "texture": "architecture\\whiterun\\wrwoodplank01",
            "emissive": false,
            "parallax": true,
            "subsurface_foliage": false,
            "subsurface": false
        }
    ]
}
```

### Additional JSON fields per feature:

**Emissive:**
```json
"emissive": true, "emissive_scale": 1.0
```

**Parallax:**
```json
"parallax": true, "displacement_scale": 0.2
```

**Subsurface:**
```json
"subsurface": true, "subsurface_color": [1,1,1], "subsurface_opacity": 1.0
```

**Multilayer:**
```json
"coat_normal": true, "coat_strength": 1.0, "coat_roughness": 1.0,
"coat_specular_level": 0.04, "coat_diffuse": true, "coat_parallax": true
```

**Fuzz:**
```json
"fuzz": { "texture": true, "color": [1,1,1], "weight": 1.0 }
```

**Glint:**
```json
"glint": { "screen_space_scale": 2.0, "log_microfacet_density": 18.0,
           "microfacet_roughness": 0.7, "density_randomization": 200.0 }
```

**Hair:**
```json
"hair": true
```

## 6. Export Directory Structure

When exporting to a mod folder, the tool creates:

```
<mod_folder>/
├── PBRNIFPatcher/
│   └── <project_name>.json          # PGPatcher config
│
└── textures/
    └── pbr/
        └── <original_path>/          # Mirrors vanilla texture path
            ├── wrwoodplank01.dds        # Diffuse (slot 1)
            ├── wrwoodplank01_n.dds      # Normal (slot 2)
            ├── wrwoodplank01_rmaos.dds  # RMAOS (slot 6)
            ├── wrwoodplank01_p.dds      # Displacement (slot 4, if parallax)
            ├── wrwoodplank01_g.dds      # Emissive (slot 3, if emissive)
            ├── wrwoodplank01_s.dds      # Subsurface (slot 8, if subsurface)
            ├── wrwoodplank01_cnr.dds    # Coat normal (slot 7, if multilayer)
            └── wrwoodplank01_f.dds      # Fuzz (slot 7, if fuzz)
```

## 7. Architecture

```
TruePBR-Manager/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── SPEC.md
├── .gitignore
│
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   │
│   ├── app/                         # Application shell
│   │   ├── MainWindow.h / .cpp
│   │   └── Application.h / .cpp
│   │
│   ├── core/                        # Core logic (no GUI dependency)
│   │   ├── Project.h / .cpp         # Project data model
│   │   ├── PBRTextureSet.h / .cpp   # Texture set model + feature flags
│   │   ├── TextureImporter.h / .cpp # Import png/dds, detect channels
│   │   ├── ChannelPacker.h / .cpp   # Merge R/M/AO/S into RMAOS DDS
│   │   ├── JsonExporter.h / .cpp    # Generate PGPatcher JSON
│   │   └── ModExporter.h / .cpp     # Export DDS + JSON to mod folder
│   │
│   ├── ui/                          # Qt widgets
│   │   ├── TexturePreviewWidget.h / .cpp   # Texture viewer (zoom, pan)
│   │   ├── TextureSetPanel.h / .cpp        # Texture set list & editor
│   │   ├── SlotEditorWidget.h / .cpp       # Per-slot import UI
│   │   ├── FeatureTogglePanel.h / .cpp     # PBR feature checkboxes
│   │   ├── ParameterPanel.h / .cpp         # PBR parameter sliders/spinboxes
│   │   ├── ExportDialog.h / .cpp           # Export target folder picker
│   │   └── ImportDialog.h / .cpp           # Import file dialog
│   │
│   └── utils/
│       ├── FileUtils.h / .cpp
│       ├── ImageUtils.h / .cpp     # Image loading/conversion helpers
│       └── DDSUtils.h / .cpp       # DDS read/write utilities
│
└── resources/
    ├── resources.qrc
    └── icons/
```

## 8. Dependencies

| Library         | Purpose                              | vcpkg Port      |
|-----------------|--------------------------------------|-----------------|
| Qt 6 Widgets    | GUI framework                        | qtbase          |
| nlohmann/json   | JSON serialization                   | nlohmann-json   |
| spdlog          | Logging                              | spdlog          |
| DirectXTex      | DDS read/write/compress/convert      | directxtex      |
| stb_image       | PNG/TGA/BMP loading                  | stb             |

**DirectXTex** is critical for:
- Reading DDS files (including BC1/BC4/BC7 compressed)
- Writing DDS with correct compression format
- Channel packing (merging individual maps into RMAOS)

## 9. Key Features (Phased)

### Phase 1 — MVP
- [ ] Project create / save / load
- [ ] Add PBR texture set (name + vanilla match path)
- [ ] Import textures into slots (Diffuse, Normal, RMAOS required)
- [ ] Import individual channels (R, M, AO, S) and pack into RMAOS
- [ ] Toggle PBR features: emissive, parallax, subsurface
- [ ] Import corresponding optional textures (_g, _p, _s)
- [ ] Configure PBR parameters (specular_level, roughness_scale, displacement_scale, etc.)
- [ ] Texture thumbnail list
- [ ] Texture preview panel (zoom, pan)
- [ ] Select output mod folder
- [ ] Export DDS to `textures/pbr/...` directory structure
- [ ] Generate PGPatcher JSON to `PBRNIFPatcher/`

### Phase 2 — Enhancement
- [ ] Multilayer Parallax support (coat_normal, coat_diffuse, etc.)
- [ ] Fuzz support (fuzz texture + params)
- [ ] Glint support
- [ ] Hair model support
- [ ] Channel isolation preview (R/G/B/A individual view)
- [ ] Batch import (scan folder, auto-detect suffixes)
- [ ] DDS compression format selection per slot
- [ ] Drag-and-drop import
- [ ] Undo/Redo

### Phase 3 — Advanced
- [ ] RMAOS visual preview (show individual channels color-coded)
- [ ] Texture validation (resolution mismatch, format warnings)
- [ ] PBR material preview (3D sphere with basic lighting)
- [ ] Defaults/Templates (save common parameter presets)
- [ ] Localization (CN/EN)
- [ ] Landscape texture set support (with PBRTextureSets JSON)

## 10. Build Instructions

### Prerequisites
- Visual Studio 2022+ with C++ Desktop workload
- vcpkg installed, with `VCPKG_ROOT` environment variable set
- CMake 3.21+ (or use the one bundled with VS)

### Option A: Using build.bat (recommended)

Open a **VS Developer Command Prompt** (or any terminal where `cl.exe` is in PATH):

```bat
set VCPKG_ROOT=<your_vcpkg_path>
build.bat
```

Output: `build/src/TruePBR-Manager.exe`

### Option B: Using CMake presets

Requires `VCPKG_ROOT` environment variable to be set:

```bash
cmake --preset default
cmake --build build --config Release
```

### Option C: Manual CMake

```bash
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build
```

## 11. Coding Conventions

- **Naming**: PascalCase for types, camelCase for functions/variables, UPPER_SNAKE for constants
- **Headers**: Use `#pragma once`
- **Formatting**: clang-format (based on Microsoft style)
- **Comments**: Doxygen-style (`///`) for public API
- **Error Handling**: Exceptions for unrecoverable, return codes / std::expected for recoverable
- **Includes**: Group order: std > Qt > third-party > project headers
