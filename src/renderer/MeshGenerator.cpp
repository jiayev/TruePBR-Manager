#include "MeshGenerator.h"

#include <algorithm>
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

    // 2x2 quad centered at origin, facing +Z (towards camera in default view)
    // Double-sided: front face (+Z normal) and back face (-Z normal)

    auto addFace = [&](float nz)
    {
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        PreviewVertex v{};
        v.normal[0] = 0;
        v.normal[1] = 0;
        v.normal[2] = nz;
        v.tangent[0] = 1;
        v.tangent[1] = 0;
        v.tangent[2] = 0;
        v.tangent[3] = nz > 0 ? 1.0f : -1.0f;

        // CCW winding when viewed from the normal direction
        if (nz > 0)
        {
            // Front face: v0(-1,-1) v1(1,-1) v2(1,1) v3(-1,1) — CCW from +Z
            v.position[0] = -1;
            v.position[1] = -1;
            v.position[2] = 0;
            v.uv[0] = 0;
            v.uv[1] = 1;
            mesh.vertices.push_back(v);

            v.position[0] = 1;
            v.position[1] = -1;
            v.uv[0] = 1;
            v.uv[1] = 1;
            mesh.vertices.push_back(v);

            v.position[0] = 1;
            v.position[1] = 1;
            v.uv[0] = 1;
            v.uv[1] = 0;
            mesh.vertices.push_back(v);

            v.position[0] = -1;
            v.position[1] = 1;
            v.uv[0] = 0;
            v.uv[1] = 0;
            mesh.vertices.push_back(v);
        }
        else
        {
            // Back face: reversed winding — v0(1,-1) v1(-1,-1) v2(-1,1) v3(1,1) — CCW from -Z
            v.position[0] = 1;
            v.position[1] = -1;
            v.position[2] = 0;
            v.uv[0] = 0;
            v.uv[1] = 1;
            mesh.vertices.push_back(v);

            v.position[0] = -1;
            v.position[1] = -1;
            v.uv[0] = 1;
            v.uv[1] = 1;
            mesh.vertices.push_back(v);

            v.position[0] = -1;
            v.position[1] = 1;
            v.uv[0] = 1;
            v.uv[1] = 0;
            mesh.vertices.push_back(v);

            v.position[0] = 1;
            v.position[1] = 1;
            v.uv[0] = 0;
            v.uv[1] = 0;
            mesh.vertices.push_back(v);
        }

        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 3);
        mesh.indices.push_back(base + 2);
    };

    addFace(+1.0f); // front
    addFace(-1.0f); // back

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
    // Winding: CW when viewed from outside (matching Sphere convention)
    // Vertex order per face: BL → BR → TR → TL (CW from normal direction)
    // UV: BL=(0,1) BR=(1,1) TR=(1,0) TL=(0,0) — texture upright
    struct FaceInfo
    {
        float normal[3];
        float tangent[4];
        float positions[4][3];
        float uvs[4][2];
    };

    // clang-format off
    FaceInfo faces[] = {
        // +Z face: from +Z, X right Y up. BL=(-1,-1,1) BR=(1,-1,1) TR=(1,1,1) TL=(-1,1,1)
        {{ 0, 0, 1}, { 1, 0, 0, 1},
         {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},
         {{0,1},{1,1},{1,0},{0,0}}},
        // -Z face: from -Z, X left Y up. BL=(1,-1,-1) BR=(-1,-1,-1) TR=(-1,1,-1) TL=(1,1,-1)
        {{ 0, 0,-1}, {-1, 0, 0, 1},
         {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}},
         {{0,1},{1,1},{1,0},{0,0}}},
        // +X face: from +X, Z left Y up. BL=(1,-1,1) BR=(1,-1,-1) TR=(1,1,-1) TL=(1,1,1)
        {{ 1, 0, 0}, { 0, 0,-1, 1},
         {{ 1,-1, 1},{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1}},
         {{0,1},{1,1},{1,0},{0,0}}},
        // -X face: from -X, Z right Y up. BL=(-1,-1,-1) BR=(-1,-1,1) TR=(-1,1,1) TL=(-1,1,-1)
        {{-1, 0, 0}, { 0, 0, 1, 1},
         {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}},
         {{0,1},{1,1},{1,0},{0,0}}},
        // +Y face: from +Y down, X right Z away. BL=(-1,1,1) BR=(1,1,1) TR=(1,1,-1) TL=(-1,1,-1)
        {{ 0, 1, 0}, { 1, 0, 0, 1},
         {{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1}},
         {{0,1},{1,1},{1,0},{0,0}}},
        // -Y face: from -Y up, X right Z toward. BL=(-1,-1,-1) BR=(1,-1,-1) TR=(1,-1,1) TL=(-1,-1,1)
        {{ 0,-1, 0}, { 1, 0, 0, 1},
         {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}},
         {{0,1},{1,1},{1,0},{0,0}}},
    };
    // clang-format on

    for (const auto& face : faces)
    {
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i)
        {
            PreviewVertex v{};
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
        // Reversed winding: 0→2→1, 0→3→2
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 3);
        mesh.indices.push_back(base + 2);
    }

    return mesh;
}

PreviewMesh MeshGenerator::generateRoundedCube(float radius, int segments)
{
    // Generate a rounded cube by deforming a sphere: clamp each axis to the
    // inner cube extent, then push outward by radius along the offset normal.

    float extent = 1.0f - radius;
    PreviewMesh sphere = generateSphere(segments * 4, segments * 2);
    PreviewMesh mesh;
    mesh.indices = sphere.indices;

    for (const auto& sv : sphere.vertices)
    {
        PreviewVertex v = sv;

        float nx = sv.normal[0];
        float ny = sv.normal[1];
        float nz = sv.normal[2];

        // Nearest point on the shrunken inner cube
        float px = std::clamp(nx * 2.0f, -extent, extent);
        float py = std::clamp(ny * 2.0f, -extent, extent);
        float pz = std::clamp(nz * 2.0f, -extent, extent);

        // Offset direction from inner cube surface to rounded surface
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

        v.normal[0] = dx;
        v.normal[1] = dy;
        v.normal[2] = dz;

        // Cube-projected UV: pick the dominant axis of the normal, then
        // project the position onto the other two axes as UV.
        float anx = std::abs(dx);
        float any = std::abs(dy);
        float anz = std::abs(dz);

        float u, vv;
        if (anx >= any && anx >= anz)
        {
            // X-dominant face: project onto YZ
            u = v.position[2] * (dx > 0 ? -1.0f : 1.0f);
            vv = v.position[1];
        }
        else if (any >= anx && any >= anz)
        {
            // Y-dominant face: project onto XZ
            u = v.position[0];
            vv = v.position[2] * (dy > 0 ? -1.0f : 1.0f);
        }
        else
        {
            // Z-dominant face: project onto XY
            u = v.position[0] * (dz > 0 ? 1.0f : -1.0f);
            vv = v.position[1];
        }

        // Map from [-1, 1] to [0, 1]
        v.uv[0] = u * 0.5f + 0.5f;
        v.uv[1] = -vv * 0.5f + 0.5f; // flip V so top of texture is at top of face

        // Tangent: aligned to UV U-axis direction
        if (anx >= any && anx >= anz)
        {
            v.tangent[0] = 0;
            v.tangent[1] = 0;
            v.tangent[2] = dx > 0 ? -1.0f : 1.0f;
            v.tangent[3] = 1.0f;
        }
        else if (any >= anx && any >= anz)
        {
            v.tangent[0] = 1;
            v.tangent[1] = 0;
            v.tangent[2] = 0;
            v.tangent[3] = 1.0f;
        }
        else
        {
            v.tangent[0] = dz > 0 ? 1.0f : -1.0f;
            v.tangent[1] = 0;
            v.tangent[2] = 0;
            v.tangent[3] = 1.0f;
        }

        mesh.vertices.push_back(v);
    }

    return mesh;
}

} // namespace tpbr
