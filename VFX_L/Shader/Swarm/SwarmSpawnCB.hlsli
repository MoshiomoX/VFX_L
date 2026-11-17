// ============================================================
// SwarmSpawnCB.hlsli
// CPU からの生成リクエストを空きスロットへ入れる CS（SwarmSpawnEnemyCS / SpawnProjCS /
// SpawnAreaCS）の cbuffer（b1）。C++ 側：Swarm::SpawnCB
// ============================================================
#ifndef SWARM_SPAWN_CB_HLSLI
#define SWARM_SPAWN_CB_HLSLI

cbuffer SwarmSpawnCB : register(b1)
{
    uint g_RequestCount;   // このディスパッチのリクエスト数
    uint g_ScanStart;      // 空きスロットを探し始める位置（毎フレームずらす）
    uint2 _spawnPad;
};

#endif
