find_package(GTest CONFIG REQUIRED)

add_executable(TruePBR-Tests
    test_PBRTextureSet.cpp
    test_TextureSetValidator.cpp
    test_Project.cpp
)

target_link_libraries(TruePBR-Tests PRIVATE
    TruePBR-Core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(TruePBR-Tests)
