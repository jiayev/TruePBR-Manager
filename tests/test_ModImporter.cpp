#include <gtest/gtest.h>

#include "core/ModImporter.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace tpbr;
using json = nlohmann::json;
namespace fs = std::filesystem;

// ─── Test Fixtures ─────────────────────────────────────────

class ModImporterTest : public ::testing::Test
{
  protected:
    fs::path m_testDir;

    void SetUp() override
    {
        m_testDir = fs::temp_directory_path() / "truepbr_mod_import_test";
        fs::create_directories(m_testDir / "PBRNIFPatcher");
        fs::create_directories(m_testDir / "textures" / "pbr");
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_testDir, ec);
    }

    /// Write a JSON file under PBRNIFPatcher/ (supports subdirectory paths like "sub/file.json").
    void writeJson(const std::string& relativePath, const json& content)
    {
        auto path = m_testDir / "PBRNIFPatcher" / relativePath;
        fs::create_directories(path.parent_path());
        std::ofstream ofs(path);
        ofs << content.dump(4);
    }

    /// Create a dummy DDS file at the given relative path under the mod dir.
    /// Just a 1-byte placeholder — sufficient for path resolution tests.
    void createDummyTexture(const std::string& relativePath)
    {
        std::string fwdPath = relativePath;
        std::replace(fwdPath.begin(), fwdPath.end(), '\\', '/');
        auto fullPath = m_testDir / fwdPath;
        fs::create_directories(fullPath.parent_path());
        std::ofstream ofs(fullPath, std::ios::binary);
        ofs.put('\0');
    }

    /// Helper: scan + import the first JSON found (for single-file tests).
    ModImportResult importFirst()
    {
        auto scan = ModImporter::scanForJsonFiles(m_testDir);
        if (!scan.success || scan.jsonFilesAbsolute.empty())
        {
            ModImportResult fail;
            fail.diagnostics.push_back({ImportDiagnostic::Severity::Error, scan.errorMessage});
            return fail;
        }
        return ModImporter::importJsonFile(scan.jsonFilesAbsolute[0], m_testDir);
    }

    /// Helper: scan + import a specific JSON by relative path.
    ModImportResult importByName(const std::string& relativePath)
    {
        auto scan = ModImporter::scanForJsonFiles(m_testDir);
        if (!scan.success)
        {
            ModImportResult fail;
            fail.diagnostics.push_back({ImportDiagnostic::Severity::Error, scan.errorMessage});
            return fail;
        }

        for (size_t i = 0; i < scan.jsonFiles.size(); ++i)
        {
            if (scan.jsonFiles[i].string() == relativePath ||
                scan.jsonFiles[i].string() == fs::path(relativePath).make_preferred().string())
            {
                return ModImporter::importJsonFile(scan.jsonFilesAbsolute[i], m_testDir);
            }
        }

        ModImportResult fail;
        fail.diagnostics.push_back({ImportDiagnostic::Severity::Error, "JSON not found: " + relativePath});
        return fail;
    }
};

// ─── Scan API ──────────────────────────────────────────────

TEST_F(ModImporterTest, Scan_FindsJsonInRoot)
{
    json arr = json::array();
    arr.push_back({{"texture", "test01"}});
    writeJson("test.json", arr);

    auto scan = ModImporter::scanForJsonFiles(m_testDir);
    ASSERT_TRUE(scan.success);
    ASSERT_EQ(scan.jsonFiles.size(), 1u);
    EXPECT_EQ(scan.jsonFiles[0].string(), "test.json");
}

TEST_F(ModImporterTest, Scan_FindsJsonInSubdirectories)
{
    json arr1 = json::array();
    arr1.push_back({{"texture", "from_root"}});
    writeJson("root.json", arr1);

    json arr2 = json::array();
    arr2.push_back({{"texture", "from_sub"}});
    writeJson("submod/deep.json", arr2);

    json arr3 = json::array();
    arr3.push_back({{"texture", "from_nested"}});
    writeJson("a/b/c/nested.json", arr3);

    auto scan = ModImporter::scanForJsonFiles(m_testDir);
    ASSERT_TRUE(scan.success);
    ASSERT_EQ(scan.jsonFiles.size(), 3u);
    ASSERT_EQ(scan.jsonFilesAbsolute.size(), 3u);

    // All three should be found (sorted)
    bool foundRoot = false;
    bool foundDeep = false;
    bool foundNested = false;
    for (const auto& rel : scan.jsonFiles)
    {
        std::string s = rel.string();
        if (s.find("root.json") != std::string::npos)
            foundRoot = true;
        if (s.find("deep.json") != std::string::npos)
            foundDeep = true;
        if (s.find("nested.json") != std::string::npos)
            foundNested = true;
    }
    EXPECT_TRUE(foundRoot);
    EXPECT_TRUE(foundDeep);
    EXPECT_TRUE(foundNested);
}

TEST_F(ModImporterTest, Scan_NoPBRNIFPatcherDir)
{
    auto emptyDir = fs::temp_directory_path() / "truepbr_empty_scan_test";
    fs::create_directories(emptyDir);

    auto scan = ModImporter::scanForJsonFiles(emptyDir);
    EXPECT_FALSE(scan.success);
    EXPECT_FALSE(scan.errorMessage.empty());

    std::error_code ec;
    fs::remove_all(emptyDir, ec);
}

TEST_F(ModImporterTest, Scan_InvalidDirectory)
{
    auto scan = ModImporter::scanForJsonFiles("C:/nonexistent/path/fake_mod");
    EXPECT_FALSE(scan.success);
}

// ─── Import Single File ────────────────────────────────────

TEST_F(ModImporterTest, ImportSingleFile_SelectsOne)
{
    json arr1 = json::array();
    arr1.push_back({{"texture", "from_file1"}, {"specular_level", 0.04}});
    writeJson("a_first.json", arr1);

    json arr2 = json::array();
    arr2.push_back({{"texture", "from_file2"}, {"specular_level", 0.08}});
    writeJson("b_second.json", arr2);

    // Import only the second file
    auto scan = ModImporter::scanForJsonFiles(m_testDir);
    ASSERT_TRUE(scan.success);
    ASSERT_EQ(scan.jsonFiles.size(), 2u);

    // Find index of b_second.json
    size_t idx = 0;
    for (size_t i = 0; i < scan.jsonFiles.size(); ++i)
    {
        if (scan.jsonFiles[i].string().find("b_second") != std::string::npos)
        {
            idx = i;
            break;
        }
    }

    auto result = ModImporter::importJsonFile(scan.jsonFilesAbsolute[idx], m_testDir);
    ASSERT_TRUE(result.success);
    // Only entries from b_second.json
    ASSERT_EQ(result.project.textureSets.size(), 1u);
    EXPECT_EQ(result.project.textureSets[0].name, "from_file2");
    EXPECT_FLOAT_EQ(result.project.textureSets[0].params.specularLevel, 0.08f);
}

TEST_F(ModImporterTest, ImportJsonFromSubdirectory)
{
    json arr = json::array();
    arr.push_back({{"texture", "submod_tex"}, {"specular_level", 0.06}});
    writeJson("mysubmod/entries.json", arr);

    auto scan = ModImporter::scanForJsonFiles(m_testDir);
    ASSERT_TRUE(scan.success);
    ASSERT_EQ(scan.jsonFiles.size(), 1u);

    auto result = ModImporter::importJsonFile(scan.jsonFilesAbsolute[0], m_testDir);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.project.textureSets.size(), 1u);
    EXPECT_EQ(result.project.textureSets[0].name, "submod_tex");
}

// ─── Basic Flat Array ──────────────────────────────────────

TEST_F(ModImporterTest, FlatArray_BasicEntry)
{
    json arr = json::array();
    arr.push_back({{"texture", "fern01"},
                   {"emissive", false},
                   {"parallax", false},
                   {"subsurface", false},
                   {"specular_level", 0.04},
                   {"roughness_scale", 1.0},
                   {"subsurface_opacity", 1.0},
                   {"displacement_scale", 1.0},
                   {"subsurface_color", {1, 1, 1}}});

    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.project.textureSets.size(), 1u);

    auto& ts = result.project.textureSets[0];
    EXPECT_EQ(ts.matchTexture, "fern01");
    EXPECT_EQ(ts.name, "fern01");
    EXPECT_EQ(ts.matchMode, TextureMatchMode::Diffuse);
    EXPECT_FALSE(ts.features.emissive);
    EXPECT_FALSE(ts.features.parallax);
    EXPECT_FALSE(ts.features.subsurface);
    EXPECT_FLOAT_EQ(ts.params.specularLevel, 0.04f);
    EXPECT_FLOAT_EQ(ts.params.roughnessScale, 1.0f);
}

TEST_F(ModImporterTest, FlatArray_MultipleEntries)
{
    json arr = json::array();
    arr.push_back({{"texture", "fern01"}, {"specular_level", 0.04}});
    arr.push_back({{"texture", "fern02"}, {"specular_level", 0.08}});
    arr.push_back({{"texture", "mushroom01"}, {"specular_level", 0.12}});

    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.project.textureSets.size(), 3u);
    EXPECT_EQ(result.project.textureSets[0].name, "fern01");
    EXPECT_EQ(result.project.textureSets[1].name, "fern02");
    EXPECT_EQ(result.project.textureSets[2].name, "mushroom01");
    EXPECT_FLOAT_EQ(result.project.textureSets[2].params.specularLevel, 0.12f);
}

// ─── Default + Entries Format ──────────────────────────────

TEST_F(ModImporterTest, DefaultsFormat_MergesDefaults)
{
    json root = {{"default", {{"vertex_color_lum_mult", 1.5}, {"specular_level", 0.04}}},
                 {"entries",
                  json::array({{{"texture", "fern01"}, {"specular_level", 0.08}},
                               {{"texture", "fern02"}}})}};

    writeJson("test.json", root);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.project.textureSets.size(), 2u);

    auto& ts0 = result.project.textureSets[0];
    EXPECT_FLOAT_EQ(ts0.params.specularLevel, 0.08f);
    EXPECT_FLOAT_EQ(ts0.params.vertexColorLumMult, 1.5f);

    auto& ts1 = result.project.textureSets[1];
    EXPECT_FLOAT_EQ(ts1.params.specularLevel, 0.04f);
    EXPECT_FLOAT_EQ(ts1.params.vertexColorLumMult, 1.5f);
}

// ─── Match Fields ──────────────────────────────────────────

TEST_F(ModImporterTest, MatchNormal)
{
    json arr = json::array();
    arr.push_back({{"match_normal", "architecture\\whiterun\\wrwoodplank01_n"}, {"specular_level", 0.04}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    auto& ts = result.project.textureSets[0];
    EXPECT_EQ(ts.matchTexture, "architecture\\whiterun\\wrwoodplank01_n");
    EXPECT_EQ(ts.matchMode, TextureMatchMode::Normal);
}

TEST_F(ModImporterTest, MatchDiffuse)
{
    json arr = json::array();
    arr.push_back({{"match_diffuse", "architecture\\whiterun\\wrwoodplank01"}, {"specular_level", 0.04}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    auto& ts = result.project.textureSets[0];
    EXPECT_EQ(ts.matchTexture, "architecture\\whiterun\\wrwoodplank01");
    EXPECT_EQ(ts.matchMode, TextureMatchMode::Diffuse);
}

TEST_F(ModImporterTest, Rename)
{
    json arr = json::array();
    arr.push_back(
        {{"texture", "architecture\\whiterun\\wrwoodplank01"}, {"rename", "custom_wood"}, {"specular_level", 0.04}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    auto& ts = result.project.textureSets[0];
    EXPECT_EQ(ts.name, "custom_wood");
    EXPECT_EQ(ts.matchTexture, "architecture\\whiterun\\wrwoodplank01");
}

// ─── Feature Flags ─────────────────────────────────────────

TEST_F(ModImporterTest, FeatureFlags_AllEnabled)
{
    json arr = json::array();
    arr.push_back({{"texture", "test01"},
                   {"emissive", true},
                   {"parallax", true},
                   {"subsurface", true},
                   {"subsurface_foliage", true},
                   {"coat_normal", true},
                   {"coat_diffuse", true},
                   {"coat_parallax", true},
                   {"hair", true}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    auto& f = result.project.textureSets[0].features;
    EXPECT_TRUE(f.emissive);
    EXPECT_TRUE(f.parallax);
    EXPECT_TRUE(f.subsurface);
    EXPECT_TRUE(f.subsurfaceFoliage);
    EXPECT_TRUE(f.coatNormal);
    EXPECT_TRUE(f.coatDiffuse);
    EXPECT_TRUE(f.coatParallax);
    EXPECT_TRUE(f.hair);
    EXPECT_TRUE(f.multilayer);
}

TEST_F(ModImporterTest, FuzzObject)
{
    json arr = json::array();
    arr.push_back({{"texture", "cloth01"}, {"fuzz", {{"texture", true}, {"color", {0.5, 0.3, 0.1}}, {"weight", 0.8}}}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    auto& ts = result.project.textureSets[0];
    EXPECT_TRUE(ts.features.fuzz);
    EXPECT_FLOAT_EQ(ts.params.fuzzColor[0], 0.5f);
    EXPECT_FLOAT_EQ(ts.params.fuzzColor[1], 0.3f);
    EXPECT_FLOAT_EQ(ts.params.fuzzColor[2], 0.1f);
    EXPECT_FLOAT_EQ(ts.params.fuzzWeight, 0.8f);
}

TEST_F(ModImporterTest, GlintObject)
{
    json arr = json::array();
    arr.push_back(
        {{"texture", "gem01"},
         {"glint",
          {{"screen_space_scale", 1.5}, {"log_microfacet_density", 2.0}, {"microfacet_roughness", 0.3}, {"density_randomization", 0.5}}}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    auto& ts = result.project.textureSets[0];
    EXPECT_TRUE(ts.features.glint);
    EXPECT_FLOAT_EQ(ts.params.glintScreenSpaceScale, 1.5f);
    EXPECT_FLOAT_EQ(ts.params.glintLogMicrofacetDensity, 2.0f);
    EXPECT_FLOAT_EQ(ts.params.glintMicrofacetRoughness, 0.3f);
    EXPECT_FLOAT_EQ(ts.params.glintDensityRandomization, 0.5f);
}

// ─── Parameters ────────────────────────────────────────────

TEST_F(ModImporterTest, AllParameters)
{
    json arr = json::array();
    arr.push_back({{"texture", "test01"},
                   {"specular_level", 0.08},
                   {"roughness_scale", 0.5},
                   {"displacement_scale", 2.0},
                   {"subsurface_opacity", 0.7},
                   {"subsurface_color", {0.8, 0.2, 0.1}},
                   {"emissive", true},
                   {"emissive_scale", 3.0},
                   {"coat_normal", true},
                   {"coat_strength", 0.9},
                   {"coat_roughness", 0.4},
                   {"coat_specular_level", 0.06},
                   {"vertex_colors", false},
                   {"vertex_color_lum_mult", 0.5},
                   {"vertex_color_sat_mult", 0.3}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    auto& p = result.project.textureSets[0].params;
    EXPECT_FLOAT_EQ(p.specularLevel, 0.08f);
    EXPECT_FLOAT_EQ(p.roughnessScale, 0.5f);
    EXPECT_FLOAT_EQ(p.displacementScale, 2.0f);
    EXPECT_FLOAT_EQ(p.subsurfaceOpacity, 0.7f);
    EXPECT_FLOAT_EQ(p.subsurfaceColor[0], 0.8f);
    EXPECT_FLOAT_EQ(p.subsurfaceColor[1], 0.2f);
    EXPECT_FLOAT_EQ(p.subsurfaceColor[2], 0.1f);
    EXPECT_FLOAT_EQ(p.emissiveScale, 3.0f);
    EXPECT_FLOAT_EQ(p.coatStrength, 0.9f);
    EXPECT_FLOAT_EQ(p.coatRoughness, 0.4f);
    EXPECT_FLOAT_EQ(p.coatSpecularLevel, 0.06f);
    EXPECT_FALSE(p.vertexColors);
    EXPECT_FLOAT_EQ(p.vertexColorLumMult, 0.5f);
    EXPECT_FLOAT_EQ(p.vertexColorSatMult, 0.3f);
}

// ─── Slot Path Overrides ───────────────────────────────────

TEST_F(ModImporterTest, ExplicitSlotPaths)
{
    json arr = json::array();
    arr.push_back({{"texture", "test01"},
                   {"slot1", "textures\\pbr\\custom\\diffuse.dds"},
                   {"slot2", "textures\\pbr\\custom\\normal.dds"},
                   {"slot6", "textures\\pbr\\custom\\rmaos.dds"}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    auto& overrides = result.project.textureSets[0].slotPathOverrides;
    EXPECT_EQ(overrides[PBRTextureSlot::Diffuse], "textures\\pbr\\custom\\diffuse.dds");
    EXPECT_EQ(overrides[PBRTextureSlot::Normal], "textures\\pbr\\custom\\normal.dds");
    EXPECT_EQ(overrides[PBRTextureSlot::RMAOS], "textures\\pbr\\custom\\rmaos.dds");
}

// ─── Delete Entries Skipped ────────────────────────────────

TEST_F(ModImporterTest, DeleteEntry_Skipped)
{
    json arr = json::array();
    arr.push_back({{"texture", "keep01"}, {"specular_level", 0.04}});
    arr.push_back({{"texture", "delete01"}, {"delete", true}});
    arr.push_back({{"texture", "keep02"}, {"specular_level", 0.08}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.project.textureSets.size(), 2u);
    EXPECT_EQ(result.project.textureSets[0].name, "keep01");
    EXPECT_EQ(result.project.textureSets[1].name, "keep02");
}

// ─── Error Cases ───────────────────────────────────────────

TEST_F(ModImporterTest, MalformedJson_Error)
{
    auto path = m_testDir / "PBRNIFPatcher" / "bad.json";
    std::ofstream ofs(path);
    ofs << "{ not valid json !!!";
    ofs.close();

    auto scan = ModImporter::scanForJsonFiles(m_testDir);
    ASSERT_TRUE(scan.success);

    auto result = ModImporter::importJsonFile(scan.jsonFilesAbsolute[0], m_testDir);
    EXPECT_FALSE(result.success);
}

// ─── Texture Resolution ────────────────────────────────────

TEST_F(ModImporterTest, TextureResolution_ConventionPaths)
{
    createDummyTexture("textures/pbr/fern01.dds");
    createDummyTexture("textures/pbr/fern01_n.dds");
    createDummyTexture("textures/pbr/fern01_rmaos.dds");

    json arr = json::array();
    arr.push_back({{"texture", "fern01"}, {"specular_level", 0.04}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    auto& ts = result.project.textureSets[0];
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::Diffuse) > 0);
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::Normal) > 0);
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::RMAOS) > 0);
}

TEST_F(ModImporterTest, TextureResolution_WithSubdir)
{
    createDummyTexture("textures/pbr/architecture/whiterun/wrwoodplank01.dds");
    createDummyTexture("textures/pbr/architecture/whiterun/wrwoodplank01_n.dds");
    createDummyTexture("textures/pbr/architecture/whiterun/wrwoodplank01_rmaos.dds");
    createDummyTexture("textures/pbr/architecture/whiterun/wrwoodplank01_g.dds");

    json arr = json::array();
    arr.push_back(
        {{"texture", "architecture\\whiterun\\wrwoodplank01"}, {"emissive", true}, {"specular_level", 0.04}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    auto& ts = result.project.textureSets[0];
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::Diffuse) > 0);
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::Normal) > 0);
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::RMAOS) > 0);
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::Emissive) > 0);
}

TEST_F(ModImporterTest, TextureResolution_WithRename)
{
    createDummyTexture("textures/pbr/architecture/whiterun/custom_wood.dds");
    createDummyTexture("textures/pbr/architecture/whiterun/custom_wood_n.dds");
    createDummyTexture("textures/pbr/architecture/whiterun/custom_wood_rmaos.dds");

    json arr = json::array();
    arr.push_back(
        {{"texture", "architecture\\whiterun\\wrwoodplank01"}, {"rename", "custom_wood"}, {"specular_level", 0.04}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    auto& ts = result.project.textureSets[0];
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::Diffuse) > 0);
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::Normal) > 0);
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::RMAOS) > 0);
}

// ─── Project Metadata ──────────────────────────────────────

TEST_F(ModImporterTest, ProjectMetadata)
{
    json arr = json::array();
    arr.push_back({{"texture", "test01"}, {"specular_level", 0.04}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);

    // Project name comes from the JSON filename
    EXPECT_EQ(result.project.name, "test");
    // Output mod folder is set to the mod directory
    EXPECT_EQ(result.project.outputModFolder, m_testDir);
}

// ─── Rename with Directory ─────────────────────────────────

TEST_F(ModImporterTest, RenameWithDirectory_SingleEntry)
{
    // When rename specifies a different directory, matchTexture should be
    // updated to the effective PBR path and the original vanilla match
    // should become an alias.
    createDummyTexture("textures/pbr/landscape/dirt02.dds");
    createDummyTexture("textures/pbr/landscape/dirt02_n.dds");

    json arr = json::array();
    arr.push_back({{"texture", "landscape\\statics\\dirt02"},
                   {"rename", "landscape\\dirt02"},
                   {"specular_level", 0.04}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.project.textureSets.size(), 1u);

    auto& ts = result.project.textureSets[0];
    EXPECT_EQ(ts.name, "dirt02");
    // matchTexture is the effective PBR path (from rename)
    EXPECT_EQ(ts.matchTexture, "landscape\\dirt02");
    // Original vanilla path is stored as an alias
    ASSERT_EQ(ts.matchAliases.size(), 1u);
    EXPECT_EQ(ts.matchAliases[0].matchTexture, "landscape\\statics\\dirt02");

    // Textures should be resolved from the rename directory
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::Diffuse) > 0);
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::Normal) > 0);
}

TEST_F(ModImporterTest, RenameWithDirectory_MergeMultipleEntries)
{
    // Two entries with the same rename but different vanilla textures
    // should be merged into one texture set with aliases.
    createDummyTexture("textures/pbr/landscape/dirt02.dds");
    createDummyTexture("textures/pbr/landscape/dirt02_n.dds");

    json arr = json::array();
    arr.push_back({{"texture", "landscape\\statics\\dirt02"},
                   {"rename", "landscape\\dirt02"},
                   {"specular_level", 0.04}});
    arr.push_back({{"texture", "landscape\\statics\\dirt02snow"},
                   {"rename", "landscape\\dirt02"},
                   {"specular_level", 0.04}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.project.textureSets.size(), 1u);

    auto& ts = result.project.textureSets[0];
    EXPECT_EQ(ts.name, "dirt02");
    EXPECT_EQ(ts.matchTexture, "landscape\\dirt02");
    // Both original vanilla paths should be aliases
    ASSERT_EQ(ts.matchAliases.size(), 2u);

    // Check that both original paths are present (order may vary)
    std::vector<std::string> aliasPaths;
    for (const auto& a : ts.matchAliases)
        aliasPaths.push_back(a.matchTexture);
    EXPECT_TRUE(std::find(aliasPaths.begin(), aliasPaths.end(),
                          "landscape\\statics\\dirt02") != aliasPaths.end());
    EXPECT_TRUE(std::find(aliasPaths.begin(), aliasPaths.end(),
                          "landscape\\statics\\dirt02snow") != aliasPaths.end());
}

TEST_F(ModImporterTest, RenameStemOnly_NoAliasCreated)
{
    // Stem-only rename should NOT create aliases — matchTexture stays as
    // the original vanilla path.
    createDummyTexture("textures/pbr/architecture/whiterun/custom_wood.dds");
    createDummyTexture("textures/pbr/architecture/whiterun/custom_wood_n.dds");

    json arr = json::array();
    arr.push_back({{"texture", "architecture\\whiterun\\wrwoodplank01"},
                   {"rename", "custom_wood"},
                   {"specular_level", 0.04}});
    writeJson("test.json", arr);

    auto result = importFirst();
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.project.textureSets.size(), 1u);

    auto& ts = result.project.textureSets[0];
    EXPECT_EQ(ts.name, "custom_wood");
    EXPECT_EQ(ts.matchTexture, "architecture\\whiterun\\wrwoodplank01");
    EXPECT_TRUE(ts.matchAliases.empty());
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::Diffuse) > 0);
    EXPECT_TRUE(ts.textures.count(PBRTextureSlot::Normal) > 0);
}
