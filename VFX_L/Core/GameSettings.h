// ============================================================
// GameSettings.h
// プレイヤーが一時停止メニューの「設定」で変える値（2026-10-07 ユーザー：音量とアウトラインの ON / OFF）。
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

    bool outline = true;   // トゥーンのアウトライン（陣営の線も一緒に消える）

    bool Save() const;
    bool Load();
};
