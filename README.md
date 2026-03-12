# TruePBR Manager

[![CI](https://github.com/jiayev/TruePBR-Manager/actions/workflows/ci.yml/badge.svg)](https://github.com/jiayev/TruePBR-Manager/actions/workflows/ci.yml)
[![Release](https://github.com/jiayev/TruePBR-Manager/actions/workflows/release.yml/badge.svg)](https://github.com/jiayev/TruePBR-Manager/actions/workflows/release.yml)

TruePBR Manager is a Windows desktop tool for assembling and exporting Skyrim True PBR texture sets compatible with [Community Shaders True PBR](https://github.com/doodlum/skyrim-community-shaders/wiki/True-PBR) and [PGPatcher](https://github.com/hakasapl/PGPatcher).

Each texture set maps to one vanilla diffuse path and stores the textures, feature flags, parameters, and export settings needed to generate a mod-ready True PBR package.

## Current Status

The application currently supports:

- Creating projects in memory and saving/loading them as `.tpbr` JSON files
- Managing multiple texture sets per project
- Importing texture slots from DDS and common raster formats such as PNG, TGA, and BMP
- Drag-and-drop import directly onto slot and channel targets
- Authoring RMAOS either as a pre-packed texture or as separate Roughness/Metallic/AO/Specular inputs
- Packing split RMAOS channels into a DDS during export
- Editing major True PBR feature flags: emissive, parallax, subsurface, foliage, multilayer, coat options, fuzz, glint, hair
- Editing the corresponding material parameters in the UI
- Choosing DDS compression per export slot
- Previewing imported textures with zoom and pan
- Exporting DDS textures into the correct `textures/pbr/...` layout and generating a PGPatcher JSON file

The application does not currently provide batch import, undo/redo, channel-isolated preview, 3D material preview, or localization.

## Texture Slots

| Slot | Suffix | Content | Notes |
|------|--------|---------|-------|
| Diffuse | `.dds` | Base Color (RGB) + Opacity (A) | Required |
| Normal | `_n.dds` | Normal Map (RGB) | Required |
| RMAOS | `_rmaos.dds` | Roughness (R) Metallic (G) AO (B) Specular (A) | Required, can be imported packed or built from split channels |
| Emissive | `_g.dds` | Glow Color | Optional |
| Displacement | `_p.dds` | Height / Parallax | Optional |
| Subsurface | `_s.dds` | Subsurface Color (RGB) + Opacity (A) | Shares TX07 with coat color |
| Coat Normal Roughness | `_cnr.dds` | Coat Normal (RGB) + Coat Roughness (A) | Shares TX06 with fuzz |
| Fuzz | `_f.dds` | Fuzz Color (RGB) + Fuzz Mask (A) | Shares TX06 with coat normal |
| Coat Color | `_s.dds` | Coat Color (RGB) + Strength (A) | Shares TX07 with subsurface |

## Typical Workflow

1. Create a project and add one or more texture sets.
2. Set the vanilla match path for each set, for example `architecture\whiterun\wrwoodplank01`.
3. Import required slots and any optional textures needed by the enabled feature set.
4. For RMAOS, choose either `Packed RMAOS` or `Split Channels`.
5. Adjust material parameters and per-slot export compression.
6. Export to a mod folder to generate DDS files and `PBRNIFPatcher/<project>.json`.

## Export Layout

```text
<mod_folder>/
├── PBRNIFPatcher/
│   └── <project>.json
└── textures/
    └── pbr/
        └── <vanilla_path>/
            ├── texture.dds
            ├── texture_n.dds
            ├── texture_rmaos.dds
            └── ...
```

Example match path:

```text
architecture\whiterun\wrwoodplank01
```

Exports to:

```text
textures/pbr/architecture/whiterun/
    wrwoodplank01.dds
    wrwoodplank01_n.dds
    wrwoodplank01_rmaos.dds
```

## Building

## Pre-commit

The repository includes a pre-commit configuration focused on automatic formatting and basic text hygiene:

- `clang-format` for C and C++ source files under `src/`
- `end-of-file-fixer` to normalize the final newline
- `trailing-whitespace` to trim trailing spaces while preserving Markdown hard line breaks
- `check-yaml`, `check-merge-conflict`, `check-case-conflict`, and `check-illegal-windows-names`

If you are using `prek`, install the hook scripts with:

```powershell
prek install
```

To format and normalize the whole repository once:

```powershell
prek run --all-files
```

If you are using the reference `pre-commit` CLI instead, the equivalent commands are:

```powershell
pre-commit install
pre-commit run --all-files
```

## GitHub Actions

The repository now includes two GitHub Actions workflows:

- [`CI`](https://github.com/jiayev/TruePBR-Manager/actions/workflows/ci.yml): runs `prek` on every pull request and on pushes to `main`, then builds the application on `windows-latest`
- [`Release`](https://github.com/jiayev/TruePBR-Manager/actions/workflows/release.yml): triggers on tags matching `v*`, builds the distributable, zips `dist/TruePBR-Manager/`, and uploads it to the GitHub Release page

Recommended baseline for this project:

- `CI` for formatting, lint-style checks, and build validation
- `Release` for tagged packaging and downloadable binaries
- `Tests` later, once the project has automated tests to run

### Requirements

- Windows x64
- Visual Studio 2022 or newer with the Desktop C++ workload
- CMake 3.21 or newer
- vcpkg with `VCPKG_ROOT` set

### Quick Build

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
build.bat
```

The script configures the project in `build/` and builds the distributable into `dist/TruePBR-Manager/`. That folder is also populated with required runtime DLLs, Qt platform plugins, and `LICENSE`.

### CMake Presets

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset default
cmake --build --preset release
```

Available configure presets:

- `default` for Release
- `debug` for Debug

### Manual CMake

```powershell
cmake -S . -B build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake `
    -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build
```

## Dependencies

Managed through vcpkg:

| Library | Purpose |
|---------|---------|
| Qt 6 Widgets | Desktop UI |
| DirectXTex | DDS metadata, decode, and encode |
| nlohmann/json | Project and export JSON serialization |
| spdlog | Logging |
| stb_image | Raster image loading |

## Project Layout

```text
src/
├── app/        MainWindow and application shell
├── core/       Project model, texture import, RMAOS packing, JSON export, mod export
├── ui/         Texture set list, slot editor, feature toggles, parameter editor, export dialog, preview
└── utils/      DDS helpers, image loading, file helpers, logging
```

## Implementation Notes

- Projects are saved as JSON with the `.tpbr` extension.
- RMAOS source mode is persisted per texture set as either packed or split-channel mode.
- Export compression can be overridden per slot and is stored in the project file.
- Preview currently shows the first available diffuse texture, otherwise the normal map.

## Roadmap

- [x] Project create/save/load
- [x] Texture slot import with metadata detection
- [x] Drag-and-drop import for slots and RMAOS channels
- [x] Split-channel RMAOS packing on export
- [x] Feature toggle editing
- [x] Parameter editing
- [x] DDS export with per-slot compression selection
- [x] PGPatcher JSON export
- [x] Basic preview with zoom and pan
- [ ] Batch import with suffix detection
- [ ] Channel-isolated preview tools
- [ ] Undo/redo
- [ ] Validation and warning surface for mismatched inputs
- [ ] 3D material preview
- [ ] Localization

## References

- [True PBR Specification](https://github.com/doodlum/skyrim-community-shaders/wiki/True-PBR)
- [PGPatcher Mod Authors Guide](https://github.com/hakasapl/PGPatcher/wiki/Mod-Authors)
- [Community Shaders](https://github.com/doodlum/skyrim-community-shaders)

## License

This project is licensed under the [Creative Commons Attribution-NonCommercial 4.0 International License (CC BY-NC 4.0)](https://creativecommons.org/licenses/by-nc/4.0/).

See [LICENSE](LICENSE) for details.
