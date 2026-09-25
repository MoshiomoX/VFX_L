// ============================================================
// FollowCamera.h
// TPS 追従カメラ：対象の後上方（右肩寄り）から追いかけ、右スティック／マウスで旋回する。
//
//   平滑追従   … 追従点をばね（SmoothDamp）で追わせる。水平と垂直で速さを分ける
//                （跳躍で画面が上下に跳ねないよう垂直は遅め）。離れすぎたら補間せず飛ぶ
//   肩ずらし   … 注視点ごと右へずらす。向き（GetForward）は変わらないので操作の基準はそのまま
//   遮蔽回避   … 注視点 → カメラの間を射線で調べ、壁・床に当たればその手前まで寄る。
//                寄る時は即座（めり込みを見せない）、戻る時はゆっくり（ぱたぱたさせない）。
//                当たり判定はシーンが SetOcclusionProbe で渡す（カメラは衝突系を知らない）
//   画面の揺れ … AddTrauma で trauma（0..1）を足すと揺れ、時間で減る。揺れ幅は trauma の二乗。
//                ビュー行列にだけ掛けるので、移動方向・向きの基準は揺れない
// 値はすべて外部（ImGui）から調整できる。
// ============================================================
#pragma once
#include "Camera/CameraBase.h"
#include <functional>

class FollowCamera : public CameraBase
{
public:
    void Update(float dt) override;

    // 追従対象のワールド座標（毎フレーム、物理更新後に渡す）
    void SetFollowTarget(const Vector3& pos) { m_FollowTarget = pos; }
    // 次の Update で補間せずに追いつく（開始時・ワープ時）
    void SnapToTarget() { m_Snap = true; }

    // 遮蔽の問い合わせ：from から dir（正規化済み）へ maxDist まで調べ、
    // 当たったら距離を outDist に入れて true。起点が物の内側なら当たらない扱いでよい
    using OcclusionProbe = std::function<bool(const Vector3& from, const Vector3& dir,
        float maxDist, float& outDist)>;
    void SetOcclusionProbe(OcclusionProbe probe) { m_Probe = std::move(probe); }

    // 揺れを足す（合計は 0..1 に丸める）
    void  AddTrauma(float amount);
    float GetTrauma() const { return m_Trauma; }

    float GetYaw()   const { return m_Yaw; }
    float GetPitch() const { return m_Pitch; }
    float GetCurrentDistance() const { return m_CurDistance; }   // 遮蔽で縮んだ後
    bool  IsOccluded() const { return m_Occluded; }

    // ---- 調整パラメータ（ImGui から触る）----
    float distance = 6.0f;    // 対象からの距離（中距離）
    float height = 1.5f;    // 注視点の高さオフセット（足元でなく胸あたりを見る）
    float shoulderOffset = 0.5f;    // 右肩へのずらし（m）。負で左肩、0 で真後ろ
    float stickSensitivity = 150.0f;  // 度/秒
    float mouseSensitivity = 0.15f;   // 度/ピクセル
    float pitchMin = -30.0f;  // 見上げ限界
    float pitchMax = 70.0f;  // 見下ろし限界
    bool  invertY = false;

    // 平滑追従
    bool  smoothFollow = true;
    float followSmoothTime = 0.12f;   // 水平（秒。小さいほど早く追いつく）
    float verticalSmoothTime = 0.25f;   // 垂直（跳躍・段差）
    float snapDistance = 8.0f;    // これ以上離れたら補間せずに飛ぶ（ワープ・再生成）

    // 遮蔽回避
    bool  avoidOcclusion = true;
    float probeRadius = 0.3f;    // カメラの太さ（中心 + 上下左右の 5 本の射線で調べる）
    float minDistance = 0.8f;    // これ以上は寄らない
    float returnSmoothTime = 0.35f;   // 遮蔽が外れて元の距離へ戻る速さ（秒）

    // 画面の揺れ
    bool  shakeEnabled = true;
    float shakeMaxYaw = 2.5f;    // 度（trauma = 1 の時）
    float shakeMaxPitch = 2.5f;
    float shakeMaxRoll = 4.0f;
    float shakeFrequency = 14.0f;   // 揺れの速さ
    float traumaDecay = 1.4f;    // 毎秒減る量

private:
    // from から dir へ、中心 + 上下左右（probeRadius ずらし）の 5 本で一番近い当たり。無ければ maxDist
    float ProbeDistance(const Vector3& from, const Vector3& dir, float maxDist,
        const Vector3& right, const Vector3& up) const;

    Vector3 m_FollowTarget = { 0, 0, 0 };
    Vector3 m_Pivot = { 0, 0, 0 };   // 平滑後の追従点
    Vector3 m_PivotVel = { 0, 0, 0 };
    bool    m_Snap = true;
    float   m_Yaw = 0.0f;    // 水平角（度）
    float   m_Pitch = 20.0f;   // 仰角（度、正=見下ろす）

    float   m_CurDistance = 6.0f;   // 遮蔽回避を反映した今の距離
    float   m_DistVel = 0.0f;
    bool    m_Occluded = false;

    float   m_Trauma = 0.0f;
    float   m_ShakeTime = 0.0f;

    OcclusionProbe m_Probe;
};
