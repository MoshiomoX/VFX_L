// ============================================================
// SpawnWaves.h
// 雑魚の湧きの「波」（2026-10-07、ユーザー「難度曲線がおかしい：序盤が退屈、5〜8 分と 10:00 で急に辛い」。
// curve 自動テストで 2〜9 分の圧が平らな一直線だった → 推奨の「波」を選んだ）。
//   難度の表（DifficultyCurve）が底の湧きを決め、ここはそれに掛ける倍率と湧く方向だけを決める:
//   ・押し寄せ（Surge）：surgeTime 秒、湧き × surgeMul。プレイヤーの周りの一つの方向（± arcHalfDeg）からだけ湧く
//   ・押し寄せの後の一息（Lull）：lullTime 秒、湧き × lullMul
//   ・それ以外（Normal）：湧き × baseMul（波で増える分をならして、平均が表とだいたい同じになるよう少し下げる）
//   最初の押し寄せは firstAt 秒（序盤に敵が体の周りまで来るように早め）、その後は interval ± jitter 秒毎。
//   押し寄せの長さは序盤 surgeTime → 制限時間の近くで surgeTimeLate へ伸びる。
//   制限時間の stopBefore 秒前からは新しく始めない（最終ウェーブの予告と重ねない）
// StageDirector が持って毎フレーム進め、MobSpawner::waveRateMul と SpawnDirector の arc* に書く
// ============================================================
#pragma once
#include <cstdint>
#include <random>

class SpawnWaves
{
public:
    enum class Phase { Normal, Surge, Lull };

    bool  enabled = true;
    float firstAt = 22.0f;        // 最初の押し寄せ（経過秒）
    float interval = 70.0f;       // 押し寄せの始まりから次の始まりまで（秒）
    float jitter = 10.0f;         // ± 秒
    float surgeTime = 10.0f;      // 押し寄せの長さ（序盤）
    float surgeTimeLate = 20.0f;  // 押し寄せの長さ（制限時間の近く）
    float surgeMul = 2.4f;
    float lullTime = 10.0f;
    float lullMul = 0.4f;
    float baseMul = 0.85f;
    float arcHalfDeg = 35.0f;     // 押し寄せが来る方向の幅（± 度）
    float stopBefore = 40.0f;     // 制限時間のこれだけ前からは新しく始めない

    void Reset(uint32_t seed);
    // runTime: 経過秒、stageTime: 制限時間（0 なら制限なし = stopBefore を見ない）
    void Update(float runTime, float stageTime);

    Phase GetPhase() const { return m_Phase; }
    float RateMul() const;
    float ArcYaw() const { return m_ArcYaw; }   // 押し寄せの方向（ラジアン。x = cos、z = sin）
    bool  ArcActive() const { return m_Phase == Phase::Surge; }
    // この Update で押し寄せが始まった（案内・音・自動テスト用）
    bool  JustStarted() const { return m_JustStarted; }
    int   SurgeCount() const { return m_Count; }
    float NextSurgeAt() const { return m_NextAt; }
    float PhaseLeft(float runTime) const { return m_PhaseEnd - runTime; }

    // パネルのボタン：次の Update で押し寄せを始める
    void QueueNow() { m_Debug = true; }

    void DrawImGui(float runTime);

private:
    void StartSurge(float runTime, float stageTime);

    std::mt19937 m_Rng{ 1u };
    Phase m_Phase = Phase::Normal;
    float m_PhaseEnd = 0.0f;
    float m_NextAt = 0.0f;
    float m_ArcYaw = 0.0f;
    int   m_Count = 0;
    bool  m_JustStarted = false;
    bool  m_Debug = false;
};
