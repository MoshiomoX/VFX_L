// ============================================================
// PrimitiveBuilder.cpp
// ============================================================
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Mesh/Mesh.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_map>

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
    // Hexahedron：8 頂点の六面体（台形柱・楔・斜坡）。
    // 頂点順は CollisionMath::ConvexFromHexahedron と同じ:
    //   下面 0-3（-x-z, +x-z, +x+z, -x+z）、上面 4-7 が対応。
    // 面ごとに平面法線を付ける（箱と同じ見え方）
    // ========================================================
    std::shared_ptr<Model> CreateHexahedron(ID3D11Device* device, const Vector3 v[8],
        const Vector4& color)
    {
        return CreateHexahedron(device, v, color, color);
    }

    std::shared_ptr<Model> CreateHexahedron(ID3D11Device* device, const Vector3 v[8],
        const Vector4& topColor, const Vector4& sideColor)
    {
        HexahedronBatch batch;
        batch.Append(v, topColor, sideColor);
        return batch.Build(device);
    }

    std::shared_ptr<Model> HexahedronBatch::Build(ID3D11Device* device) const
    {
        if (m_Indices.empty()) return nullptr;
        auto mesh = std::make_shared<Mesh>();
        if (!mesh->Create(device, m_Verts, m_Indices)) return nullptr;

        auto model = std::make_shared<Model>();
        model->AddSubMesh(mesh);
        return model;
    }

    void HexahedronBatch::Append(const Vector3 v[8], const Vector4& topColor, const Vector4& sideColor)
    {
        std::vector<VERTEX_3D>& verts = m_Verts;
        std::vector<unsigned int>& indices = m_Indices;

        Vector3 centroid;
        for (int i = 0; i < 8; ++i) centroid += v[i];
        centroid /= 8.0f;

        // CreateBox と同じ並び（外から見て 左下・左上・右上・右下）
        static const int faces[6][4] = {
            { 0, 4, 5, 1 },   // -Z
            { 2, 6, 7, 3 },   // +Z
            { 3, 7, 4, 0 },   // -X
            { 1, 5, 6, 2 },   // +X
            { 3, 0, 1, 2 },   // -Y
            { 4, 7, 6, 5 },   // +Y
        };
        static const Vector2 uvs[4] = { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } };

        for (int f = 0; f < 6; ++f)
        {
            const Vector3& a = v[faces[f][0]];
            const Vector3& b = v[faces[f][1]];
            const Vector3& c = v[faces[f][2]];
            Vector3 n = (b - a).Cross(c - a);
            n.Normalize();
            if (n.Dot(a - centroid) < 0.0f) n = -n;   // 外向きに揃える

            // 上を向いた面（坂の上面も）だけ top の色
            const Vector4& color = (n.y > 0.5f) ? topColor : sideColor;
            unsigned int base = (unsigned int)verts.size();
            for (int k = 0; k < 4; ++k)
                verts.push_back(MakeVertex(v[faces[f][k]], n, uvs[k], color));

            indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 1);
            indices.push_back(base + 0); indices.push_back(base + 3); indices.push_back(base + 2);
        }
    }

    // ========================================================
    // ColoredGrid：細分化した水平面。頂点は共有し（色が滑らかに繋がる）、
    // 法線は全部 +Y。巻き順は CreateBox の +Y 面と同じ（上から見て CW）。
    // uv は 2m で 1 周（貼図を載せる時用）
    // ========================================================
    std::shared_ptr<Model> CreateColoredGrid(ID3D11Device* device,
        float sizeX, float sizeZ, int divX, int divZ, float y,
        const std::function<Vector4(float x, float z)>& colorAt)
    {
        divX = (std::max)(divX, 1);
        divZ = (std::max)(divZ, 1);
        const int vx = divX + 1, vz = divZ + 1;

        std::vector<VERTEX_3D> verts;
        std::vector<unsigned int> indices;
        verts.reserve((size_t)vx * vz);
        indices.reserve((size_t)divX * divZ * 6);

        for (int iz = 0; iz < vz; ++iz)
            for (int ix = 0; ix < vx; ++ix)
            {
                const float x = -0.5f * sizeX + sizeX * ix / divX;
                const float z = -0.5f * sizeZ + sizeZ * iz / divZ;
                const Vector4 c = colorAt ? colorAt(x, z) : Vector4(1, 1, 1, 1);
                verts.push_back(MakeVertex({ x, y, z }, { 0, 1, 0 }, { x * 0.5f, -z * 0.5f }, c));
            }

        for (int iz = 0; iz < divZ; ++iz)
            for (int ix = 0; ix < divX; ++ix)
            {
                const unsigned int a = (unsigned int)(iz * vx + ix);   // (-x,-z)
                const unsigned int b = a + (unsigned int)vx;           // (-x,+z)
                const unsigned int c = b + 1;                          // (+x,+z)
                const unsigned int d = a + 1;                          // (+x,-z)
                indices.push_back(a); indices.push_back(c); indices.push_back(b);
                indices.push_back(a); indices.push_back(d); indices.push_back(c);
            }

        auto mesh = std::make_shared<Mesh>();
        if (!mesh->Create(device, verts, indices)) return nullptr;

        auto model = std::make_shared<Model>();
        model->AddSubMesh(mesh);
        return model;
    }

    // ========================================================
    // SteppedGrid：段々の地面。上面は ColoredGrid と同じ巻き順・uv。
    // 頂点は (節点, 高さ) で共有する（段の境の節点は高さごとに別の頂点）。
    // 壁の四隅は外（低い側）から見て 左下・左上・右上・右下 = HexahedronBatch の面と同じ並び。
    // 外から見た「左」= 上 × 法線
    // ========================================================
    std::shared_ptr<Model> CreateSteppedGrid(ID3D11Device* device,
        int cellsX, int cellsZ, float cellSize,
        const std::function<float(int gx, int gz)>& levelAt,
        const std::function<Vector4(float x, float z, float level)>& topColorAt,
        const std::function<Vector4(float x, float y, float z)>& wallColorAt,
        float bandHeight)
    {
        cellsX = (std::max)(cellsX, 1);
        cellsZ = (std::max)(cellsZ, 1);
        bandHeight = (std::max)(bandHeight, 0.25f);
        const float x0 = -0.5f * cellsX * cellSize;
        const float z0 = -0.5f * cellsZ * cellSize;

        std::vector<float> level((size_t)cellsX * cellsZ);
        for (int gz = 0; gz < cellsZ; ++gz)
            for (int gx = 0; gx < cellsX; ++gx)
                level[(size_t)gz * cellsX + gx] = levelAt ? levelAt(gx, gz) : 0.0f;

        std::vector<VERTEX_3D> verts;
        std::vector<unsigned int> indices;
        verts.reserve((size_t)(cellsX + 1) * (cellsZ + 1) + 4096);
        indices.reserve((size_t)cellsX * cellsZ * 6 + 4096);

        // ---- 上面 ----
        std::unordered_map<uint64_t, unsigned int> topVert;
        topVert.reserve((size_t)(cellsX + 1) * (cellsZ + 1));
        auto nodeVert = [&](int ix, int iz, float y) -> unsigned int
            {
                uint32_t bits = 0;
                std::memcpy(&bits, &y, sizeof(bits));
                const uint64_t key = ((uint64_t)(iz * (cellsX + 1) + ix) << 32) | bits;
                auto it = topVert.find(key);
                if (it != topVert.end()) return it->second;
                const float x = x0 + ix * cellSize, z = z0 + iz * cellSize;
                const Vector4 c = topColorAt ? topColorAt(x, z, y) : Vector4(1, 1, 1, 1);
                const unsigned int idx = (unsigned int)verts.size();
                verts.push_back(MakeVertex({ x, y, z }, { 0, 1, 0 }, { x * 0.5f, -z * 0.5f }, c));
                topVert.emplace(key, idx);
                return idx;
            };
        for (int gz = 0; gz < cellsZ; ++gz)
            for (int gx = 0; gx < cellsX; ++gx)
            {
                const float y = level[(size_t)gz * cellsX + gx];
                const unsigned int a = nodeVert(gx, gz, y);           // (-x,-z)
                const unsigned int b = nodeVert(gx, gz + 1, y);       // (-x,+z)
                const unsigned int c = nodeVert(gx + 1, gz + 1, y);   // (+x,+z)
                const unsigned int d = nodeVert(gx + 1, gz, y);       // (+x,-z)
                indices.push_back(a); indices.push_back(c); indices.push_back(b);
                indices.push_back(a); indices.push_back(d); indices.push_back(c);
            }

        // ---- 壁 ----
        // 境の辺 p0-p1（y は無視）に lo..hi の壁。n = 高い側から低い側へ
        auto wall = [&](Vector3 p0, Vector3 p1, float lo, float hi, const Vector3& n)
            {
                const Vector3 left = Vector3(0, 1, 0).Cross(n);
                if ((p1 - p0).Dot(left) > 0.0f) std::swap(p0, p1);   // p0 = 左
                const int bands = (std::max)(1, (int)std::ceil((hi - lo) / bandHeight - 0.01f));
                for (int k = 0; k < bands; ++k)
                {
                    const float yb = lo + (hi - lo) * k / bands;
                    const float yt = lo + (hi - lo) * (k + 1) / bands;
                    const Vector3 mid = (p0 + p1) * 0.5f;
                    const Vector4 c = wallColorAt ? wallColorAt(mid.x, (yb + yt) * 0.5f, mid.z) : Vector4(1, 1, 1, 1);
                    const unsigned int base = (unsigned int)verts.size();
                    verts.push_back(MakeVertex({ p0.x, yb, p0.z }, n, { 0, 1 }, c));
                    verts.push_back(MakeVertex({ p0.x, yt, p0.z }, n, { 0, 0 }, c));
                    verts.push_back(MakeVertex({ p1.x, yt, p1.z }, n, { 1, 0 }, c));
                    verts.push_back(MakeVertex({ p1.x, yb, p1.z }, n, { 1, 1 }, c));
                    indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 1);
                    indices.push_back(base + 0); indices.push_back(base + 3); indices.push_back(base + 2);
                }
            };
        for (int gz = 0; gz < cellsZ; ++gz)
            for (int gx = 0; gx < cellsX; ++gx)
            {
                const float la = level[(size_t)gz * cellsX + gx];
                if (gx + 1 < cellsX)   // +x の隣との境（x 一定）
                {
                    const float lb = level[(size_t)gz * cellsX + gx + 1];
                    const float x = x0 + (gx + 1) * cellSize;
                    const Vector3 p0(x, 0, z0 + gz * cellSize), p1(x, 0, z0 + (gz + 1) * cellSize);
                    if (la > lb)      wall(p0, p1, lb, la, { 1, 0, 0 });
                    else if (lb > la) wall(p0, p1, la, lb, { -1, 0, 0 });
                }
                if (gz + 1 < cellsZ)   // +z の隣との境（z 一定）
                {
                    const float lb = level[(size_t)(gz + 1) * cellsX + gx];
                    const float z = z0 + (gz + 1) * cellSize;
                    const Vector3 p0(x0 + gx * cellSize, 0, z), p1(x0 + (gx + 1) * cellSize, 0, z);
                    if (la > lb)      wall(p0, p1, lb, la, { 0, 0, 1 });
                    else if (lb > la) wall(p0, p1, la, lb, { 0, 0, -1 });
                }
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
        // 法線 = 球面の法線そのもの (sinφ·cx, cosφ, sinφ·cz)
        for (int i = 0; i <= halfStacks; ++i)
        {
            float phi = (PI * 0.5f) * i / halfStacks;
            float y = halfH + std::cos(phi) * radius;
            float r = std::sin(phi) * radius;
            addRing(y, r, Vector3(std::sin(phi), std::cos(phi), std::sin(phi)),
                (float)i / (halfStacks * 2 + 1));
            ++totalRings;
        }

        // --- 円柱下端（法線は水平）---
        addRing(-halfH, radius, Vector3(1, 0, 1), 0.5f);
        ++totalRings;

        // --- 下半球：円柱下端の少し下 → 最下点 ---
        // ここは r = cosφ·radius なので水平成分は cosφ、竪直成分は -sinφ
        for (int i = 1; i <= halfStacks; ++i)
        {
            float phi = (PI * 0.5f) * i / halfStacks;
            float y = -halfH - std::sin(phi) * radius;
            float r = std::cos(phi) * radius;
            addRing(y, r, Vector3(std::cos(phi), -std::sin(phi), std::cos(phi)),
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

    // ========================================================
    // Bipyramid：上下 2 つの角錐を赤道で貼り合わせた宝石。
    // 三角形毎に頂点を持つ（平らな面の法線）。
    // 巻き順は CreateBox と同じ：cross(p1 - p0, p2 - p0) が内側を向く
    // ========================================================
    std::shared_ptr<Model> CreateBipyramid(ID3D11Device* device,
        float radius, float top, float bottom, int sides, const Vector4& color)
    {
        sides = (std::max)(sides, 3);
        std::vector<VERTEX_3D> verts;
        std::vector<unsigned int> indices;

        auto addFace = [&](const Vector3& p0, const Vector3& p1, const Vector3& p2)
            {
                Vector3 n = (p1 - p0).Cross(p2 - p0);
                n.Normalize();
                const Vector3 center = (p0 + p1 + p2) / 3.0f;
                // 外向きの法線。cross が外を向いていたら巻きを逆にする
                const bool flip = n.Dot(center) > 0.0f;
                if (!flip) n = -n;

                const unsigned int base = (unsigned int)verts.size();
                verts.push_back(MakeVertex(p0, n, { 0.5f, 0.0f }, color));
                verts.push_back(MakeVertex(p1, n, { 0.0f, 1.0f }, color));
                verts.push_back(MakeVertex(p2, n, { 1.0f, 1.0f }, color));
                indices.push_back(base);
                indices.push_back(flip ? base + 2 : base + 1);
                indices.push_back(flip ? base + 1 : base + 2);
            };

        const Vector3 up(0.0f, top, 0.0f);
        const Vector3 down(0.0f, -bottom, 0.0f);
        for (int i = 0; i < sides; ++i)
        {
            const float a0 = 2.0f * PI * i / sides;
            const float a1 = 2.0f * PI * (i + 1) / sides;
            const Vector3 e0(std::cos(a0) * radius, 0.0f, std::sin(a0) * radius);
            const Vector3 e1(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius);
            addFace(up, e0, e1);
            addFace(down, e1, e0);
        }

        auto mesh = std::make_shared<Mesh>();
        if (!mesh->Create(device, verts, indices)) return nullptr;

        auto model = std::make_shared<Model>();
        model->AddSubMesh(mesh);
        return model;
    }
}