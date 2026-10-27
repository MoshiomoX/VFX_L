// ============================================================
// BossIntro.h
// 門から呼んだ Boss の登場（2026-10-27、ユーザー「突然湧いて門から出てこないのが違和感が強い」→
// 「カメラを動かさない 3 秒の短い演出」・まずは登場だけ）。ゲームは止めない。
//   0.0 秒  門を使う：門の渦が赤く荒れる（地鳴り・小さな揺れ）
//   1.0 秒  Boss が門の口のすぐ前（StageDirector::bossIntroDelay / bossIntroFront）に煙と一緒に湧く（咆哮と曲は BattleAudio）
//   湧いてから walkTime 秒、walkDir へ walkSpeed で歩く（技は出さない・体で殴らない）
//   歩き終わり：その場で踏み込み（土煙・石・草が倒れる・揺れ）+ 画面の真ん中に Boss の名前
// 動かすのは GPU の Boss の突進の指示（BomberCB::bossCharge*）。シーンが Frame を見て BossAttacks の代わりに書く
// ============================================================
#pragma once
#include <SimpleMath.h>

class BossIntro
{
public:
    using Vector3 = DirectX::SimpleMath::Vector3;

    // 毎フレームの結果（シーンが見る）
    struct Frame
    {
        bool  active = false;     // 演出の間（BossAttacks を止め、Boss は殴らない）
        bool  steer = false;      // Boss の歩きを指示する（湧いてから歩き終わりまで）
        DirectX::SimpleMath::Vector2 dir;   // 歩く向き（xz の単位）
        float speed = 0.0f;       // 0 = その場で止まる
        bool  rumbleStart = false;   // 門が荒れ始めた（赤い渦を出す）
        bool  emerge = false;        // Boss が湧いた（湧く所に煙。1 回だけ）
        bool  stomp = false;         // 歩き終わりの踏み込み（1 回だけ）
        bool  finished = false;      // 演出が終わった（赤い渦を止める。1 回だけ）
        float shake = 0.0f;          // この瞬間の揺れの強さ（RaiseTrauma に渡す。0 = 無し）
    };

    // 門を使った瞬間（StageDirector::ConsumeBossIntro）
    void Start(const Vector3& gate, const Vector3& walkDir, const Vector3& spawn);
    void Cancel() { m_Phase = Phase::Idle; }
    // bossAlive = Boss が GPU に居る（リードバック）
    void Update(float dt, bool bossAlive, Frame& out);
    bool Active() const { return m_Phase != Phase::Idle; }
    const Vector3& Gate() const { return m_Gate; }
    const Vector3& Spawn() const { return m_Spawn; }

    // ---- 調整 ----
    float walkTime = 1.4f;        // 湧いてから歩く秒
    float walkSpeed = 3.0f;       // m/秒（口の前 2.5m → 6.5m あまり）
    float holdTime = 0.6f;        // 踏み込みの後、技を出し始めるまで
    float maxWaitSpawn = 3.0f;    // 湧かない時に諦める秒（湧く枠が無い等）

private:
    enum class Phase { Idle, Rumble, Walk, Hold };
    Phase m_Phase = Phase::Idle;
    float m_Time = 0.0f;
    bool  m_StartedFrame = false;
    Vector3 m_Gate, m_Dir, m_Spawn;
};
