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
)

set(APP_SOURCES
    app/MainWindow.h
    app/MainWindow.cpp
    app/Application.h
    app/Application.cpp
)

set(UTIL_SOURCES
    utils/FileUtils.h
    utils/FileUtils.cpp
    utils/ImageUtils.h
    utils/ImageUtils.cpp
    utils/DDSUtils.h
    utils/DDSUtils.cpp
)

set(ALL_SOURCES
    main.cpp
    ${CORE_SOURCES}
    ${UI_SOURCES}
    ${APP_SOURCES}
    ${UTIL_SOURCES}
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

# --- Qt Runtime Deployment ---
# Copy Qt platform plugins and DLLs next to the executable so it can run.
# This uses Qt's official deployment tool (windeployqt) if available,
# otherwise manually copies the platform plugin.

# Find windeployqt
find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${_qt_bin_dir}" "${Qt6_DIR}/../../../tools/Qt6/bin"
    PATHS ENV PATH
)

if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET TruePBR-Manager POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --no-compiler-runtime
            --no-translations
            --no-opengl-sw
            "$<TARGET_FILE:TruePBR-Manager>"
        COMMENT "Running windeployqt..."
    )
else()
    # Fallback: manually copy the platform plugin
    get_target_property(_qt_core_loc Qt6::Core LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_core_loc}" DIRECTORY)
    get_filename_component(_qt_root "${_qt_bin_dir}" DIRECTORY)

    set(_qt_plugins_src "${_qt_root}/plugins/platforms")
    set(_qt_plugins_dst "$<TARGET_FILE_DIR:TruePBR-Manager>/platforms")

    add_custom_command(TARGET TruePBR-Manager POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_qt_plugins_dst}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt_plugins_src}/qwindows.dll"
            "${_qt_plugins_dst}/qwindows.dll"
        COMMENT "Copying Qt platform plugin (qwindows.dll)..."
    )
endif()
