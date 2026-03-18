#include <gtest/gtest.h>

#include "core/VanillaConverter.h"

using namespace tpbr;

// ─── ShininessToRoughness conversion tests ─────────────────────────

TEST(VanillaConverterTest, ShininessToRoughness_Zero)
{
    // shininess=0 → fully rough → 255
    EXPECT_EQ(VanillaConverter::shininessToRoughness(0.0f), 255);
}

TEST(VanillaConverterTest, ShininessToRoughness_100)
{
    // pow(2/102, 0.25) * 255 ≈ 97
    uint8_t result = VanillaConverter::shininessToRoughness(100.0f);
    EXPECT_NEAR(result, 97, 1); // ±1 tolerance
}

TEST(VanillaConverterTest, ShininessToRoughness_10000)
{
    // Very high shininess → near 0 (very smooth/metallic)
    uint8_t result = VanillaConverter::shininessToRoughness(10000.0f);
    EXPECT_NEAR(result, 0, 5); // Should be very small
}

TEST(VanillaConverterTest, ShininessToRoughness_Negative)
{
    // Negative shininess → clamp to 255
    EXPECT_EQ(VanillaConverter::shininessToRoughness(-10.0f), 255);
}

// ─── sRGB ↔ Linear conversion tests ────────────────────────────────

TEST(VanillaConverterTest, SrgbToLinear_Black)
{
    EXPECT_NEAR(VanillaConverter::srgbToLinear(0), 0.0f, 0.001f);
}

TEST(VanillaConverterTest, SrgbToLinear_White)
{
    EXPECT_NEAR(VanillaConverter::srgbToLinear(255), 1.0f, 0.001f);
}

TEST(VanillaConverterTest, SrgbToLinear_Mid)
{
    // 128 sRGB ≈ 0.216 linear
    EXPECT_NEAR(VanillaConverter::srgbToLinear(128), 0.216f, 0.01f);
}

TEST(VanillaConverterTest, LinearToSrgb_Black)
{
    EXPECT_NEAR(VanillaConverter::linearToSrgb(0.0f), 0, 1);
}

TEST(VanillaConverterTest, LinearToSrgb_White)
{
    EXPECT_NEAR(VanillaConverter::linearToSrgb(1.0f), 255, 1);
}

TEST(VanillaConverterTest, LinearToSrgb_Roundtrip)
{
    // Test roundtrip: linearToSrgb(srgbToLinear(x)) ≈ x (±1 for rounding)
    uint8_t testValues[] = {0, 1, 64, 128, 192, 254, 255};
    for (uint8_t x : testValues)
    {
        float linear = VanillaConverter::srgbToLinear(x);
        uint8_t roundtrip = VanillaConverter::linearToSrgb(linear);
        EXPECT_NEAR(roundtrip, x, 1) << "Roundtrip failed for sRGB value " << (int)x;
    }
}

// ─── Gamma/Brightness adjustment tests ────────────────────────────

TEST(VanillaConverterTest, ApplyGammaBrightness_Identity)
{
    // 2x2 red image with 100% alpha
    std::vector<uint8_t> rgba = {
        255, 0, 0, 255,   // Red pixel, alpha=255
        255, 0, 0, 255,
        255, 0, 0, 255,
        255, 0, 0, 255
    };
    std::vector<uint8_t> original = rgba;

    // Apply identity transformation: gamma=1.0, brightness=0.0
    VanillaConverter::applyGammaBrightness(rgba.data(), 2, 2, 1.0f, 0.0f);

    // Should be unchanged
    for (size_t i = 0; i < rgba.size(); ++i)
    {
        EXPECT_EQ(rgba[i], original[i]) << "Pixel data changed at index " << i;
    }
}

TEST(VanillaConverterTest, ApplyGammaBrightness_AlphaPreserved)
{
    // Verify that alpha channel is never modified
    std::vector<uint8_t> rgba = {
        100, 50, 25, 64,   // Various alpha values
        200, 150, 75, 192
    };
    std::vector<uint8_t> originalAlpha = {rgba[3], rgba[7]};

    VanillaConverter::applyGammaBrightness(rgba.data(), 2, 1, 2.0f, 0.5f);

    // Alpha values must remain unchanged
    EXPECT_EQ(rgba[3], originalAlpha[0]) << "First pixel alpha was modified";
    EXPECT_EQ(rgba[7], originalAlpha[1]) << "Second pixel alpha was modified";
}

// ─── Extract Alpha Channel tests ───────────────────────────────────

TEST(VanillaConverterTest, ExtractAlphaChannel)
{
    // 2x2 RGBA with known alphas: 0, 128, 255, 64
    std::vector<uint8_t> rgba = {
        255, 0, 0, 0,        // Red, alpha=0
        0, 255, 0, 128,      // Green, alpha=128
        0, 0, 255, 255,      // Blue, alpha=255
        128, 128, 128, 64    // Gray, alpha=64
    };

    auto alpha = VanillaConverter::extractAlphaChannel(rgba.data(), 2, 2);

    EXPECT_EQ(alpha.size(), 4);
    EXPECT_EQ(alpha[0], 0);
    EXPECT_EQ(alpha[1], 128);
    EXPECT_EQ(alpha[2], 255);
    EXPECT_EQ(alpha[3], 64);
}

TEST(VanillaConverterTest, ExtractAlphaChannel_AllOnes)
{
    // 1x4 image with all alpha=255
    std::vector<uint8_t> rgba = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        128, 128, 128, 255
    };

    auto alpha = VanillaConverter::extractAlphaChannel(rgba.data(), 1, 4);

    EXPECT_EQ(alpha.size(), 4);
    for (uint8_t val : alpha)
    {
        EXPECT_EQ(val, 255);
    }
}

// ─── ValidateInput tests ──────────────────────────────────────────

TEST(VanillaConverterTest, ValidateInput_MissingDiffuse)
{
    VanillaConversionInput input;
    input.inputFiles[VanillaTextureType::Normal] = "/path/to/normal.dds";
    input.textureSetName = "TestSet";
    input.vanillaMatchPath = "architecture/whiterun/wrwoodplank01";

    std::vector<ImportDiagnostic> diags;
    bool valid = VanillaConverter::validateInput(input, diags);

    EXPECT_FALSE(valid) << "Should fail when Diffuse is missing";
    EXPECT_FALSE(diags.empty()) << "Should produce diagnostic messages";

    // Check for error diagnostic about missing Diffuse
    bool foundDiffuseError = false;
    for (const auto& diag : diags)
    {
        if (diag.severity == DiagnosticSeverity::Error && diag.message.find("Diffuse") != std::string::npos)
        {
            foundDiffuseError = true;
            break;
        }
    }
    EXPECT_TRUE(foundDiffuseError) << "Should report Diffuse as missing";
}

TEST(VanillaConverterTest, ValidateInput_MissingNormal)
{
    VanillaConversionInput input;
    input.inputFiles[VanillaTextureType::Diffuse] = "/path/to/diffuse.dds";
    input.textureSetName = "TestSet";
    input.vanillaMatchPath = "architecture/whiterun/wrwoodplank01";

    std::vector<ImportDiagnostic> diags;
    bool valid = VanillaConverter::validateInput(input, diags);

    EXPECT_FALSE(valid) << "Should fail when Normal is missing";
}

TEST(VanillaConverterTest, ValidateInput_MinimumValid)
{
    VanillaConversionInput input;
    input.inputFiles[VanillaTextureType::Diffuse] = "/path/to/diffuse.dds";
    input.inputFiles[VanillaTextureType::Normal] = "/path/to/normal.dds";
    input.textureSetName = "TestSet";
    input.vanillaMatchPath = "architecture/whiterun/wrwoodplank01";

    std::vector<ImportDiagnostic> diags;
    bool valid = VanillaConverter::validateInput(input, diags);

    EXPECT_TRUE(valid) << "Should pass with Diffuse + Normal";
}

TEST(VanillaConverterTest, ValidateInput_AllOptionalPresent)
{
    VanillaConversionInput input;
    input.inputFiles[VanillaTextureType::Diffuse] = "/path/to/diffuse.dds";
    input.inputFiles[VanillaTextureType::Normal] = "/path/to/normal.dds";
    input.inputFiles[VanillaTextureType::Specular] = "/path/to/specular.dds";
    input.inputFiles[VanillaTextureType::EnvMask] = "/path/to/envmask.dds";
    input.inputFiles[VanillaTextureType::Cubemap] = "/path/to/cubemap.dds";
    input.inputFiles[VanillaTextureType::Glow] = "/path/to/glow.dds";
    input.inputFiles[VanillaTextureType::Parallax] = "/path/to/parallax.dds";
    input.inputFiles[VanillaTextureType::BackLight] = "/path/to/backlight.dds";
    input.textureSetName = "TestSet";
    input.vanillaMatchPath = "architecture/whiterun/wrwoodplank01";

    std::vector<ImportDiagnostic> diags;
    bool valid = VanillaConverter::validateInput(input, diags);

    EXPECT_TRUE(valid) << "Should pass with all textures present";
}
