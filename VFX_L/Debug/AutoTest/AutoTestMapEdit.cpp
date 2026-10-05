// ============================================================
// AutoTestMapEdit.cpp
// TEMP-TEST: VFXL_MAPEDIT_AUTOTEST=1
// F6 の地図エディタ（Scene/MapEditMode）：seed 12345 の草原を開き、道を塞ぐ木を 1 本動かして
// 衝突の箱・塞いだマスが付いて来るか、報酬の箱と Boss の門を足して保存 → 読み直して同じかを autotest.log へ。
// 保存先は Assets/Data/MapData/_edittest.vmap（戦闘側の確認に使うので消さない：VFXL_MAP=_edittest + mapio）
// ============================================================
#include "Debug/AutoTest/AutoTestMapEdit.h"
#include "Scene/MapEditMode.h"
#include "Camera/FlyCamera.h"
#include "Graphics/Model/Model.h"
#include "World/GridWorld.h"
#include <Windows.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>

using DirectX::SimpleMath::Vector3;

namespace
{
    float s_Time = 0.0f;
    int   s_Step = 0;
    uint32_t s_Group = 0;
    Vector3  s_OldPos, s_OldBoxLo;
    int      s_OldCell = -1;

    void Log(const char* what)
    {
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::ofstream f("autotest.log", std::ios::app);
        f << ms << " " << what << " (t " << s_Time << " s)" << std::endl;
    }

    int CellOf(const MapData::Map& m, const Vector3& p)
    {
        const float cs = GridWorld::kCellSize;
        const int gx = (int)std::floor((p.x + 0.5f * m.gw * cs) / cs), gz = (int)std::floor((p.z + 0.5f * m.gd * cs) / cs);
        return (gx < 0 || gz < 0 || gx >= m.gw || gz >= m.gd) ? -1 : gz * m.gw + gx;
    }
}

bool MapEditAutoTest::Enabled()
{
    static const bool on = GetEnvironmentVariableA("VFXL_MAPEDIT_AUTOTEST", nullptr, 0) > 0;
    return on;
}

// 0.5 秒：生成して開く。中央から 15〜45m の「幹の衝突がある木」を 1 本選んで寄る → 1.5 秒 `mapedit look before`
// 2 秒：その木を (6, 0, 4) 動かす（地面に付いていく）。箱が同じだけ動いたか、元のマスが開いて新しいマスが塞がったか → 3 秒 `look after`
// 3.5 秒：木の横に報酬の箱、反対側に Boss の門を足して門を選ぶ → 4.5 秒 `look prefabs`
// 5 秒：_edittest として保存、読み直してバイト列が同じか → `mapedit done`
void MapEditAutoTest::Update(MapEditMode& edit, FlyCamera& camera, float dt)
{
    s_Time += dt;
    char line[320];
    MapData::Map& map = edit.TestMap();
    if (s_Step > 0 && s_Step < 7 && s_Group == 0) { Log("mapedit no tree found"); Log("mapedit done"); s_Step = 7; return; }

    if (s_Step == 0 && s_Time >= 0.5f)
    {
        edit.TestOpen(12345u, 0);
        for (const auto& b : map.boxes)
        {
            if (b.tag.kind != MapData::kTree) continue;
            const int pi = MapEdit::FindProp(map, b.tag.group);
            if (pi < 0) continue;
            const Vector3 p = map.props[(size_t)pi].pos;
            const float d = std::sqrt(p.x * p.x + p.z * p.z);
            if (d < 12.0f || d > 90.0f) continue;
            s_Group = b.tag.group;
            s_OldPos = p;
            s_OldBoxLo = b.lo;
            break;
        }
        s_OldCell = CellOf(map, s_OldPos);
        snprintf(line, sizeof(line),
            "mapedit open props %zu boxes %zu blocks %zu walkableMatchesBlocks %d tree group %u at %.2f,%.2f,%.2f simple %d collision %d cellWalkable %d",
            map.props.size(), map.boxes.size(), map.blocks.size(), (int)MapEdit::WalkableMatchesBlocks(map), s_Group,
            s_OldPos.x, s_OldPos.y, s_OldPos.z, (int)MapEdit::IsSimpleProp(map, s_Group),
            (int)MapEdit::CollisionOf(map, s_Group), s_OldCell >= 0 ? (int)map.walkable[(size_t)s_OldCell] : -1);
        Log(line);
        edit.TestSelectGroup(s_Group);
        camera.Place(s_OldPos + Vector3(3.0f, 9.0f, -14.0f), 0.0f, 28.0f);
        s_Step = 1;
    }
    else if (s_Step == 1 && s_Time >= 1.5f) { Log("mapedit look before"); s_Step = 2; }
    else if (s_Step == 2 && s_Time >= 2.0f)
    {
        const bool moved = edit.TestApplyMove({ 6.0f, 0.0f, 4.0f });
        const int pi = MapEdit::FindProp(map, s_Group);
        const Vector3 p = map.props[(size_t)pi].pos;
        Vector3 boxLo;
        int boxes = 0, blockCells = 0;
        for (const auto& b : map.boxes) if (b.tag.group == s_Group) { boxLo = b.lo; ++boxes; }
        for (const auto& b : map.blocks) if (b.tag.group == s_Group) blockCells += b.w * b.d;
        const int newCell = CellOf(map, p);
        snprintf(line, sizeof(line),
            "mapedit move ok %d pos %.2f,%.2f,%.2f ground %.2f boxes %d boxDelta %.2f,%.2f,%.2f blockCells %d oldCellWalkable %d newCellWalkable %d",
            (int)moved, p.x, p.y, p.z, MapEdit::GroundHeight(map, p.x, p.z), boxes,
            boxLo.x - s_OldBoxLo.x, boxLo.y - s_OldBoxLo.y, boxLo.z - s_OldBoxLo.z, blockCells,
            s_OldCell >= 0 ? (int)map.walkable[(size_t)s_OldCell] : -1, newCell >= 0 ? (int)map.walkable[(size_t)newCell] : -1);
        Log(line);
        s_Step = 3;
    }
    else if (s_Step == 3 && s_Time >= 3.0f) { Log("mapedit look after"); s_Step = 4; }
    else if (s_Step == 4 && s_Time >= 3.5f)
    {
        const Vector3 p = map.props[(size_t)MapEdit::FindProp(map, s_Group)].pos;
        auto ground = [&](float x, float z) { return Vector3(x, MapEdit::GroundHeight(map, x, z), z); };
        edit.TestAddPlacement(MapData::kPlaceCrate, ground(p.x + 3.0f, p.z - 2.0f));
        edit.TestAddPlacement(MapData::kPlaceCrate, ground(p.x - 3.0f, p.z - 3.0f));
        const int gate = edit.TestAddPlacement(MapData::kPlaceBossGate, ground(p.x - 1.0f, p.z + 7.0f));
        map.placements[(size_t)gate].yawDeg = 180.0f;
        edit.TestSelectPlacement(gate);
        snprintf(line, sizeof(line), "mapedit prefabs placements %zu", map.placements.size());
        Log(line);

        // 手で置く見えない体積（3 歩目）：6 x 3 x 4 m を木の右へ。衝突の箱が 1 つ増え、足跡のマスが塞がる →
        // 4m 動かすとマスが付いて来る → 「雑魚を塞ぐ」を切るとマスが開く（箱は残る）→ 戻す
        auto cells = [&](uint32_t g) { int n = 0; for (const auto& b : map.blocks) if (b.tag.group == g) n += b.w * b.d; return n; };
        auto boxesOf = [&](uint32_t g) { int n = 0; for (const auto& b : map.boxes) if (b.tag.group == g) ++n; return n; };
        auto walk = [&](const Vector3& q) { const int c = CellOf(map, q); return c >= 0 ? (int)map.walkable[(size_t)c] : -1; };
        Vector3 c0 = ground(p.x + 7.0f, p.z + 1.0f);
        c0.y += 1.2f;
        const uint32_t vg = MapEdit::AddVolume(map, c0, { 3.0f, 1.5f, 2.0f }, true, true);
        const int cellsAdd = cells(vg), boxesAdd = boxesOf(vg), walkAdd = walk(c0);
        edit.TestSelectGroup(vg);
        edit.TestApplyMove({ 4.0f, 0.0f, 0.0f });
        const Vector3 c1 = map.volumes[(size_t)MapEdit::FindVolume(map, vg)].center;
        const int cellsMove = cells(vg), walkOld = walk(c0 - Vector3(2.5f, 0.0f, 0.0f)), walkNew = walk(c1);
        map.volumes[(size_t)MapEdit::FindVolume(map, vg)].blockMobs = false;
        MapEdit::ApplyVolume(map, vg);
        const int cellsOff = cells(vg), boxesOff = boxesOf(vg), walkOff = walk(c1);
        map.volumes[(size_t)MapEdit::FindVolume(map, vg)].blockMobs = true;
        MapEdit::ApplyVolume(map, vg);
        snprintf(line, sizeof(line),
            "mapedit volume add boxes %d cells %d centerWalkable %d | move dx %.2f cells %d oldEdgeWalkable %d newCenterWalkable %d | mobsOff boxes %d cells %d centerWalkable %d | back cells %d matches %d",
            boxesAdd, cellsAdd, walkAdd, c1.x - c0.x, cellsMove, walkOld, walkNew, boxesOff, cellsOff, walkOff, cells(vg),
            (int)MapEdit::WalkableMatchesBlocks(map));
        Log(line);
        edit.TestSelectGroup(vg);
        edit.TestFocus(camera);   // 体積の線（水色 → 選択中は黄）と塞いだマス（赤）を写す
        s_Step = 5;
    }
    else if (s_Step == 5 && s_Time >= 4.5f) { Log("mapedit look prefabs"); s_Step = 6; }
    else if (s_Step == 6 && s_Time >= 5.0f)
    {
        std::vector<uint8_t> a, b;
        MapData::Serialize(map, a);
        const bool saved = MapData::Save("_edittest", map);
        MapData::Map back;
        const bool loaded = MapData::Load("_edittest", back);
        if (loaded) MapData::Serialize(back, b);
        snprintf(line, sizeof(line), "mapedit save %d load %d sameBytes %d bytes %zu walkableMatchesBlocks %d",
            (int)saved, (int)loaded, (int)(a == b), a.size(), (int)MapEdit::WalkableMatchesBlocks(map));
        Log(line);
        Log("mapedit done");
        s_Step = 7;
    }
}
