// ============================================================
// PrimitiveBuilder.cpp
// ============================================================
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Mesh/Mesh.h"
#include <vector>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    const float PI = 3.14159265358979f;

    VERTEX_3D MakeVertex(const Vector3& pos, const Vector3& normal,
        const Vector2& uv, const Vector4& color)
    {
        VERTEX_3D v = {};
        v.position = pos;
        v.normal = normal;
        v.tangent = Vector3(1, 0, 0);
        v.uv = uv;
        v.color = color;
        return v;
    }
}

namespace PrimitiveBuilder
{
    // ========================================================
    // Box：6面 × 4頂点（面ごとに法線を分けるため頂点は共有しない）
    // ========================================================
    std::shared_ptr<Model> CreateBox(ID3D11Device* device, const Vector3& he,
        const Vector4& color)
    {
        std::vector<VERTEX_3D> verts;
        std::vector<unsigned int> indices;

        // 各面：法線 n、面内の u 軸・v 軸
        struct Face { Vector3 n, u, v; };
        Face faces[6] = {
            { { 0, 0,-1}, { 1, 0, 0}, { 0, 1, 0} },   // -Z
            { { 0, 0, 1}, {-1, 0, 0}, { 0, 1, 0} },   // +Z
            { {-1, 0, 0}, { 0, 0,-1}, { 0, 1, 0} },   // -X
            { { 1, 0, 0}, { 0, 0, 1}, { 0, 1, 0} },   // +X
            { { 0,-1, 0}, { 1, 0, 0}, { 0, 0,-1} },   // -Y
            { { 0, 1, 0}, { 1, 0, 0}, { 0, 0, 1} },   // +Y
        };

        for (int f = 0; f < 6; ++f)
        {
            const Face& face = faces[f];
            Vector3 center(face.n.x * he.x, face.n.y * he.y, face.n.z * he.z);
            Vector3 uAxis(face.u.x * he.x, face.u.y * he.y, face.u.z * he.z);
            Vector3 vAxis(face.v.x * he.x, face.v.y * he.y, face.v.z * he.z);

            unsigned int base = (unsigned int)verts.size();
            verts.push_back(MakeVertex(center - uAxis - vAxis, face.n, { 0, 1 }, color));
            verts.push_back(MakeVertex(center - uAxis + vAxis, face.n, { 0, 0 }, color));
            verts.push_back(MakeVertex(center + uAxis + vAxis, face.n, { 1, 0 }, color));
            verts.push_back(MakeVertex(center + uAxis - vAxis, face.n, { 1, 1 }, color));

            // 左手系 CW 巻き順（外側から見て時計回り）
            indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 1);
            indices.push_back(base + 0); indices.push_back(base + 3); indices.push_back(base + 2);
        }

        auto mesh = std::make_shared<Mesh>();
        if (!mesh->Create(device, verts, indices)) return nullptr;

        auto model = std::make_shared<Model>();
        model->AddSubMesh(mesh);
        return model;
    }

    // ========================================================
    // Sphere：UV球（経緯度分割）
    // ========================================================
    std::shared_ptr<Model> CreateSphere(ID3D11Device* device, float radius,
        const Vector4& color, int segments)
    {
        std::vector<VERTEX_3D> verts;
        std::vector<unsigned int> indices;

        int stacks = segments;
        int slices = segments * 2;

        for (int i = 0; i <= stacks; ++i)
        {
            float phi = PI * i / stacks;      // 0（上）〜π（下）
            float y = std::cos(phi);
            float r = std::sin(phi);

            for (int j = 0; j <= slices; ++j)
            {
                float theta = 2.0f * PI * j / slices;
                Vector3 n(r * std::cos(theta), y, r * std::sin(theta));
                verts.push_back(MakeVertex(n * radius, n,
                    { (float)j / slices, (float)i / stacks }, color));
            }
        }

        for (int i = 0; i < stacks; ++i)
        {
            for (int j = 0; j < slices; ++j)
            {
                unsigned int a = i * (slices + 1) + j;
                unsigned int b = a + slices + 1;
                // 左手系 CW 巻き順
                indices.push_back(a);     indices.push_back(a + 1); indices.push_back(b);
                indices.push_back(a + 1); indices.push_back(b + 1); indices.push_back(b);
            }
        }

        auto mesh = std::make_shared<Mesh>();
        if (!mesh->Create(device, verts, indices)) return nullptr;

        auto model = std::make_shared<Model>();
        model->AddSubMesh(mesh);
        return model;
    }

    // ========================================================
    // Capsule：上半球 + 円柱 + 下半球
    //   衝突体と同じ定義（height = 円柱部のみ、全高 = height + 2*radius）
    // ========================================================
    std::shared_ptr<Model> CreateCapsule(ID3D11Device* device,
        float radius, float height,
        const Vector4& color, int segments)
    {
        std::vector<VERTEX_3D> verts;
        std::vector<unsigned int> indices;

        int slices = segments * 2;
        int halfStacks = segments / 2;
        float halfH = height * 0.5f;

        // リングを1周ぶん追加（y位置、リング半径、法線、V座標）
        auto addRing = [&](float y, float ringR, const Vector3& nRef, float vCoord)
            {
                for (int j = 0; j <= slices; ++j)
                {
                    float theta = 2.0f * PI * j / slices;
                    float cx = std::cos(theta);
                    float cz = std::sin(theta);
                    Vector3 pos(cx * ringR, y, cz * ringR);
                    Vector3 n(cx * nRef.x, nRef.y, cz * nRef.z);
                    n.Normalize();
                    verts.push_back(MakeVertex(pos, n, { (float)j / slices, vCoord }, color));
                }
            };

        int totalRings = 0;

        // --- 上半球：頂点(phi=0) → 円柱上端(phi=π/2) ---
        for (int i = 0; i <= halfStacks; ++i)
        {
            float phi = (PI * 0.5f) * i / halfStacks;
            float y = halfH + std::cos(phi) * radius;
            float r = std::sin(phi) * radius;
            addRing(y, r, Vector3(1, std::cos(phi), 1), (float)i / (halfStacks * 2 + 1));
            ++totalRings;
        }

        // --- 円柱下端（法線は水平）---
        addRing(-halfH, radius, Vector3(1, 0, 1), 0.5f);
        ++totalRings;

        // --- 下半球：円柱下端の少し下 → 最下点 ---
        for (int i = 1; i <= halfStacks; ++i)
        {
            float phi = (PI * 0.5f) * i / halfStacks;
            float y = -halfH - std::sin(phi) * radius;
            float r = std::cos(phi) * radius;
            addRing(y, r, Vector3(1, -std::sin(phi), 1),
                0.5f + (float)i / (halfStacks * 2 + 1));
            ++totalRings;
        }

        // リング間を三角形で繋ぐ（左手系 CW）
        for (int i = 0; i < totalRings - 1; ++i)
        {
            for (int j = 0; j < slices; ++j)
            {
                unsigned int a = i * (slices + 1) + j;
                unsigned int b = a + slices + 1;
                indices.push_back(a);     indices.push_back(a + 1); indices.push_back(b);
                indices.push_back(a + 1); indices.push_back(b + 1); indices.push_back(b);
            }
        }

        auto mesh = std::make_shared<Mesh>();
        if (!mesh->Create(device, verts, indices)) return nullptr;

        auto model = std::make_shared<Model>();
        model->AddSubMesh(mesh);
        return model;
    }
}