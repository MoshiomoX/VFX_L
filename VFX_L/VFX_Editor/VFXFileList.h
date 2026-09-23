#pragma once
#include <string>
#include <vector>
#include <initializer_list>

// ============================================================
// フォルダを列挙して ImGui の Combo にする（ネイティブの dialog は使わない）
// 起動後の追加ファイルは Refresh で拾う
// ============================================================
namespace VFXFileList
{
    // dir 以下（再帰）で exts に合う相対パスを返す。結果は cache する
    const std::vector<std::string>& List(const std::string& dir,
        std::initializer_list<const char*> exts);
    void Refresh();

    // 変更があれば true。selected は "Assets/..." のフルパス
    bool Combo(const char* label, const std::string& dir,
        std::initializer_list<const char*> exts, std::string& selected);
}