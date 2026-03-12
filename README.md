# TruePBR Manager

A desktop tool for Skyrim modding artists to manage, pack, and export PBR texture sets compatible with [Community Shaders True PBR](https://github.com/doodlum/skyrim-community-shaders/wiki/True-PBR) and [PGPatcher](https://github.com/hakasapl/PGPatcher).

## What It Does

Each PBR texture set maps to **one vanilla Skyrim texture**. The tool lets you:

- **Import** PBR textures (PNG or DDS) into the correct slots — Diffuse, Normal, RMAOS, Emissive, Displacement, Subsurface, etc.
- **Pack channels** — import separate Roughness, Metallic, AO, Specular maps and merge them into a single `_rmaos.dds`
- **Toggle PBR features** — emissive, parallax, subsurface scattering, multilayer parallax, fuzz, glint, hair
- **Configure parameters** — specular level, roughness scale, displacement scale, subsurface color/opacity, and more
- **Export** — output DDS files to the correct `textures/pbr/` directory structure and generate a PGPatcher-compatible JSON config

## Texture Slots

| Slot | Suffix | Content | Required |
|------|--------|---------|----------|
| Diffuse | `.dds` | Base Color (RGB) + Opacity (A) | Yes |
| Normal | `_n.dds` | Normal Map (RGB) | Yes |
| RMAOS | `_rmaos.dds` | Roughness (R) Metallic (G) AO (B) Specular (A) | Yes |
| Emissive | `_g.dds` | Glow Color (RGB) | If emissive |
| Displacement | `_p.dds` | Height Map (R) | If parallax |
| Subsurface | `_s.dds` | Subsurface Color (RGB) + Opacity (A) | If SSS |
| Coat Normal | `_cnr.dds` | Coat Normal (RGB) + Coat Roughness (A) | If multilayer |
| Fuzz | `_f.dds` | Fuzz Color (RGB) + Fuzz Mask (A) | If fuzz |

## Export Structure

```
<mod_folder>/
├── PBRNIFPatcher/
│   └── <project>.json              # PGPatcher config
└── textures/
    └── pbr/
        └── <vanilla_path>/
            ├── texture.dds          # Diffuse
            ├── texture_n.dds        # Normal
            ├── texture_rmaos.dds    # RMAOS
            └── ...                  # Optional slots
```

## Building

### Prerequisites

- **Windows** x64
- **Visual Studio 2022+** with C++ Desktop workload
- **vcpkg** with `VCPKG_ROOT` environment variable set
- **CMake** 3.21+

### Quick Build

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
build.bat
```

The script auto-detects Visual Studio via `vswhere`, sets up the compiler environment, installs dependencies through vcpkg, and builds with Ninja. Output: `build\src\TruePBR-Manager.exe`

### CMake Presets

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset default
cmake --build build
```

### Manual CMake

```bash
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build
```

## Dependencies

All managed automatically via vcpkg:

| Library | Purpose |
|---------|---------|
| Qt 6 Widgets | GUI |
| DirectXTex | DDS read/write/compress |
| nlohmann/json | JSON serialization |
| spdlog | Logging |
| stb_image | PNG/TGA/BMP loading |

## Project Structure

```
src/
├── app/        MainWindow, Application
├── core/       PBRTextureSet, Project, TextureImporter,
│               ChannelPacker, JsonExporter, ModExporter
├── ui/         TexturePreviewWidget, TextureSetPanel,
│               SlotEditorWidget, FeatureTogglePanel,
│               ParameterPanel, ExportDialog
└── utils/      FileUtils, ImageUtils, DDSUtils
```

## Roadmap

- [x] Project scaffold and build system
- [ ] Texture import (PNG/DDS) into PBR slots
- [ ] RMAOS channel packing from individual maps
- [ ] PBR feature toggles and parameter editing
- [ ] Texture preview with zoom/pan
- [ ] Export DDS + PGPatcher JSON to mod folder
- [ ] Batch import with suffix auto-detection
- [ ] DDS compression format selection
- [ ] Project save/load

## References

- [True PBR Specification](https://github.com/doodlum/skyrim-community-shaders/wiki/True-PBR)
- [PGPatcher Mod Authors Guide](https://github.com/hakasapl/PGPatcher/wiki/Mod-Authors)
- [Community Shaders](https://github.com/doodlum/skyrim-community-shaders)

## License

This project is licensed under the [Creative Commons Attribution-NonCommercial 4.0 International License (CC BY-NC 4.0)](https://creativecommons.org/licenses/by-nc/4.0/).

You are free to use, modify, and share this software for non-commercial purposes with attribution. See [LICENSE](LICENSE) for details.
