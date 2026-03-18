find_package(GTest CONFIG REQUIRED)

add_executable(TruePBR-Tests
    test_PBRTextureSet.cpp
    test_TextureSetValidator.cpp
    test_Project.cpp
    test_ModImporter.cpp
    test_VanillaConverter.cpp
)

set_target_properties(TruePBR-Tests PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
)

target_link_libraries(TruePBR-Tests PRIVATE
    TruePBR-Core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(TruePBR-Tests)
