// ============================================================
// ShaderPath.h
// shader の読み込みパスと読み込み方法を1ヶ所に集める。
//
//   以前は GPUParticleSystem と ResourceManager が
//   同じ変換関数を各自持ち、Renderer は直接パスを書いていた。
//   読み込み方針を変えるたびに3ヶ所を直す必要があり、
//   直し漏れが「Release だけ shader が見つからない」形で現れた。
// ============================================================
#pragma once
#include <string>
#include <d3d11.h>

namespace ShaderPath
{
    // ============================================================
    // hlsl のパスから cso のパスを作る
    // 例）L"Shader/Particle/ParticleEmitCS.hlsl"
    //        -> "Shader/Particle/ParticleEmitCS.cso"
    // ============================================================
    inline std::string ToCso(const std::wstring& hlslPath)
    {
        std::string s(hlslPath.begin(), hlslPath.end());
        const size_t dot = s.find_last_of('.');
        if (dot != std::string::npos)
            s = s.substr(0, dot);
        return s + ".cso";
    }

    inline std::string ToCso(const std::string& hlslPath)
    {
        std::string s = hlslPath;
        const size_t dot = s.find_last_of('.');
        if (dot != std::string::npos)
            s = s.substr(0, dot);
        return s + ".cso";
    }

    // ============================================================
    // VS/PS/CS 共通のロード
    //
    //   Debug  Release  cso を読む。
    //   構成によって読み込み経路が変わると、
    //   片方でしか出ない問題が必ず生まれる。実際にそうなった。
    //
    //   実行時コンパイルの利点（hlsl を書き換えて再起動すれば反映）は
    //   失うが、パラメータ調整は VFX エディタ側で完結するため影響は小さい。
    // ============================================================
    template <class T>
    HRESULT Load(T* shader, ID3D11Device* device, const std::wstring& hlslPath)
    {
        return shader->Load(device, ToCso(hlslPath).c_str());
    }
}