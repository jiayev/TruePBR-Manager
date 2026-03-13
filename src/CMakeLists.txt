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
    renderer/IBLProcessor.h
    renderer/IBLProcessor.cpp
    renderer/SkyboxShader.hlsl
    renderer/PBRShader.hlsl
    renderer/Common/Math.hlsli
    renderer/Common/BRDF.hlsli
    renderer/Common/Shading.hlsli
    renderer/Common/PBRMath.hlsli
    renderer/Common/PBR.hlsli
    renderer/Common/Random.hlsli
    renderer/Common/Glints/Glints2023.hlsli
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
    d3dcompiler.lib
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

set_target_properties(TruePBR-Manager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${DIST_DIR}"
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

# Copy HLSL shaders — file-level dependency tracking so that modifying
# any shader triggers a re-copy on the next build, even when no C++ changed.
set(SHADER_FILES
    PBRShader.hlsl
    IBLEquirectToCube.hlsl
    IBLProjectSH.hlsl
    IBLReduceSH.hlsl
    IBLPrefilter.hlsl
    IBLBrdfLut.hlsl
    SkyboxShader.hlsl
    Common/Math.hlsli
    Common/BRDF.hlsli
    Common/Shading.hlsli
    Common/PBRMath.hlsli
    Common/PBR.hlsli
    Common/Random.hlsli
    Common/Glints/Glints2023.hlsli
)

set(SHADER_OUTPUTS)
foreach(shader IN LISTS SHADER_FILES)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/renderer/${shader}")
    set(_dst "${DIST_DIR}/shaders/${shader}")
    get_filename_component(_dst_dir "${_dst}" DIRECTORY)
    add_custom_command(
        OUTPUT "${_dst}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${_dst}"
        DEPENDS "${_src}"
        COMMENT "Shader: ${shader}"
    )
    list(APPEND SHADER_OUTPUTS "${_dst}")
endforeach()

add_custom_target(copy_shaders ALL DEPENDS ${SHADER_OUTPUTS})
add_dependencies(TruePBR-Manager copy_shaders)

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

    add_custom_target(pack ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PACK_DIR}"
        COMMAND ${CMAKE_COMMAND} -E tar cf "${PACK_DIR}/${PACK_NAME}.zip" --format=zip -- "TruePBR-Manager"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
        COMMENT "Packing dist into ${PACK_DIR}/${PACK_NAME}.zip ..."
    )
    add_dependencies(pack TruePBR-Manager copy_shaders)
endif()
