// ============================================================
// DevUI.h
// 開発用の UI（ImGui・エディタ・F キーのシーン切替・自動テスト）の有無（2026-10-07、Demo ビルド）。
//   Debug / Release : 今まで通り全部ある
//   Demo            : VFXL_DEMO が定義される（構成「Demo|x64」）。ImGui を作らない・エディタのシーンを登録しない・
//                     F キーで切り替えない・コンソールを出さない・自動テストの環境変数を見ない
// ゲームの処理から「ImGui がマウス / キーを取っているか」を見る時は、ImGui を直接触らずここを通す
// （Demo では ImGui の context が無いので、ImGui::GetIO() を呼ぶと落ちる）
// ============================================================
#pragma once
#include "imgui.h"
#include <cstring>

namespace DevUI
{
#ifdef VFXL_DEMO
    constexpr bool kEnabled = false;
#else
    constexpr bool kEnabled = true;
#endif

    // その名前の戦闘の自動テスト（VFXL_BATTLE_AUTOTEST）を回してよいか。Demo でも負荷試験の 2 つだけは回す
    // （2026-10-23、ユーザー：「Debug と Demo で負荷試験を」。環境変数を立てた時だけで、普通に遊ぶ時は何も変わらない）
    inline bool AutoTestAllowed(const char* name)
    {
        return kEnabled || std::strcmp(name, "stress") == 0 || std::strcmp(name, "stressfx") == 0
            || std::strcmp(name, "gustall") == 0;   // TEMP-TEST（2026-10-27 突風の全体テストを Demo でも）
    }

    // ImGui が動いているか（Demo では常に false）
    inline bool Active() { return ImGui::GetCurrentContext() != nullptr; }
    inline bool WantMouse() { return Active() && ImGui::GetIO().WantCaptureMouse; }
    inline bool WantKeyboard() { return Active() && ImGui::GetIO().WantCaptureKeyboard; }
    inline bool WantTextInput() { return Active() && ImGui::GetIO().WantTextInput; }
}
