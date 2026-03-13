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
    // Build a cube with beveled edges. Each of the 6 faces is a grid that
    // extends past the flat region into the rounded edges/corners. UV covers
    // the full [0,1] range per face including the bevel portion.
    //
    // Strategy: for each face, generate an (N+1)x(N+1) grid of vertices.
    // The flat center occupies most of the grid; the outer ring curves around
    // the bevel. The position is computed by offsetting from the inner cube
    // surface outward along the rounded normal.

    const int N = std::max(segments, 2); // grid subdivisions per face edge
    const float halfExtent = 1.0f;       // half-size of the outer cube
    const float inner = halfExtent - radius;

    PreviewMesh mesh;

    // Face definition: axis (0=X,1=Y,2=Z), sign (+1/-1), and the two tangent axes
    struct FaceDef
    {
        int axis;    // which axis is the face normal
        float sign;  // +1 or -1
        int uAxis;   // which axis maps to U
        float uSign; // U direction sign
        int vAxis;   // which axis maps to V
        float vSign; // V direction sign
    };

    // 6 faces: ordered to match Cube face definitions
    FaceDef faceDefs[] = {
        {2, +1, 0, +1, 1, +1}, // +Z: U=+X, V=+Y
        {2, -1, 0, -1, 1, +1}, // -Z: U=-X, V=+Y
        {0, +1, 2, -1, 1, +1}, // +X: U=-Z, V=+Y
        {0, -1, 2, +1, 1, +1}, // -X: U=+Z, V=+Y
        {1, +1, 0, +1, 2, -1}, // +Y: U=+X, V=-Z
        {1, -1, 0, +1, 2, +1}, // -Y: U=+X, V=+Z
    };

    for (const auto& face : faceDefs)
    {
        uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());

        for (int j = 0; j <= N; ++j)
        {
            for (int i = 0; i <= N; ++i)
            {
                // UV in [0, 1]
                float u = static_cast<float>(i) / static_cast<float>(N);
                float v = static_cast<float>(j) / static_cast<float>(N);

                // Map UV to [-1, 1] range on the face plane
                float su = (u * 2.0f - 1.0f) * halfExtent;
                float sv = (v * 2.0f - 1.0f) * halfExtent;

                // Nearest point on the inner cube face
                float cu = std::clamp(su, -inner, inner);
                float cv = std::clamp(sv, -inner, inner);

                // Offset direction (from inner cube point toward the actual surface point)
                float du = su - cu;
                float dv = sv - cv;
                float dn = face.sign * radius; // always push outward along face normal

                // Compute the actual normal direction
                float normU = du;
                float normV = dv;
                float normN = face.sign * radius;
                float normLen = std::sqrt(normU * normU + normV * normV + normN * normN);
                if (normLen > 0.0001f)
                {
                    normU /= normLen;
                    normV /= normLen;
                    normN /= normLen;
                }

                // Final position = inner cube point + radius * normalized offset
                float posU = cu + normU * radius;
                float posV = cv + normV * radius;
                float posN = face.sign * inner + normN * radius;

                // Map back to XYZ
                PreviewVertex vert{};
                vert.position[face.uAxis] = posU * face.uSign;
                vert.position[face.vAxis] = posV * face.vSign;
                vert.position[face.axis] = posN;

                vert.normal[face.uAxis] = normU * face.uSign;
                vert.normal[face.vAxis] = normV * face.vSign;
                vert.normal[face.axis] = normN;

                // Tangent aligned to U direction
                vert.tangent[face.uAxis] = face.uSign;
                vert.tangent[face.vAxis] = 0;
                vert.tangent[face.axis] = 0;
                vert.tangent[3] = 1.0f;

                // UV: flip V so texture top is at geometric top
                vert.uv[0] = u;
                vert.uv[1] = 1.0f - v;

                mesh.vertices.push_back(vert);
            }
        }

        // Generate indices for this face grid
        // Use same winding as Cube (reversed: 0,2,1 / 0,3,2)
        for (int j = 0; j < N; ++j)
        {
            for (int i = 0; i < N; ++i)
            {
                uint32_t a = baseVertex + j * (N + 1) + i;
                uint32_t b = a + 1;
                uint32_t c = a + (N + 1);
                uint32_t d = c + 1;

                // Two triangles per quad, reversed winding to match Cube
                mesh.indices.push_back(a);
                mesh.indices.push_back(c);
                mesh.indices.push_back(b);

                mesh.indices.push_back(b);
                mesh.indices.push_back(c);
                mesh.indices.push_back(d);
            }
        }
    }

    return mesh;
}

} // namespace tpbr
