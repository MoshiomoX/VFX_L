// ============================================================
// pch.h
// プリコンパイル済みヘッダ（2026-10-07、ユーザー：「include を整理、重複は消す、よく使う物はまとめる」）。
// プロジェクトの設定で全部の .cpp に強制インクルード（/FI pch.h、/Yu）されるので、.cpp には書かない。
//   ・ここに入れるのは多くの .cpp が使う標準ライブラリだけ（数えて 2 ファイル以上の物）。
//     プロジェクトのヘッダは入れない（1 つ変えると全部が作り直しになる・どの部品に頼っているかが見えなくなる）
//   ・Windows.h も入れない（min / max のマクロが全部に広がる）
//   ・ヘッダ（.h）は自分だけで読めるように、使う標準ライブラリを今まで通り自分で include する
//   ・ThirdParty の .cpp / .c はこれを使わない（VFX_L.vcxproj で NotUsing）
// ============================================================
#pragma once

#include <algorithm>
#include <chrono>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
