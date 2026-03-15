# Source files
set(CORE_SOURCES
    core/Project.h
    core/Project.cpp
    core/PBRTextureSet.h
    core/PBRTextureSet.cpp
    core/TextureImporter.h
    core/TextureImporter.cpp
    core/ChannelPacker.h
    core/ChannelPacker.cpp
    core/JsonExporter.h
    core/JsonExporter.cpp
    core/ModExporter.h
    core/ModExporter.cpp
    core/LandscapeExporter.h
    core/LandscapeExporter.cpp
    core/TextureSetValidator.h
    core/TextureSetValidator.cpp
)

set(UI_SOURCES
    ui/TexturePreviewWidget.h
    ui/TexturePreviewWidget.cpp
    ui/TextureSetPanel.h
    ui/TextureSetPanel.cpp
    ui/SlotEditorWidget.h
    ui/SlotEditorWidget.cpp
    ui/FeatureTogglePanel.h
    ui/FeatureTogglePanel.cpp
    ui/ParameterPanel.h
    ui/ParameterPanel.cpp
    ui/ExportDialog.h
    ui/ExportDialog.cpp
    ui/ImportDialog.h
    ui/ImportDialog.cpp
    ui/MaterialPreviewWidget.h
    ui/MaterialPreviewWidget.cpp
)

set(APP_SOURCES
    app/MainWindow.h
    app/MainWindow.cpp
    app/Application.h
    app/Application.cpp
)

set(UTIL_SOURCES
    utils/Log.h
    utils/Log.cpp
    utils/FileUtils.h
    utils/FileUtils.cpp
    utils/ImageUtils.h
    utils/ImageUtils.cpp
    utils/DDSUtils.h
    utils/DDSUtils.cpp
)

set(RENDERER_SOURCES
    renderer/D3D12Renderer.h
    renderer/D3D12Renderer.cpp
    renderer/D3D12UploadQueue.h
    renderer/D3D12UploadQueue.cpp
    renderer/DescriptorHeap.h
    renderer/DescriptorHeap.cpp
    renderer/IBLPipeline.h
    renderer/IBLPipeline.cpp
    renderer/MeshGenerator.h
    renderer/MeshGenerator.cpp
    renderer/SkyboxShader.hlsl
    renderer/PBRShader.hlsl
    renderer/Common/Math.hlsli
    renderer/Common/BRDF.hlsli
    renderer/Common/Shading.hlsli
    renderer/Common/PBRMath.hlsli
    renderer/Common/PBR.hlsli
    renderer/Common/Random.hlsli
    renderer/Common/Glints/Glints2023.hlsli
    renderer/Common/GT7ToneMap.hlsli
    renderer/ToneMapShader.hlsl
    renderer/TAAResolve.hlsl
)

# Prevent Visual Studio from compiling HLSL files with its built-in FXC (we compile via CMake custom commands)
set_source_files_properties(
    renderer/SkyboxShader.hlsl
    renderer/PBRShader.hlsl
    renderer/Common/Math.hlsli
    renderer/Common/BRDF.hlsli
    renderer/Common/Shading.hlsli
    renderer/Common/PBRMath.hlsli
    renderer/Common/PBR.hlsli
    renderer/Common/Random.hlsli
    renderer/Common/Glints/Glints2023.hlsli
    renderer/Common/GT7ToneMap.hlsli
    renderer/ToneMapShader.hlsl
    renderer/TAAResolve.hlsl
    PROPERTIES VS_TOOL_OVERRIDE "None"
)

set(ALL_SOURCES
    main.cpp
    ${CORE_SOURCES}
    ${UI_SOURCES}
    ${APP_SOURCES}
    ${UTIL_SOURCES}
    ${RENDERER_SOURCES}
    third_party/tinyexr_impl.cpp
    third_party/stb_image_write_impl.cpp
)

# Qt resources
set(RESOURCES
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)

add_executable(TruePBR-Manager WIN32
    ${ALL_SOURCES}
    ${RESOURCES}
)

# Find packages
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(Stb REQUIRED)

target_include_directories(TruePBR-Manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Stb_INCLUDE_DIR}
)

target_link_libraries(TruePBR-Manager PRIVATE
    Qt6::Widgets
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    Microsoft::DirectXTex
    d3d12.lib
    dxgi.lib
)

# Windows-specific: embed manifest, set subsystem
if(WIN32)
    target_compile_definitions(TruePBR-Manager PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )
endif()

# --- Output directly to dist/ ---
# Put exe and DLLs straight into dist/TruePBR-Manager/ so there's no
# need to copy anything after the build. Just zip that folder.

set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist/TruePBR-Manager")

# Transitive DLLs that Qt links at runtime but doesn't expose via
# INTERFACE_LINK_LIBRARIES (so TARGET_RUNTIME_DLLS misses them, and
# windeployqt doesn't deploy them either).
get_target_property(_qt_core_dll Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT _qt_core_dll)
    get_target_property(_qt_core_dll Qt6::Core IMPORTED_LOCATION)
endif()
get_filename_component(_VCPKG_BIN_DIR "${_qt_core_dll}" DIRECTORY)

set(_QT_TRANSITIVE_DLLS
    "${_VCPKG_BIN_DIR}/double-conversion.dll"
    "${_VCPKG_BIN_DIR}/libpng16.dll"
    "${_VCPKG_BIN_DIR}/md4c.dll"
    "${_VCPKG_BIN_DIR}/pcre2-16.dll"
    "${_VCPKG_BIN_DIR}/zlib1.dll"
)

set_target_properties(TruePBR-Manager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${DIST_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${DIST_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${DIST_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${DIST_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${DIST_DIR}"
)

# Qt and vcpkg DLLs: CMake copies dependent DLLs next to the exe automatically
# when this generator expression is used (CMake 3.21+, Ninja/VS):
if(WIN32)
    add_custom_command(TARGET TruePBR-Manager POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:TruePBR-Manager>
            "${DIST_DIR}"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs to dist/..."
    )
    # Qt's transitive DLLs (not captured by TARGET_RUNTIME_DLLS)
    add_custom_command(TARGET TruePBR-Manager POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_QT_TRANSITIVE_DLLS}
            "${DIST_DIR}"
        COMMENT "Copying Qt transitive DLLs to dist/..."
    )
endif()

# Qt platform plugins (windeployqt handles plugins, styles, imageformats)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${Qt6_DIR}/../../../tools/Qt6/bin"
    PATHS ENV PATH
)

if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET TruePBR-Manager POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --no-compiler-runtime
            --no-translations
            --no-opengl-sw
            "$<TARGET_FILE:TruePBR-Manager>"
        COMMENT "Running windeployqt in dist/..."
    )
else()
    # Fallback: manually copy the platform plugin
    get_target_property(_qt_core_loc Qt6::Core LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_core_loc}" DIRECTORY)
    get_filename_component(_qt_root "${_qt_bin_dir}" DIRECTORY)

    add_custom_command(TARGET TruePBR-Manager POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt_root}/plugins/platforms/qwindows.dll"
            "${DIST_DIR}/platforms/qwindows.dll"
        COMMENT "Copying Qt platform plugin to dist/..."
    )
endif()

# Copy LICENSE
add_custom_command(TARGET TruePBR-Manager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${DIST_DIR}/LICENSE"
    COMMENT "Copying LICENSE to dist/..."
)

# ── Offline HLSL → CSO compilation via FXC ─────────────────────────────
# Compile shaders at build time so the app loads pre-compiled bytecode
# instead of calling D3DCompileFromFile at every startup.

find_program(DXC_EXECUTABLE dxc
    DOC "DirectX Shader Compiler (DXC)"
    HINTS "$ENV{VULKAN_SDK}/Bin"
)
if(NOT DXC_EXECUTABLE)
    # Try common Windows SDK locations
    file(GLOB _SDK_BIN_DIRS "C:/Program Files (x86)/Windows Kits/10/bin/10.*/x64")
    if(_SDK_BIN_DIRS)
        list(SORT _SDK_BIN_DIRS ORDER DESCENDING)
        find_program(DXC_EXECUTABLE dxc HINTS ${_SDK_BIN_DIRS})
    endif()
endif()
if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "dxc.exe not found. Install the Vulkan SDK, Windows SDK, or set DXC_EXECUTABLE.")
endif()
message(STATUS "Found DXC: ${DXC_EXECUTABLE}")

set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/renderer")
set(SHADER_OUT_DIR "${DIST_DIR}/shaders")

# All .hlsli include files (used as DEPENDS for shaders that #include them)
set(HLSLI_DEPS
    "${SHADER_SRC_DIR}/Common/Math.hlsli"
    "${SHADER_SRC_DIR}/Common/BRDF.hlsli"
    "${SHADER_SRC_DIR}/Common/Shading.hlsli"
    "${SHADER_SRC_DIR}/Common/PBRMath.hlsli"
    "${SHADER_SRC_DIR}/Common/PBR.hlsli"
    "${SHADER_SRC_DIR}/Common/Random.hlsli"
    "${SHADER_SRC_DIR}/Common/Glints/Glints2023.hlsli"
    "${SHADER_SRC_DIR}/Common/GT7ToneMap.hlsli"
)

# Helper function: compile one HLSL entry point to a .cso file
# Usage: compile_hlsl(<source.hlsl> <EntryPoint> <profile> <output.cso> [INCLUDE_DIR dir] [DEPENDS ...])
function(compile_hlsl SOURCE ENTRY PROFILE OUTPUT)
    cmake_parse_arguments(ARG "" "INCLUDE_DIR" "DEPENDS" ${ARGN})
    set(_src "${SHADER_SRC_DIR}/${SOURCE}")
    set(_dst "${SHADER_OUT_DIR}/${OUTPUT}")

    set(_include_args)
    if(ARG_INCLUDE_DIR)
        set(_include_args /I "${ARG_INCLUDE_DIR}")
    endif()

    add_custom_command(
        OUTPUT "${_dst}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
        COMMAND ${DXC_EXECUTABLE}
            $<$<CONFIG:Debug>:-Zi>
            $<$<CONFIG:Debug>:-Od>
            $<$<NOT:$<CONFIG:Debug>>:-O1>
            -T ${PROFILE}
            -E ${ENTRY}
            ${_include_args}
            -Fo "${_dst}"
            "${_src}"
        DEPENDS "${_src}" ${ARG_DEPENDS}
        COMMENT "DXC: ${SOURCE} [${ENTRY}] -> ${OUTPUT}"
        VERBATIM
    )
endfunction()

# ── Graphics shaders (PBRShader needs include path for Common/) ─────────
compile_hlsl(PBRShader.hlsl   VSMain   vs_6_0  PBRShader_VS.cso
    INCLUDE_DIR "${SHADER_SRC_DIR}"  DEPENDS ${HLSLI_DEPS})
compile_hlsl(PBRShader.hlsl   PSMain   ps_6_0  PBRShader_PS.cso
    INCLUDE_DIR "${SHADER_SRC_DIR}"  DEPENDS ${HLSLI_DEPS})
compile_hlsl(SkyboxShader.hlsl  SkyboxVS  vs_6_0  SkyboxShader_VS.cso
    INCLUDE_DIR "${SHADER_SRC_DIR}"  DEPENDS ${HLSLI_DEPS})
compile_hlsl(SkyboxShader.hlsl  SkyboxPS  ps_6_0  SkyboxShader_PS.cso
    INCLUDE_DIR "${SHADER_SRC_DIR}"  DEPENDS ${HLSLI_DEPS})

# ── Post-process shaders (ToneMap, TAA) ──────────────────────────────────
compile_hlsl(ToneMapShader.hlsl  ToneMapVS  vs_6_0  ToneMapShader_VS.cso
    INCLUDE_DIR "${SHADER_SRC_DIR}"  DEPENDS ${HLSLI_DEPS})
compile_hlsl(ToneMapShader.hlsl  ToneMapPS  ps_6_0  ToneMapShader_PS.cso
    INCLUDE_DIR "${SHADER_SRC_DIR}"  DEPENDS ${HLSLI_DEPS})
compile_hlsl(TAAResolve.hlsl     CSMain     cs_6_0  TAAResolve.cso)

# ── Compute shaders (IBL – no includes) ─────────────────────────────────
compile_hlsl(IBLEquirectToCube.hlsl     CSMain  cs_6_0  IBLEquirectToCube.cso)
compile_hlsl(IBLPrefilter.hlsl          CSMain  cs_6_0  IBLPrefilter.cso)
compile_hlsl(IBLBrdfLut.hlsl            CSMain  cs_6_0  IBLBrdfLut.cso)
compile_hlsl(IBLDiffuseIrradiance.hlsl  CSMain  cs_6_0  IBLDiffuseIrradiance.cso)
compile_hlsl(IBLCubemapMipGen.hlsl      CSMain  cs_6_0  IBLCubemapMipGen.cso)

set(COMPILED_SHADER_OUTPUTS
    "${SHADER_OUT_DIR}/PBRShader_VS.cso"
    "${SHADER_OUT_DIR}/PBRShader_PS.cso"
    "${SHADER_OUT_DIR}/SkyboxShader_VS.cso"
    "${SHADER_OUT_DIR}/SkyboxShader_PS.cso"
    "${SHADER_OUT_DIR}/ToneMapShader_VS.cso"
    "${SHADER_OUT_DIR}/ToneMapShader_PS.cso"
    "${SHADER_OUT_DIR}/TAAResolve.cso"
    "${SHADER_OUT_DIR}/IBLEquirectToCube.cso"
    "${SHADER_OUT_DIR}/IBLPrefilter.cso"
    "${SHADER_OUT_DIR}/IBLBrdfLut.cso"
    "${SHADER_OUT_DIR}/IBLDiffuseIrradiance.cso"
    "${SHADER_OUT_DIR}/IBLCubemapMipGen.cso"
)

add_custom_target(compile_shaders ALL DEPENDS ${COMPILED_SHADER_OUTPUTS})
add_dependencies(TruePBR-Manager compile_shaders)

# Create hdris directory in dist (user places HDRI files here)
add_custom_command(TARGET TruePBR-Manager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}/hdris"
    COMMENT "Creating hdris directory in dist/..."
)

# Copy built-in HDRIs if they exist in the source tree
if(EXISTS "${CMAKE_SOURCE_DIR}/resources/HDRIs")
    add_custom_command(TARGET TruePBR-Manager POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/HDRIs"
            "${DIST_DIR}/hdris"
        COMMENT "Copying built-in HDRIs to dist/..."
    )
endif()

# --- Optional: pack dist into a zip ---
option(TRUEPBR_PACK "Create a zip archive of the dist folder after build" OFF)

if(TRUEPBR_PACK)
    set(PACK_DIR "${CMAKE_SOURCE_DIR}/dist/package")
    set(PACK_NAME "TruePBR-Manager-${PROJECT_VERSION}")
    set(PACK_STAGING_BASE "${CMAKE_BINARY_DIR}/_pack_staging")
    set(STAGING_DIR "${PACK_STAGING_BASE}/TruePBR-Manager")

    # Ensure the staging base directory exists at configure time
    # so WORKING_DIRECTORY is valid on the very first build
    file(MAKE_DIRECTORY "${PACK_STAGING_BASE}")

    # Collect optional commands (windeployqt, HDRIs)
    set(_PACK_EXTRA_COMMANDS)

    if(WINDEPLOYQT_EXECUTABLE)
        list(APPEND _PACK_EXTRA_COMMANDS
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-compiler-runtime
                --no-translations
                --no-opengl-sw
                "${STAGING_DIR}/TruePBR-Manager.exe"
        )
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/HDRIs")
        list(APPEND _PACK_EXTRA_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/resources/HDRIs"
                "${STAGING_DIR}/hdris"
        )
    endif()

    add_custom_target(pack ALL
        # 1. Clean and recreate staging
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${STAGING_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGING_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGING_DIR}/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGING_DIR}/hdris"
        # 2. Copy exe
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:TruePBR-Manager>"
            "${STAGING_DIR}/"
        # 3. Copy runtime DLLs (direct link dependencies)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:TruePBR-Manager>
            "${STAGING_DIR}/"
        # 4. Qt transitive DLLs (not captured by TARGET_RUNTIME_DLLS)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_QT_TRANSITIVE_DLLS}
            "${STAGING_DIR}/"
        # 5. Copy LICENSE
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/LICENSE"
            "${STAGING_DIR}/LICENSE"
        # 6. Copy compiled shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DIST_DIR}/shaders"
            "${STAGING_DIR}/shaders"
        # 7. Optional: windeployqt + HDRIs
        ${_PACK_EXTRA_COMMANDS}
        # 8. Pack
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PACK_DIR}"
        COMMAND ${CMAKE_COMMAND} -E tar cf "${PACK_DIR}/${PACK_NAME}.zip" --format=zip -- "TruePBR-Manager"
        WORKING_DIRECTORY "${PACK_STAGING_BASE}"
        COMMAND_EXPAND_LISTS
        COMMENT "Packing into ${PACK_DIR}/${PACK_NAME}.zip ..."
    )
    add_dependencies(pack TruePBR-Manager compile_shaders)
endif()
