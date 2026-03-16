#include <gtest/gtest.h>

#include "core/PBRTextureSet.h"

using namespace tpbr;

// ─── slotSuffix ────────────────────────────────────────────

TEST(PBRTextureSet, SlotSuffix_Diffuse)
{
    EXPECT_STREQ(slotSuffix(PBRTextureSlot::Diffuse), ".dds");
}

TEST(PBRTextureSet, SlotSuffix_Normal)
{
    EXPECT_STREQ(slotSuffix(PBRTextureSlot::Normal), "_n.dds");
}

TEST(PBRTextureSet, SlotSuffix_RMAOS)
{
    EXPECT_STREQ(slotSuffix(PBRTextureSlot::RMAOS), "_rmaos.dds");
}

TEST(PBRTextureSet, SlotSuffix_Emissive)
{
    EXPECT_STREQ(slotSuffix(PBRTextureSlot::Emissive), "_g.dds");
}

TEST(PBRTextureSet, SlotSuffix_Displacement)
{
    EXPECT_STREQ(slotSuffix(PBRTextureSlot::Displacement), "_p.dds");
}

// ─── slotDisplayName ───────────────────────────────────────

TEST(PBRTextureSet, SlotDisplayName_NotEmpty)
{
    EXPECT_NE(std::string(slotDisplayName(PBRTextureSlot::Diffuse)), "");
    EXPECT_NE(std::string(slotDisplayName(PBRTextureSlot::Normal)), "");
    EXPECT_NE(std::string(slotDisplayName(PBRTextureSlot::RMAOS)), "");
}

// ─── defaultCompressionForSlot ─────────────────────────────

TEST(PBRTextureSet, DefaultCompression_Diffuse_BC7sRGB)
{
    EXPECT_EQ(defaultCompressionForSlot(PBRTextureSlot::Diffuse), DDSCompressionMode::BC7_sRGB);
}

TEST(PBRTextureSet, DefaultCompression_Normal_BC7Linear)
{
    EXPECT_EQ(defaultCompressionForSlot(PBRTextureSlot::Normal), DDSCompressionMode::BC7_Linear);
}

TEST(PBRTextureSet, DefaultCompression_RMAOS_BC7Linear)
{
    EXPECT_EQ(defaultCompressionForSlot(PBRTextureSlot::RMAOS), DDSCompressionMode::BC7_Linear);
}

TEST(PBRTextureSet, DefaultCompression_Emissive_BC6H)
{
    EXPECT_EQ(defaultCompressionForSlot(PBRTextureSlot::Emissive), DDSCompressionMode::BC6H_UF16);
}

TEST(PBRTextureSet, DefaultCompression_Displacement_BC4)
{
    EXPECT_EQ(defaultCompressionForSlot(PBRTextureSlot::Displacement), DDSCompressionMode::BC4_Linear);
}

// ─── compressionModeKey / tryParseCompressionMode round-trip ──

TEST(PBRTextureSet, CompressionMode_RoundTrip)
{
    DDSCompressionMode modes[] = {DDSCompressionMode::BC7_sRGB,    DDSCompressionMode::BC7_Linear,
                                  DDSCompressionMode::BC3_sRGB,    DDSCompressionMode::BC6H_UF16,
                                  DDSCompressionMode::BC5_Linear,  DDSCompressionMode::BC4_Linear,
                                  DDSCompressionMode::BC1_sRGB,    DDSCompressionMode::BC1_Linear,
                                  DDSCompressionMode::RGBA8_sRGB,  DDSCompressionMode::RGBA8_Linear};

    for (auto mode : modes)
    {
        std::string key = compressionModeKey(mode);
        EXPECT_FALSE(key.empty()) << "Key for mode " << static_cast<int>(mode) << " is empty";

        DDSCompressionMode parsed;
        EXPECT_TRUE(tryParseCompressionMode(key, parsed)) << "Failed to parse key: " << key;
        EXPECT_EQ(parsed, mode) << "Round-trip failed for key: " << key;
    }
}

TEST(PBRTextureSet, CompressionMode_InvalidKey)
{
    DDSCompressionMode mode;
    EXPECT_FALSE(tryParseCompressionMode("invalid_key", mode));
    EXPECT_FALSE(tryParseCompressionMode("", mode));
}

// ─── rmaosSourceModeKey / tryParseRmaosSourceMode round-trip ──

TEST(PBRTextureSet, RMAOSSourceMode_RoundTrip)
{
    RMAOSSourceMode modes[] = {RMAOSSourceMode::PackedTexture, RMAOSSourceMode::SeparateChannels};

    for (auto mode : modes)
    {
        std::string key = rmaosSourceModeKey(mode);
        RMAOSSourceMode parsed;
        EXPECT_TRUE(tryParseRmaosSourceMode(key, parsed));
        EXPECT_EQ(parsed, mode);
    }
}

TEST(PBRTextureSet, RMAOSSourceMode_InvalidKey)
{
    RMAOSSourceMode mode;
    EXPECT_FALSE(tryParseRmaosSourceMode("invalid", mode));
}

// ─── textureMatchModeKey / tryParseTextureMatchMode round-trip ──

TEST(PBRTextureSet, MatchMode_RoundTrip)
{
    TextureMatchMode modes[] = {TextureMatchMode::Auto, TextureMatchMode::Diffuse, TextureMatchMode::Normal};

    for (auto mode : modes)
    {
        std::string key = textureMatchModeKey(mode);
        TextureMatchMode parsed;
        EXPECT_TRUE(tryParseTextureMatchMode(key, parsed));
        EXPECT_EQ(parsed, mode);
    }
}

// ─── generateExportSizeOptions ─────────────────────────────

TEST(PBRTextureSet, ExportSizeOptions_FirstIsOriginal)
{
    auto opts = generateExportSizeOptions(1024, 1024);
    ASSERT_FALSE(opts.empty());
    EXPECT_EQ(opts[0].first, 1024);
    EXPECT_EQ(opts[0].second, 1024);
}

TEST(PBRTextureSet, ExportSizeOptions_ContainsPowerOfTwo)
{
    auto opts = generateExportSizeOptions(2048, 2048);
    bool found512 = false;
    for (auto& [w, h] : opts)
    {
        if (w == 512 && h == 512)
            found512 = true;
    }
    EXPECT_TRUE(found512) << "Expected 512x512 in options for 2048x2048";
}

TEST(PBRTextureSet, ExportSizeOptions_NonSquare)
{
    auto opts = generateExportSizeOptions(2048, 1024);
    ASSERT_FALSE(opts.empty());
    EXPECT_EQ(opts[0].first, 2048);
    EXPECT_EQ(opts[0].second, 1024);
}
