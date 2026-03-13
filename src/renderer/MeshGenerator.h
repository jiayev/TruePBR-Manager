#pragma once

#include <cstdint>
#include <vector>

namespace tpbr
{

/// Preview mesh shape types
enum class PreviewShape
{
    Plane,
    Sphere,
    Cube,
    RoundedCube,
};

/// Single vertex for the preview mesh
struct PreviewVertex
{
    float position[3];
    float normal[3];
    float tangent[4]; // xyz = tangent, w = handedness
    float uv[2];
};

/// Generated mesh data
struct PreviewMesh
{
    std::vector<PreviewVertex> vertices;
    std::vector<uint32_t> indices;
};

/// Generates preview meshes for the material preview widget.
class MeshGenerator
{
  public:
    /// Generate a mesh of the given shape.
    static PreviewMesh generate(PreviewShape shape);

    static PreviewMesh generatePlane();
    static PreviewMesh generateSphere(int slices = 64, int stacks = 32);
    static PreviewMesh generateCube();
    static PreviewMesh generateRoundedCube(float radius = 0.35f, int segments = 16);
};

} // namespace tpbr
