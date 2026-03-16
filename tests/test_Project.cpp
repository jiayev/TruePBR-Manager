#include <gtest/gtest.h>

#include "core/PBRTextureSet.h"
#include "core/Project.h"

#include <cstdio>
#include <filesystem>

using namespace tpbr;

namespace fs = std::filesystem;

// Helper: create a temporary file path
static fs::path tempProjectPath()
{
    auto tmp = fs::temp_directory_path() / "truepbr_test_project.tpbr";
    return tmp;
}

class ProjectTest : public ::testing::Test
{
  protected:
    void TearDown() override
    {
        auto path = tempProjectPath();
        std::error_code ec;
        fs::remove(path, ec);
    }
};

// ─── addTextureSet ─────────────────────────────────────────

TEST_F(ProjectTest, AddTextureSet_ReturnsIndex)
{
    Project proj;
    proj.name = "TestProject";

    size_t idx = proj.addTextureSet("Set1", "textures\\diffuse01");
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(proj.textureSets.size(), 1u);
    EXPECT_EQ(proj.textureSets[0].name, "Set1");
    EXPECT_EQ(proj.textureSets[0].matchTexture, "textures\\diffuse01");
}

TEST_F(ProjectTest, AddMultipleSets)
{
    Project proj;
    proj.name = "TestProject";

    size_t idx0 = proj.addTextureSet("A", "pathA");
    size_t idx1 = proj.addTextureSet("B", "pathB");
    EXPECT_EQ(idx0, 0u);
    EXPECT_EQ(idx1, 1u);
    EXPECT_EQ(proj.textureSets.size(), 2u);
}

// ─── removeTextureSet ──────────────────────────────────────

TEST_F(ProjectTest, RemoveTextureSet_Valid)
{
    Project proj;
    proj.name = "TestProject";
    proj.addTextureSet("A", "pathA");
    proj.addTextureSet("B", "pathB");

    proj.removeTextureSet(0);
    EXPECT_EQ(proj.textureSets.size(), 1u);
    EXPECT_EQ(proj.textureSets[0].name, "B");
}

TEST_F(ProjectTest, RemoveTextureSet_OutOfRange_NoOp)
{
    Project proj;
    proj.name = "TestProject";
    proj.addTextureSet("A", "pathA");

    proj.removeTextureSet(999);
    EXPECT_EQ(proj.textureSets.size(), 1u);
}

// ─── Save / Load round-trip ────────────────────────────────

TEST_F(ProjectTest, SaveLoad_BasicRoundTrip)
{
    Project proj;
    proj.name = "RoundTripTest";
    proj.outputModFolder = "D:/Mods/TestMod";
    proj.addTextureSet("WoodPlank", "architecture\\whiterun\\wrwoodplank01");

    auto& ts = proj.textureSets[0];
    ts.matchMode = TextureMatchMode::Normal;
    ts.rmaosSourceMode = RMAOSSourceMode::SeparateChannels;
    ts.features.emissive = true;
    ts.features.parallax = true;
    ts.params.specularLevel = 0.08f;
    ts.params.roughnessScale = 0.5f;
    ts.params.emissiveScale = 2.0f;
    ts.landscapeEdids = {"LandscapeDirt02", "LandscapeGrass01"};
    ts.tags = "test";
    ts.notes = "round-trip test";

    auto path = tempProjectPath();
    ASSERT_TRUE(proj.save(path));
    ASSERT_TRUE(fs::exists(path));

    Project loaded = Project::load(path);

    EXPECT_EQ(loaded.name, "RoundTripTest");
    EXPECT_EQ(loaded.outputModFolder.string(), "D:/Mods/TestMod");
    ASSERT_EQ(loaded.textureSets.size(), 1u);

    auto& lts = loaded.textureSets[0];
    EXPECT_EQ(lts.name, "WoodPlank");
    EXPECT_EQ(lts.matchTexture, "architecture\\whiterun\\wrwoodplank01");
    EXPECT_EQ(lts.matchMode, TextureMatchMode::Normal);
    EXPECT_EQ(lts.rmaosSourceMode, RMAOSSourceMode::SeparateChannels);
    EXPECT_TRUE(lts.features.emissive);
    EXPECT_TRUE(lts.features.parallax);
    EXPECT_FALSE(lts.features.subsurface);
    EXPECT_FLOAT_EQ(lts.params.specularLevel, 0.08f);
    EXPECT_FLOAT_EQ(lts.params.roughnessScale, 0.5f);
    EXPECT_FLOAT_EQ(lts.params.emissiveScale, 2.0f);
    ASSERT_EQ(lts.landscapeEdids.size(), 2u);
    EXPECT_EQ(lts.landscapeEdids[0], "LandscapeDirt02");
    EXPECT_EQ(lts.landscapeEdids[1], "LandscapeGrass01");
    EXPECT_EQ(lts.tags, "test");
    EXPECT_EQ(lts.notes, "round-trip test");
}

TEST_F(ProjectTest, SaveLoad_MultipleTextureSets)
{
    Project proj;
    proj.name = "MultiSet";
    proj.addTextureSet("A", "pathA");
    proj.addTextureSet("B", "pathB");
    proj.addTextureSet("C", "pathC");

    auto path = tempProjectPath();
    ASSERT_TRUE(proj.save(path));

    Project loaded = Project::load(path);
    ASSERT_EQ(loaded.textureSets.size(), 3u);
    EXPECT_EQ(loaded.textureSets[0].name, "A");
    EXPECT_EQ(loaded.textureSets[1].name, "B");
    EXPECT_EQ(loaded.textureSets[2].name, "C");
}

TEST_F(ProjectTest, SaveLoad_CompressionOverrides)
{
    Project proj;
    proj.name = "CompressionTest";
    proj.addTextureSet("Set1", "path1");
    proj.textureSets[0].exportCompression[PBRTextureSlot::Diffuse] = DDSCompressionMode::BC3_sRGB;
    proj.textureSets[0].exportCompression[PBRTextureSlot::Normal] = DDSCompressionMode::BC5_Linear;

    auto path = tempProjectPath();
    ASSERT_TRUE(proj.save(path));

    Project loaded = Project::load(path);
    ASSERT_EQ(loaded.textureSets.size(), 1u);

    auto& comp = loaded.textureSets[0].exportCompression;
    ASSERT_TRUE(comp.count(PBRTextureSlot::Diffuse));
    EXPECT_EQ(comp[PBRTextureSlot::Diffuse], DDSCompressionMode::BC3_sRGB);
    ASSERT_TRUE(comp.count(PBRTextureSlot::Normal));
    EXPECT_EQ(comp[PBRTextureSlot::Normal], DDSCompressionMode::BC5_Linear);
}

TEST_F(ProjectTest, Load_NonexistentFile_EmptyProject)
{
    Project loaded = Project::load("C:/nonexistent/path/fake.tpbr");
    // Should not crash, returns a default project
    EXPECT_TRUE(loaded.textureSets.empty());
}
