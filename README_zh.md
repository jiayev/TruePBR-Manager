[English](README.md) | **中文**

# TruePBR Manager

[![CI](https://github.com/jiayev/TruePBR-Manager/actions/workflows/ci.yml/badge.svg)](https://github.com/jiayev/TruePBR-Manager/actions/workflows/ci.yml)
[![Release](https://github.com/jiayev/TruePBR-Manager/actions/workflows/release.yml/badge.svg)](https://github.com/jiayev/TruePBR-Manager/actions/workflows/release.yml)

TruePBR Manager 是一个 Windows 桌面工具，用于组装和导出与 [Community Shaders True PBR](https://github.com/doodlum/skyrim-community-shaders/wiki/True-PBR) 和 [PGPatcher](https://github.com/hakasapl/PGPatcher) 兼容的 Skyrim True PBR 贴图集。

每个贴图集对应一个原版漫反射路径，并存储生成 mod-ready True PBR 包所需的贴图、特性标志、参数和导出设置。

## 当前状态

应用程序目前支持：

- 项目保存/加载（`.tpbr` JSON），多贴图集管理
- 拖放贴图导入（DDS、PNG、TGA、BMP、JPG），每个槽位支持导入/清除
- RMAOS 编辑：支持打包或分通道，导出时自动打包
- 完整的 True PBR 特性编辑：自发光、视差、次表面散射、涂层、绒毛、闪光、毛发等
- 每个槽位的 DDS 压缩格式、导出尺寸覆盖和槽位路径覆盖
- 2D 预览：支持缩放、平移和通道隔离
- 3D 材质预览（D3D12 Cook-Torrance PBR）：基于 HDRI 的 IBL、天空盒、TAA、HDR 输出、GT7 色调映射、ACEScg 管线
- 3D 调试通道可视化：法线、粗糙度、金属度、环境光遮蔽、高光
- 完整的 Mipmap 生成，支持可调节的 Mip LOD 偏移
- 按文件名后缀批量导入
- 导入现有 PBR Mod：从 mod 目录读取 PGPatcher JSON 并解析 DDS 贴图以重建项目
- 导出前验证（分辨率、缺失槽位、冲突检测）
- 地形贴图集支持（TXST EDID）
- PGPatcher JSON 和 DDS 贴图导出
- 导出在后台线程运行，带进度对话框（可取消）
- 重新导出时跳过未更改的贴图（时间戳 + 格式检查）
- 原版贴图集转换：将 Blinn-Phong 贴图（Diffuse、Normal、Specular、EnvMask、Cubemap、Glow、Parallax、BackLight）转换为 True PBR，支持自动 RMAOS 生成、立方体贴图金属色调、逐贴图伽马/亮度调节和实时输出预览
- 转换在后台线程运行，带进度对话框（可取消）
- 最近项目列表（文件菜单，最多 10 个条目）
- 3D 预览设置跨会话持久化（灯光、HDRI、渲染标志等）
- 本地化：自动检测系统语言，支持运行时语言切换，开发期间热重载翻译文件

应用程序目前不提供撤销/重做功能。

## 贴图槽位

| 槽位 | 后缀 | 内容 | 备注 |
|------|------|------|------|
| 漫反射 | `.dds` | 基础色（RGB）+ 不透明度（A） | 必需 |
| 法线 | `_n.dds` | 法线贴图（RGB） | 必需 |
| RMAOS | `_rmaos.dds` | 粗糙度（R）金属度（G）环境光遮蔽（B）高光（A） | 必需，可导入打包格式或从分通道构建 |
| 自发光 | `_g.dds` | 发光颜色 | 可选 |
| 位移 | `_p.dds` | 高度 / 视差 | 可选 |
| 次表面 | `_s.dds` | 次表面颜色（RGB）+ 不透明度（A） | 与涂层颜色共享 TX07 |
| 涂层法线粗糙度 | `_cnr.dds` | 涂层法线（RGB）+ 涂层粗糙度（A） | 与绒毛共享 TX06 |
| 绒毛 | `_f.dds` | 绒毛颜色（RGB）+ 绒毛遮罩（A） | 与涂层法线共享 TX06 |
| 涂层颜色 | `_s.dds` | 涂层颜色（RGB）+ 强度（A） | 与次表面共享 TX07 |

## 典型工作流程

1. 创建项目并添加一个或多个贴图集。
2. 为每个贴图集设置原版匹配路径，例如 `architecture\whiterun\wrwoodplank01`。
3. 导入必需的槽位以及所启用特性集所需的任何可选贴图。
4. 对于 RMAOS，选择 `打包 RMAOS` 或 `分通道`。
5. 调整材质参数和每个槽位的导出压缩设置。
6. 导出到 mod 文件夹以生成 DDS 文件和 `PBRNIFPatcher/<project>.json`。

## 导出布局

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

匹配路径示例：

```text
architecture\whiterun\wrwoodplank01
```

导出到：

```text
textures/pbr/architecture/whiterun/
    wrwoodplank01.dds
    wrwoodplank01_n.dds
    wrwoodplank01_rmaos.dds
```

## 构建

## Pre-commit

仓库包含专注于自动格式化和基本文本清理的 pre-commit 配置：

- `clang-format` 用于 `src/` 下的 C 和 C++ 源文件
- `end-of-file-fixer` 用于规范化文件末尾换行
- `trailing-whitespace` 用于清除尾部空格，同时保留 Markdown 硬换行
- `check-yaml`、`check-merge-conflict`、`check-case-conflict` 和 `check-illegal-windows-names`

如果你使用 `prek`，请通过以下命令安装钩子脚本：

```powershell
prek install
```

一次性格式化和规范化整个仓库：

```powershell
prek run --all-files
```

如果你使用标准的 `pre-commit` CLI，等效命令为：

```powershell
pre-commit install
pre-commit run --all-files
```

## GitHub Actions

仓库包含两个 GitHub Actions 工作流：

- [`CI`](https://github.com/jiayev/TruePBR-Manager/actions/workflows/ci.yml)：在每次 pull request 和推送到 `main` 时运行 `prek`，然后在 `windows-latest` 上构建应用程序
- [`Release`](https://github.com/jiayev/TruePBR-Manager/actions/workflows/release.yml)：在匹配 `v*` 的标签上触发，构建可分发版本，压缩 `dist/TruePBR-Manager/`，并上传到 GitHub Release 页面

本项目推荐的基线配置：

- `CI` 用于格式化、lint 风格检查和构建验证
- `Release` 用于标签打包和可下载的二进制文件
- 单元测试可通过 `cmake --preset test` 在本地运行

### 环境要求

- Windows x64
- Visual Studio 2022 或更新版本，需安装桌面 C++ 工作负载
- CMake 3.21 或更新版本
- vcpkg，且设置了 `VCPKG_ROOT` 环境变量

### 快速构建

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
build.bat            # Release 构建（默认）
build.bat debug      # Debug 构建
build.bat release    # 显式 Release 构建
```

该脚本在 `build/` 中配置项目，并将可分发版本构建到 `dist/TruePBR-Manager/`。该文件夹还包含所需的运行时 DLL、Qt 平台插件、预编译着色器和 `LICENSE`。

### CMake 预设

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset default
cmake --build --preset release
```

可用的配置预设：

- `default` 用于 Release
- `debug` 用于 Debug
- `test` 用于带单元测试的 Debug

### 运行测试

```powershell
cmake --preset test
cmake --build --preset test
ctest --preset test
```

### 手动 CMake

```powershell
cmake -S . -B build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake `
    -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build
```

## 依赖

通过 vcpkg 管理：

| 库 | 用途 |
|---|------|
| Qt 6 Widgets | 桌面 UI |
| DirectXTex | DDS 元数据、解码和编码 |
| nlohmann/json | 项目和导出 JSON 序列化 |
| spdlog | 日志记录 |
| stb_image | 光栅图像加载 |
| Google Test | 单元测试（可选，仅 test 预设） |

额外依赖：

| 库 | 用途 | 来源 |
|---|------|------|
| tinyexr | HDRI 用 EXR 图像加载 | 内置（v1.0.9） |
| D3D12 / DXGI | GPU 渲染（3D 预览） | Windows SDK |

## 项目结构

```text
src/
├── app/        主窗口和应用程序外壳
├── core/       项目模型、贴图导入、RMAOS 打包、JSON 导出、mod 导出、mod 导入、原版转换、地形导出、翻译管理器、应用设置
├── renderer/   D3D12 GPU 后端、IBL 管线、网格生成、HLSL 着色器
├── ui/         贴图集列表、槽位编辑器、特性开关、参数编辑器、原版转换对话框、3D 预览部件
└── utils/      DDS 工具、图像加载、文件工具、日志
tests/          单元测试（Google Test）：PBRTextureSet、TextureSetValidator、Project、VanillaConverter
translations/   JSON 翻译文件（en.json、zh_CN.json 等）
```

## 实现说明

- 项目以 JSON 格式保存，扩展名为 `.tpbr`。
- RMAOS 源模式以打包或分通道模式持久化存储于每个贴图集。
- 导出压缩格式可按槽位覆盖，并存储在项目文件中。
- 导出尺寸可按槽位覆盖（2 的幂次缩小）。
- 槽位路径覆盖允许每个贴图集自定义 PGPatcher `slotN` 导出路径。
- 导入时检测 Alpha 模式，并影响可用的压缩选项（BC1 要求无 Alpha）。
- 翻译使用由 `TranslationManager` 加载的自定义 JSON 格式。将 `<locale>.json` 文件放在可执行文件旁的 `translations/` 目录中。应用程序自动检测系统语言并支持通过菜单栏运行时切换。
- 应用程序级设置（语言、窗口几何、上次项目目录）通过 `AppSettings` 持久化到可执行文件旁的 `TruePBR-Manager.ini` 中。
- 已匹配目标压缩格式和 Mipmap 数量的源 DDS 文件将直接复制，不进行重新编码。
- 2D 预览默认显示漫反射贴图；点击任何槽位可预览该贴图并进行通道隔离。
- 3D 预览使用重写的 D3D12 渲染器，具有双缓冲帧、异步贴图上传队列和 GPU 计算 IBL 管线。
- 着色器在构建时预编译为 `.cso`；渲染器全程使用 ACEScg 色彩空间。
- PGPatcher JSON 在贴图集名称与原版文件名不同时使用 `rename`，仅在生成路径与 PGPatcher 基于约定的推断不同时才输出显式 `slotN` 路径。

## 路线图

计划功能：

- [x] 内置原版贴图集转换
- [x] 导入现有 PBR Mod（读取包含 PGPatcher JSON + 贴图的 mod 目录，重建项目）
- [ ] 撤销/重做

## 参考资料

- [True PBR 规范](https://github.com/doodlum/skyrim-community-shaders/wiki/True-PBR)
- [PGPatcher Mod 作者指南](https://github.com/hakasapl/PGPatcher/wiki/Mod-Authors)
- [Community Shaders](https://github.com/doodlum/skyrim-community-shaders)

## 许可证

本项目采用 [知识共享 署名-非商业性使用 4.0 国际许可协议（CC BY-NC 4.0）](https://creativecommons.org/licenses/by-nc/4.0/) 授权。

详见 [LICENSE](LICENSE)。
