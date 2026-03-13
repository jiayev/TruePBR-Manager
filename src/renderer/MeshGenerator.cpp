#include "MeshGenerator.h"

#include <cmath>

namespace tpbr
{

static const float PI = 3.14159265359f;

PreviewMesh MeshGenerator::generate(PreviewShape shape)
{
    switch (shape)
    {
    case PreviewShape::Plane:
        return generatePlane();
    case PreviewShape::Sphere:
        return generateSphere();
    case PreviewShape::Cube:
        return generateCube();
    case PreviewShape::RoundedCube:
        return generateRoundedCube();
    default:
        return generateSphere();
    }
}

PreviewMesh MeshGenerator::generatePlane()
{
    PreviewMesh mesh;

    // 2x2 quad centered at origin, facing +Y
    PreviewVertex v;
    v.normal[0] = 0;
    v.normal[1] = 1;
    v.normal[2] = 0;
    v.tangent[0] = 1;
    v.tangent[1] = 0;
    v.tangent[2] = 0;
    v.tangent[3] = 1;

    // v0: (-1, 0, -1) uv(0, 0)
    v.position[0] = -1;
    v.position[1] = 0;
    v.position[2] = -1;
    v.uv[0] = 0;
    v.uv[1] = 0;
    mesh.vertices.push_back(v);

    // v1: (1, 0, -1) uv(1, 0)
    v.position[0] = 1;
    v.uv[0] = 1;
    mesh.vertices.push_back(v);

    // v2: (1, 0, 1) uv(1, 1)
    v.position[2] = 1;
    v.uv[1] = 1;
    mesh.vertices.push_back(v);

    // v3: (-1, 0, 1) uv(0, 1)
    v.position[0] = -1;
    v.uv[0] = 0;
    mesh.vertices.push_back(v);

    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

PreviewMesh MeshGenerator::generateSphere(int slices, int stacks)
{
    PreviewMesh mesh;

    for (int j = 0; j <= stacks; ++j)
    {
        float phi = PI * static_cast<float>(j) / static_cast<float>(stacks);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);

        for (int i = 0; i <= slices; ++i)
        {
            float theta = 2.0f * PI * static_cast<float>(i) / static_cast<float>(slices);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            PreviewVertex v;
            v.normal[0] = sinPhi * cosTheta;
            v.normal[1] = cosPhi;
            v.normal[2] = sinPhi * sinTheta;

            v.position[0] = v.normal[0];
            v.position[1] = v.normal[1];
            v.position[2] = v.normal[2];

            v.uv[0] = static_cast<float>(i) / static_cast<float>(slices);
            v.uv[1] = static_cast<float>(j) / static_cast<float>(stacks);

            // Tangent: derivative of position w.r.t. theta
            v.tangent[0] = -sinTheta;
            v.tangent[1] = 0;
            v.tangent[2] = cosTheta;
            v.tangent[3] = 1.0f;

            mesh.vertices.push_back(v);
        }
    }

    for (int j = 0; j < stacks; ++j)
    {
        for (int i = 0; i < slices; ++i)
        {
            uint32_t a = j * (slices + 1) + i;
            uint32_t b = a + slices + 1;

            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(a + 1);

            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b + 1);
        }
    }

    return mesh;
}

PreviewMesh MeshGenerator::generateCube()
{
    PreviewMesh mesh;

    // 6 faces, 4 vertices each
    struct FaceInfo
    {
        float normal[3];
        float tangent[4];
        float positions[4][3];
        float uvs[4][2];
    };

    // clang-format off
    FaceInfo faces[] = {
        // +Z face
        {{0, 0, 1}, {1, 0, 0, 1}, {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}}, {{0,0},{1,0},{1,1},{0,1}}},
        // -Z face
        {{0, 0,-1}, {-1,0, 0, 1}, {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}}, {{0,0},{1,0},{1,1},{0,1}}},
        // +X face
        {{1, 0, 0}, {0, 0, 1, 1}, {{ 1,-1, 1},{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1}}, {{0,0},{1,0},{1,1},{0,1}}},
        // -X face
        {{-1,0, 0}, {0, 0,-1, 1}, {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}}, {{0,0},{1,0},{1,1},{0,1}}},
        // +Y face
        {{0, 1, 0}, {1, 0, 0, 1}, {{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1}}, {{0,0},{1,0},{1,1},{0,1}}},
        // -Y face
        {{0,-1, 0}, {1, 0, 0, 1}, {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}}, {{0,0},{1,0},{1,1},{0,1}}},
    };
    // clang-format on

    for (const auto& face : faces)
    {
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i)
        {
            PreviewVertex v;
            v.position[0] = face.positions[i][0];
            v.position[1] = face.positions[i][1];
            v.position[2] = face.positions[i][2];
            v.normal[0] = face.normal[0];
            v.normal[1] = face.normal[1];
            v.normal[2] = face.normal[2];
            v.tangent[0] = face.tangent[0];
            v.tangent[1] = face.tangent[1];
            v.tangent[2] = face.tangent[2];
            v.tangent[3] = face.tangent[3];
            v.uv[0] = face.uvs[i][0];
            v.uv[1] = face.uvs[i][1];
            mesh.vertices.push_back(v);
        }
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }

    return mesh;
}

PreviewMesh MeshGenerator::generateRoundedCube(float radius, int segments)
{
    // Generate a cube with rounded edges by placing spherical caps at corners,
    // cylindrical edges along edges, and flat faces.
    // Simplified approach: generate a subdivided sphere, then remap positions
    // to create a rounded cube shape.

    float extent = 1.0f - radius;
    PreviewMesh sphere = generateSphere(segments * 4, segments * 2);
    PreviewMesh mesh;
    mesh.indices = sphere.indices;

    for (const auto& sv : sphere.vertices)
    {
        PreviewVertex v = sv;

        // Map sphere normal to rounded cube surface
        float nx = sv.normal[0];
        float ny = sv.normal[1];
        float nz = sv.normal[2];

        // Push position outward from a shrunken cube
        float cx = std::clamp(nx / (std::abs(nx) + 0.001f), -1.0f, 1.0f) * extent;
        float cy = std::clamp(ny / (std::abs(ny) + 0.001f), -1.0f, 1.0f) * extent;
        float cz = std::clamp(nz / (std::abs(nz) + 0.001f), -1.0f, 1.0f) * extent;

        // Nearest point on shrunken cube
        float px = std::clamp(nx * 2.0f, -extent, extent);
        float py = std::clamp(ny * 2.0f, -extent, extent);
        float pz = std::clamp(nz * 2.0f, -extent, extent);

        // Offset by radius in normal direction
        float dx = nx - px / (extent > 0.0f ? extent : 1.0f) * 0.5f;
        float dy = ny - py / (extent > 0.0f ? extent : 1.0f) * 0.5f;
        float dz = nz - pz / (extent > 0.0f ? extent : 1.0f) * 0.5f;
        float dlen = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dlen > 0.0001f)
        {
            dx /= dlen;
            dy /= dlen;
            dz /= dlen;
        }

        v.position[0] = px + dx * radius;
        v.position[1] = py + dy * radius;
        v.position[2] = pz + dz * radius;

        // Normal is the offset direction
        v.normal[0] = dx;
        v.normal[1] = dy;
        v.normal[2] = dz;

        mesh.vertices.push_back(v);
    }

    return mesh;
}

} // namespace tpbr
