#include <gtest/gtest.h>

#include "core/PBRTextureSet.h"
#include "core/TextureSetValidator.h"

using namespace tpbr;

// Helper to build a minimal valid texture set
static PBRTextureSet makeMinimalValid()
{
    PBRTextureSet ts;
    ts.name = "TestSet";
    ts.matchTexture = "architecture\\whiterun\\wrwoodplank01";

    TextureEntry diffuse;
    diffuse.slot = PBRTextureSlot::Diffuse;
    diffuse.sourcePath = "C:/textures/diffuse.dds";
    diffuse.width = 1024;
    diffuse.height = 1024;
    ts.textures[PBRTextureSlot::Diffuse] = diffuse;

    TextureEntry normal;
    normal.slot = PBRTextureSlot::Normal;
    normal.sourcePath = "C:/textures/normal.dds";
    normal.width = 1024;
    normal.height = 1024;
    ts.textures[PBRTextureSlot::Normal] = normal;

    TextureEntry rmaos;
    rmaos.slot = PBRTextureSlot::RMAOS;
    rmaos.sourcePath = "C:/textures/rmaos.dds";
    rmaos.width = 1024;
    rmaos.height = 1024;
    ts.textures[PBRTextureSlot::RMAOS] = rmaos;

    return ts;
}

// ─── Valid set produces no errors ──────────────────────────

TEST(Validator, MinimalValid_NoErrors)
{
    auto ts = makeMinimalValid();
    auto issues = TextureSetValidator::validate(ts);

    for (auto& issue : issues)
    {
        EXPECT_NE(issue.severity, ValidationSeverity::Error) << "Unexpected error: " << issue.message;
    }
}

// ─── Missing match texture ─────────────────────────────────

TEST(Validator, EmptyMatchTexture_Error)
{
    auto ts = makeMinimalValid();
    ts.matchTexture = "";
    auto issues = TextureSetValidator::validate(ts);

    bool found = false;
    for (auto& issue : issues)
    {
        if (issue.severity == ValidationSeverity::Error && issue.message.find("match") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found) << "Expected error about missing match texture";
}

// ─── Missing required slots ────────────────────────────────

TEST(Validator, MissingDiffuse_Error)
{
    auto ts = makeMinimalValid();
    ts.textures.erase(PBRTextureSlot::Diffuse);
    auto issues = TextureSetValidator::validate(ts);

    bool found = false;
    for (auto& issue : issues)
    {
        if (issue.severity == ValidationSeverity::Error &&
            (issue.message.find("Diffuse") != std::string::npos || issue.message.find("diffuse") != std::string::npos))
            found = true;
    }
    EXPECT_TRUE(found) << "Expected error about missing Diffuse";
}

TEST(Validator, MissingNormal_Error)
{
    auto ts = makeMinimalValid();
    ts.textures.erase(PBRTextureSlot::Normal);
    auto issues = TextureSetValidator::validate(ts);

    bool found = false;
    for (auto& issue : issues)
    {
        if (issue.severity == ValidationSeverity::Error &&
            (issue.message.find("Normal") != std::string::npos || issue.message.find("normal") != std::string::npos))
            found = true;
    }
    EXPECT_TRUE(found) << "Expected error about missing Normal";
}

TEST(Validator, MissingRMAOS_PackedMode_Error)
{
    auto ts = makeMinimalValid();
    ts.textures.erase(PBRTextureSlot::RMAOS);
    ts.rmaosSourceMode = RMAOSSourceMode::PackedTexture;
    auto issues = TextureSetValidator::validate(ts);

    bool found = false;
    for (auto& issue : issues)
    {
        if (issue.severity == ValidationSeverity::Error &&
            (issue.message.find("RMAOS") != std::string::npos || issue.message.find("rmaos") != std::string::npos))
            found = true;
    }
    EXPECT_TRUE(found) << "Expected error about missing RMAOS in packed mode";
}

TEST(Validator, SplitMode_NoPackedRMAOS_NoError)
{
    auto ts = makeMinimalValid();
    ts.textures.erase(PBRTextureSlot::RMAOS);
    ts.rmaosSourceMode = RMAOSSourceMode::SeparateChannels;

    ChannelMapEntry roughness;
    roughness.sourcePath = "C:/textures/roughness.png";
    roughness.width = 1024;
    roughness.height = 1024;
    ts.channelMaps[ChannelMap::Roughness] = roughness;

    auto issues = TextureSetValidator::validate(ts);

    for (auto& issue : issues)
    {
        if (issue.severity == ValidationSeverity::Error &&
            issue.message.find("RMAOS") != std::string::npos)
        {
            FAIL() << "Should not error about RMAOS in split mode: " << issue.message;
        }
    }
}

// ─── Non-power-of-two warning ──────────────────────────────

TEST(Validator, NonPowerOfTwo_Warning)
{
    auto ts = makeMinimalValid();
    ts.textures[PBRTextureSlot::Diffuse].width = 1000;
    ts.textures[PBRTextureSlot::Diffuse].height = 1000;
    auto issues = TextureSetValidator::validate(ts);

    bool found = false;
    for (auto& issue : issues)
    {
        if (issue.severity == ValidationSeverity::Warning &&
            (issue.message.find("power") != std::string::npos || issue.message.find("Power") != std::string::npos))
            found = true;
    }
    EXPECT_TRUE(found) << "Expected warning about non-power-of-two";
}

// ─── Slot conflicts ────────────────────────────────────────

TEST(Validator, TX06_CoatAndFuzz_Error)
{
    auto ts = makeMinimalValid();

    TextureEntry coat;
    coat.slot = PBRTextureSlot::CoatNormalRoughness;
    coat.sourcePath = "C:/textures/coat.dds";
    coat.width = 1024;
    coat.height = 1024;
    ts.textures[PBRTextureSlot::CoatNormalRoughness] = coat;

    TextureEntry fuzz;
    fuzz.slot = PBRTextureSlot::Fuzz;
    fuzz.sourcePath = "C:/textures/fuzz.dds";
    fuzz.width = 1024;
    fuzz.height = 1024;
    ts.textures[PBRTextureSlot::Fuzz] = fuzz;

    auto issues = TextureSetValidator::validate(ts);

    bool found = false;
    for (auto& issue : issues)
    {
        if (issue.severity == ValidationSeverity::Error &&
            (issue.message.find("TX06") != std::string::npos || issue.message.find("conflict") != std::string::npos ||
             issue.message.find("Coat") != std::string::npos))
            found = true;
    }
    EXPECT_TRUE(found) << "Expected error about TX06 slot conflict";
}

TEST(Validator, TX07_SubsurfaceAndCoatColor_Error)
{
    auto ts = makeMinimalValid();

    TextureEntry ss;
    ss.slot = PBRTextureSlot::Subsurface;
    ss.sourcePath = "C:/textures/ss.dds";
    ss.width = 1024;
    ss.height = 1024;
    ts.textures[PBRTextureSlot::Subsurface] = ss;

    TextureEntry cc;
    cc.slot = PBRTextureSlot::CoatColor;
    cc.sourcePath = "C:/textures/cc.dds";
    cc.width = 1024;
    cc.height = 1024;
    ts.textures[PBRTextureSlot::CoatColor] = cc;

    auto issues = TextureSetValidator::validate(ts);

    bool found = false;
    for (auto& issue : issues)
    {
        if (issue.severity == ValidationSeverity::Error &&
            (issue.message.find("TX07") != std::string::npos || issue.message.find("conflict") != std::string::npos ||
             issue.message.find("Subsurface") != std::string::npos))
            found = true;
    }
    EXPECT_TRUE(found) << "Expected error about TX07 slot conflict";
}

// ─── Feature without texture warning ───────────────────────

TEST(Validator, EmissiveEnabled_NoTexture_Warning)
{
    auto ts = makeMinimalValid();
    ts.features.emissive = true;
    // No emissive texture assigned
    auto issues = TextureSetValidator::validate(ts);

    bool found = false;
    for (auto& issue : issues)
    {
        if (issue.severity == ValidationSeverity::Warning &&
            (issue.message.find("emissive") != std::string::npos || issue.message.find("Emissive") != std::string::npos))
            found = true;
    }
    EXPECT_TRUE(found) << "Expected warning about emissive enabled without texture";
}

// ─── Match Alias Validation ────────────────────────────────

TEST(Validator, EmptyAlias_Warning)
{
    auto ts = makeMinimalValid();
    ts.matchAliases.push_back({"", TextureMatchMode::Diffuse});

    auto issues = TextureSetValidator::validate(ts);
    bool found = false;
    for (auto& issue : issues)
    {
        if (issue.severity == ValidationSeverity::Warning && issue.message.find("alias") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found) << "Expected warning about empty alias path";
}

TEST(Validator, DuplicateAlias_Warning)
{
    auto ts = makeMinimalValid();
    ts.matchAliases.push_back({"landscape\\statics\\dirt02", TextureMatchMode::Diffuse});
    ts.matchAliases.push_back({"landscape\\statics\\dirt02", TextureMatchMode::Diffuse});

    auto issues = TextureSetValidator::validate(ts);
    bool found = false;
    for (auto& issue : issues)
    {
        if (issue.severity == ValidationSeverity::Warning && issue.message.find("uplicate") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found) << "Expected warning about duplicate alias";
}

TEST(Validator, AliasDuplicatesPrimary_Warning)
{
    auto ts = makeMinimalValid();
    // Alias duplicates the primary matchTexture
    ts.matchAliases.push_back({ts.matchTexture, TextureMatchMode::Diffuse});

    auto issues = TextureSetValidator::validate(ts);
    bool found = false;
    for (auto& issue : issues)
    {
        if (issue.severity == ValidationSeverity::Warning && issue.message.find("uplicate") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found) << "Expected warning when alias duplicates primary matchTexture";
}

TEST(Validator, ValidAliases_NoAliasWarnings)
{
    auto ts = makeMinimalValid();
    ts.matchAliases.push_back({"landscape\\statics\\dirt02", TextureMatchMode::Diffuse});
    ts.matchAliases.push_back({"landscape\\statics\\dirt02snow", TextureMatchMode::Diffuse});

    auto issues = TextureSetValidator::validate(ts);
    for (auto& issue : issues)
    {
        if (issue.message.find("alias") != std::string::npos || issue.message.find("uplicate") != std::string::npos)
            FAIL() << "Unexpected alias-related issue: " << issue.message;
    }
}
