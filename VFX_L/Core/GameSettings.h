// ============================================================
// GameSettings.h
// プレイヤーが一時停止メニューの「設定」で変える値（2026-10-07 ユーザー：音量とアウトラインの ON / OFF。10-08 から敵味方の縁取りだけ。10-19 ダメージの数字も）。
//   音量は AudioSystem が自分の Settings.json に持っているので、ここには置かない（設定画面が AudioSystem を直接触る）。
//   ここにあるのは画面側の設定だけ。Assets/Data/Settings.json に保存し、起動時に読む。
//   ImGui のデバッグ用の開閉（Outline::Params::enabled など）とは別物：
//   描く側は「デバッグの開閉 && この設定」で描く（デバッグ側を上書きしない）
// ============================================================
#pragma once

class GameSettings
{
public:
    static GameSettings& Get();

    // 敵・味方の色付きの縁取り（プレイヤーの青 / 敵の赤）。オフでもトゥーンの黒い線は残る
    // （2026-10-08 ユーザー：切った時に消したいのは敵味方の枠だけ。10-07 版はアウトライン全体を消していた）
    bool factionOutline = true;

    // ダメージの数字（UI/DamageNumbers：敵の頭の上の数字とプレイヤーが受けた赤い数字）を出す（2026-10-19 ユーザー「設定にダメージ数字のオン / オフ」）。
    // ImGui の「Damage Numbers」のチェック（デバッグ）とは別：描く側は「デバッグの開閉 && この設定」
    bool damageNumbers = true;

    // 初回のチュートリアル（UI/TutorialGuide）を終えた / 飛ばした（2026-10-13）。false なら第 1 ステージの始めに出る
    bool tutorialDone = false;

    bool Save() const;
    bool Load();
};
