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
#include "World/MapTerrainEdit.h"

#include <Windows.h>

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
    // 計測：開いた後の待ち時間（1.0〜1.5 秒）の平均フレーム時間
    static float s_IdleSum = 0.0f; static int s_IdleN = 0;
    if (s_Step == 1 && s_Time > 1.0f) { s_IdleSum += dt; ++s_IdleN; }
    char line[320];
    MapData::Map& map = edit.TestMap();
    if (s_Step > 0 && s_Step != 10 && s_Group == 0) { Log("mapedit no tree found"); Log("mapedit done"); s_Step = 10; return; }

    if (s_Step == 0 && s_Time >= 0.5f)
    {
        edit.TestOpen(12345u, 0);
        {
            // 地形の部品から作り直した結果が生成器の結果と同じか（4 歩目）
            const MapTerrainEdit::Check c = MapTerrainEdit::Verify(map);
            snprintf(line, sizeof(line), "mapedit rederive blockParts %zu rampParts %zu pads %zu reliefSame %d heightMaxDiff %.4f diffCells %d walkableSame %d grassSame %d recordsSame %d zonesSame %d rawSame %d hills %zu",
                map.blockParts.size(), map.rampParts.size(), map.pads.size(), (int)c.reliefSame, c.heightMaxDiff, c.heightDiffCells,
                (int)c.walkableSame, (int)c.grassSame, (int)c.recordsSame, (int)c.zonesSame, (int)c.rawSame, map.hills.size());
            Log(line);
            Log(("mapedit rederive detail" + c.detail).c_str());
        }
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
    else if (s_Step == 1 && s_Time >= 1.5f)
    {
        char t[96]; snprintf(t, sizeof(t), "mapedit perf idle frame %.2f ms (%d frames)", s_IdleN ? 1000.0f * s_IdleSum / s_IdleN : 0.0f, s_IdleN); Log(t);
        Log("mapedit look before"); s_Step = 2;
    }
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
        // ---- 地形の部品（4 歩目）----
        // 生成された台地（登れる・坂付き）を 1 つ選び、4 マス動かす → 元の所の高さ場が地面に戻り、先の所が上面の高さになる。
        // 新しい台地 + 坂道を足す → 上面の高さ・坂の角度。消す → 高さ場が元に戻る
        auto heightAt = [&](float x, float z) { return MapEdit::GroundHeight(map, x, z); };
        auto center = [&](const MapData::BlockPart& b)
            {
                const float cs = GridWorld::kCellSize;
                return Vector3(-0.5f * map.gw * cs + (b.x + b.w * 0.5f) * cs, 0.0f, -0.5f * map.gd * cs + (b.z + b.d * 0.5f) * cs);
            };
        uint32_t pg = 0;
        for (const auto& b : map.blockParts)
            if (b.tag.kind == MapData::kPlateau && b.raise && b.onGround && MapTerrainEdit::RampCount(map, b.tag.group) > 0)
            { pg = b.tag.group; break; }
        if (pg != 0)
        {
            const MapData::BlockPart before = *MapTerrainEdit::Block(map, pg, 0);
            const Vector3 c0 = center(before);
            const size_t padsBefore = map.pads.size();
            // 動かせる向きを探す（場外・他の物は見ない。場外だけ MoveGroup が弾く）
            const auto t0 = std::chrono::steady_clock::now();
            bool moved = MapTerrainEdit::MoveGroup(map, pg, 4, 0) || MapTerrainEdit::MoveGroup(map, pg, -4, 0);
            { char t[96]; snprintf(t, sizeof(t), "mapedit perf MoveGroup %.0f ms", std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count()); Log(t); }
            const MapData::BlockPart after = *MapTerrainEdit::Block(map, pg, 0);
            const Vector3 c1 = center(after);
            int boxes = 0;
            float boxLoX = 0.0f;
            for (const auto& b : map.boxes) if (b.tag.group == pg) { ++boxes; boxLoX = b.lo.x; }
            snprintf(line, sizeof(line),
                "mapedit part move ok %d cells %d->%d top %.2f->%.2f base %.2f->%.2f | oldCenter h %.2f (was top %.2f) newCenter h %.2f | boxes %d pads %zu->%zu walkableMatches %d",
                (int)moved, before.x, after.x, before.top, after.top, before.base, after.base,
                heightAt(c0.x - (after.x - before.x > 0 ? 3.0f : -3.0f), c0.z), before.top, heightAt(c1.x, c1.z),
                boxes, padsBefore, map.pads.size(), (int)MapEdit::WalkableMatchesBlocks(map));
            Log(line);
            (void)boxLoX;
        }

        // 新しい台地：真ん中の空き地に 6 x 6・3m、+x へ幅 2 の坂道
        const Vector3 tp = map.props[(size_t)MapEdit::FindProp(map, s_Group)].pos;
        const float cs = GridWorld::kCellSize;
        (void)tp;
        const int gx = 59, gz = 60;   // フィールドの真ん中の少し東（開始時の場所の空き地）
        const Vector3 probe(-0.5f * map.gw * cs + (gx + 0.5f) * cs, 0.0f, -0.5f * map.gd * cs + (gz - 11 + 0.5f) * cs);
        const float groundBefore = heightAt(probe.x, probe.z);
        const size_t partsBefore = map.blockParts.size();
        const uint32_t ng = MapTerrainEdit::AddPlateau(map, gx - 3, gz - 14, 6, 6, 3.0f);
        const bool rampOk = MapTerrainEdit::AddRamp(map, ng, 0, 0, 2, 2);
        const MapData::BlockPart nb = *MapTerrainEdit::Block(map, ng, 0);
        const MapData::RampPart nr = *MapTerrainEdit::Ramp(map, ng, 0);
        const float topH = heightAt(probe.x, probe.z);
        const float slope = std::atan2(nr.top - nr.base, nr.w * cs) * 57.29578f;
        snprintf(line, sizeof(line),
            "mapedit part add group %u parts %zu->%zu ramp %d | ground %.2f base %.2f top %.2f heightOnTop %.2f | ramp cells %d,%d %dx%d slope %.1f deg midRampH %.2f",
            ng, partsBefore, map.blockParts.size(), (int)rampOk, groundBefore, nb.base, nb.top, topH,
            nr.x, nr.z, nr.w, nr.d, slope,
            heightAt(-0.5f * map.gw * cs + (nr.x + nr.w * 0.5f) * cs, -0.5f * map.gd * cs + (nr.z + nr.d * 0.5f) * cs));
        Log(line);

        // 複製して消す：消した後の高さ場が、足す前（groundBefore）へ戻るか
        MapData::Map trial = map;
        MapTerrainEdit::DeleteGroup(trial, ng);
        snprintf(line, sizeof(line), "mapedit part delete heightBack %.2f (before %.2f) parts %zu walkableMatches %d",
            MapEdit::GroundHeight(trial, probe.x, probe.z), groundBefore, trial.blockParts.size(),
            (int)MapEdit::WalkableMatchesBlocks(trial));
        Log(line);


        // ---- 区域の塗り替え（4 歩目の 2 段目）----
        // 真ん中の西（マス 38,50）に山頂、真ん中の北西（マス 46,56）に洞窟を半径 3 マスで塗って作り直す：
        // 高さ場が +16m / -10m になる、洞の岩の壁（塞いだマス）・屋根の岩・松明が増える、山頂 / 洞窟のマスの表が増える。
        // 新しい坑に下り坂を足す → 上端の先（マス 50,55）が洞の口になり、岩の壁から外れて歩けるようになる
        {
            auto cellCenter = [&](int x, int z) { return Vector3(-0.5f * map.gw * cs + (x + 0.5f) * cs, 0.0f, -0.5f * map.gd * cs + (z + 0.5f) * cs); };
            auto countZone = [&](uint8_t zn) { return (int)std::count(map.zone.begin(), map.zone.end(), zn); };
            auto countRing = [&]() { return (int)std::count(map.caveRing.begin(), map.caveRing.end(), (uint8_t)1); };
            auto countKind = [&](uint16_t k) { int n = 0; for (const auto& p : map.props) if (p.tag.kind == k) ++n; return n; };
            const int summit0 = countZone(1), mine0 = countZone(2), ring0 = countRing(), rocks0 = countKind(MapData::kRoofRock);
            const size_t sc0 = map.summitCells.size(), mc0 = map.mineCells.size(), torch0 = map.torches.size();
            const int painted = MapTerrainEdit::PaintZone(map, 38, 50, 3, 1) + MapTerrainEdit::PaintZone(map, 46, 56, 3, 2);
            MapTerrainEdit::RegenZones(map);
            const Vector3 ps = cellCenter(38, 50), pm = cellCenter(46, 56);
            snprintf(line, sizeof(line),
                "mapedit zone paint cells %d summit %d->%d mine %d->%d | h summit %.2f mine %.2f | ring %d->%d roofRocks %d->%d lights %zu->%zu | summitCells %zu->%zu mineCells %zu->%zu walkableMatches %d",
                painted, summit0, countZone(1), mine0, countZone(2), heightAt(ps.x, ps.z), heightAt(pm.x, pm.z),
                ring0, countRing(), rocks0, countKind(MapData::kRoofRock), torch0, map.torches.size(),
                sc0, map.summitCells.size(), mc0, map.mineCells.size(), (int)MapEdit::WalkableMatchesBlocks(map));
            Log(line);

            const size_t mouthCell = (size_t)55 * map.gw + 50;
            const int ringBefore = map.caveRing[mouthCell], walkBefore = map.walkable[mouthCell];
            const uint32_t rg = MapTerrainEdit::AddZoneRamp(map, false, 45, 55, 1, 2, 5);   // -x へ下る、幅 2・長さ 5（坑が小さいので急）
            const Vector3 pr = cellCenter(47, 55);
            snprintf(line, sizeof(line),
                "mapedit zone mineRamp group %u | mouth cell ring %d->%d walkable %d->%d | midRamp h %.2f | mineRamps %zu hasDeep %d",
                rg, ringBefore, (int)map.caveRing[mouthCell], walkBefore, (int)map.walkable[mouthCell],
                heightAt(pr.x, pr.z), map.mineRamps.size(), (int)map.hasMineDeep);
            Log(line);
        }


        // ---- 起伏の中身：丘の部品と全体の設定 ----
        // 丘（半径 9m・高さ 3m）を世界 (24, -34) へ足す → そこが約 3m 上がる。12m 動かす → 元の所は戻り、先の所が上がる。
        // 丘の高さの設定を半分にする → 起伏の幅が縮む → 台地の足元を合わせ直す（ReseatAll）→ 設定を戻す
        {
            auto ms = [](std::chrono::steady_clock::time_point a) { return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - a).count(); };
            // 部品も区域の境も無い平原の空き地を探す（中心と 12m 先の両方、周り 14m に地形の部品が無い所）
            float hx = 24.0f, hz = -34.0f;
            {
                auto freeAt = [&](float x, float z)
                    {
                        for (float dz = -14.0f; dz <= 14.0f; dz += 7.0f)
                            for (float dx = -14.0f; dx <= 14.0f; dx += 7.0f)
                            {
                                const int cx = (int)std::floor((x + dx + 0.5f * map.gw * cs) / cs), cz = (int)std::floor((z + dz + 0.5f * map.gd * cs) / cs);
                                if (cx < 3 || cz < 3 || cx >= map.gw - 3 || cz >= map.gd - 3) return false;
                                if (map.zone[(size_t)cz * map.gw + cx] != 0 || MapTerrainEdit::GroupAt(map, x + dx, z + dz) != 0) return false;
                            }
                        return true;
                    };
                bool found = false;
                for (float z = -70.0f; z <= 70.0f && !found; z += 6.0f)
                    for (float x = -70.0f; x <= 58.0f && !found; x += 6.0f)
                        if (freeAt(x, z) && freeAt(x + 12.0f, z)) { hx = x; hz = z; found = true; }
            }
            const float a0 = heightAt(hx, hz), b0 = heightAt(hx + 12.0f, hz);
            const size_t hills0 = map.hills.size();
            const uint32_t hg = MapTerrainEdit::AddHill(map, hx, hz, 9.0f, 3.0f);
            MapTerrainEdit::Rederive(map);
            const float a1 = heightAt(hx, hz);
            auto t0 = std::chrono::steady_clock::now();
            map.hills[(size_t)MapTerrainEdit::FindHill(map, hg)].x += 12.0f;
            MapTerrainEdit::RebuildRaw(map);
            MapTerrainEdit::Rederive(map);
            const float moveMs = ms(t0);
            const float a2 = heightAt(hx, hz), b2 = heightAt(hx + 12.0f, hz);

            auto range = [&]() { const auto mm = std::minmax_element(map.reliefPlain.begin(), map.reliefPlain.end()); return *mm.second - *mm.first; };
            const float range0 = range(), baseBefore = MapTerrainEdit::Block(map, ng, 0)->base;
            const float keep = map.reliefParams.hillHeight;
            t0 = std::chrono::steady_clock::now();
            map.reliefParams.hillHeight = keep * 0.5f;
            MapTerrainEdit::RebuildRaw(map);
            MapTerrainEdit::Rederive(map);
            const float paramMs = ms(t0);
            t0 = std::chrono::steady_clock::now();
            MapTerrainEdit::ReseatAll(map);
            const float reseatMs = ms(t0);
            const float range1 = range(), baseHalf = MapTerrainEdit::Block(map, ng, 0)->base;
            map.reliefParams.hillHeight = keep;
            MapTerrainEdit::RebuildRaw(map);
            MapTerrainEdit::ReseatAll(map);
            snprintf(line, sizeof(line),
                "mapedit hill at %.0f,%.0f parts %zu->%zu | add: %.2f -> %.2f | move 12m: old spot %.2f (was %.2f), new spot %.2f -> %.2f | hillHeight x0.5: plain range %.2f -> %.2f, plateau base %.2f -> %.2f, back -> range %.2f base %.2f | walkableMatches %d",
                hx, hz, hills0, map.hills.size(), a0, a1, a2, a0, b0, b2, range0, range1, baseBefore, baseHalf, range(),
                MapTerrainEdit::Block(map, ng, 0)->base, (int)MapEdit::WalkableMatchesBlocks(map));
            Log(line);
            snprintf(line, sizeof(line), "mapedit perf hill move %.0f ms param change %.0f ms ReseatAll %.0f ms", moveMs, paramMs, reseatMs);
            Log(line);
        }

        // ---- 起伏の筆（4 歩目の 3 段目）----
        // 真ん中の少し南西（世界 -4, -14）へ半径 10m の筆：8m 下げる（傾きの上限で浅くなる・床の箱も下がる）→
        // 高さ 1m に平らにする → 3m 上げる（丘が出来る）。25m 離れた所は変わらない
        {
            using MapTerrainEdit::BrushMode;
            const float bx = -4.0f, bz = -14.0f, br = 10.0f;
            auto maxSlopeDeg = [&]()   // 筆の中心を通る x 方向の線の上で一番急な所
                {
                    float m = 0.0f;
                    for (float x = bx - br - 4.0f; x < bx + br + 4.0f; x += 0.5f)
                        m = (std::max)(m, std::fabs(heightAt(x + 0.5f, bz) - heightAt(x, bz)) / 0.5f);
                    return std::atan(m) * 57.29578f;
                };
            auto floorTop = [&]()   // 平原の床の箱の上面（一番大きい箱）
                {
                    float top = -1.0e9f, area = 0.0f;
                    for (const auto& b : map.boxes)
                    {
                        if (b.tag.kind != MapData::kFloor) continue;
                        const float a = (b.hi.x - b.lo.x) * (b.hi.z - b.lo.z);
                        if (a > area && b.hi.y < 8.0f && b.hi.y > -9.0f) { area = a; top = b.hi.y; }
                    }
                    return top;
                };
            const float h0 = heightAt(bx, bz), far0 = heightAt(bx - 25.0f, bz), floor0 = floorTop();
            const int n1 = MapTerrainEdit::BrushRelief(map, bx, bz, br, BrushMode::Lower, 8.0f);
            MapTerrainEdit::FinishBrush(map);
            const float h1 = heightAt(bx, bz), slope1 = maxSlopeDeg(), floor1 = floorTop();
            const float plainMin = *std::min_element(map.reliefPlain.begin(), map.reliefPlain.end());
            for (int i = 0; i < 8; ++i) MapTerrainEdit::BrushRelief(map, bx, bz, br, BrushMode::Flatten, 1.0f, 1.0f);
            MapTerrainEdit::FinishBrush(map);
            const float h2 = heightAt(bx, bz);
            MapTerrainEdit::BrushRelief(map, bx, bz, br, BrushMode::Raise, 3.0f);
            for (int i = 0; i < 4; ++i) MapTerrainEdit::BrushRelief(map, bx + 6.0f, bz, 5.0f, BrushMode::Smooth, 1.0f);
            MapTerrainEdit::FinishBrush(map);
            const float h3 = heightAt(bx, bz);
            snprintf(line, sizeof(line),
                "mapedit brush nodes %d | start %.2f lower8 -> %.2f (slope max %.1f deg, floor box top %.2f -> %.2f, plain min %.2f) | flatten to 1 -> %.2f | raise3 -> %.2f (slope max %.1f deg) | far %.2f -> %.2f | walkableMatches %d",
                n1, h0, h1, slope1, floor0, floor1, plainMin, h2, h3, maxSlopeDeg(), far0, heightAt(bx - 25.0f, bz),
                (int)MapEdit::WalkableMatchesBlocks(map));
            Log(line);
        }

        { const auto t0 = std::chrono::steady_clock::now(); MapTerrainEdit::Rederive(map); const auto t1 = std::chrono::steady_clock::now(); MapTerrainEdit::RegenZones(map); const auto t2 = std::chrono::steady_clock::now(); edit.TestRebuildView(); const auto t3 = std::chrono::steady_clock::now();
          char t[160]; snprintf(t, sizeof(t), "mapedit perf Rederive %.0f ms RegenZones %.0f ms RebuildView %.0f ms", std::chrono::duration<float, std::milli>(t1 - t0).count(), std::chrono::duration<float, std::milli>(t2 - t1).count(), std::chrono::duration<float, std::milli>(t3 - t2).count()); Log(t);
          Log(("mapedit perf detail " + MapTerrainEdit::detail::LastPerf()).c_str()); }
        edit.TestSelectGroup(ng);
        edit.TestFocus(camera);
        s_Step = 7;
    }
    else if (s_Step == 7 && s_Time >= 6.5f)
    {
        Log("mapedit look parts");
        // 塗った山頂（西）と洞窟（南）が両方入る所へ
        edit.TestSelectGroup(0);
        camera.Place({ -15.0f, 75.0f, -75.0f }, 0.0f, 45.0f);
        s_Step = 8;
    }
    else if (s_Step == 8 && s_Time >= 7.5f)
    {
        Log("mapedit look zones");
        camera.Place({ -4.0f, 22.0f, -48.0f }, 0.0f, 30.0f);   // 筆で作った丘（世界 -4, -14）
        s_Step = 11;
    }
    else if (s_Step == 11 && s_Time >= 8.5f) { Log("mapedit look hills"); s_Step = 9; }
    else if (s_Step == 9 && s_Time >= 9.0f)
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
        s_Step = 10;
    }
}
