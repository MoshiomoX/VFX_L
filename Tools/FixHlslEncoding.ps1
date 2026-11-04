# HLSL / HLSLI の文字コードをそろえる（UTF-8・BOM なし）。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools/FixHlslEncoding.ps1 -Root VFX_L [-Check]
#
# fxc（d3dcompiler_47）は
#   - UTF-8（BOM なし）: 日本語のコメントをそのまま読める。行番号もずれない。
#   - UTF-8（BOM あり）: 1 行目で error X3000: Illegal character in shader file。
#   - Shift-JIS       : 「ソ・表・能・予」などは 2 バイト目が 0x5C（\）なので、
#                       // コメントの行末に来ると次の行がコメントに飲み込まれる（黙ってコードが消える）。
# 日本語版 Windows（ACP 932）の VS は ASCII だけのファイルに日本語を足すと Shift-JIS で保存するので、
# .editorconfig で UTF-8 を指定したうえで、すり抜けた分をここで直す。
#
# 直すもの: BOM を外す / Shift-JIS（CP932）を UTF-8 に変換する。改行コードと中身は触らない。
# UTF-8 としても CP932 としても読めないファイルはエラー（変換しない）。
# -Check: 直さずに報告だけする（直すべきファイルがあれば終了コード 1）。
# -Utf8Output: 出力を UTF-8 にする（MSBuild の Exec から呼ぶとき。手で動かすときは付けない）。
# -Utf8Output: 出力を UTF-8 にする（MSBuild の Exec から呼ぶとき。手で動かすときは付けない）。
# 出力は MSBuild の書式（path(1): warning HLSLENC: ...）なので、VS のエラー一覧に出る。

param(
    [Parameter(Mandatory = $true)][string]$Root,
    [switch]$Check,
    [switch]$Utf8Output
)

$ErrorActionPreference = 'Stop'
if ($Utf8Output) { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) }

$utf8Strict = New-Object System.Text.UTF8Encoding($false, $true)
$utf8NoBom  = New-Object System.Text.UTF8Encoding($false, $false)
$cp932Strict = [System.Text.Encoding]::GetEncoding(932,
    [System.Text.EncoderFallback]::ExceptionFallback,
    [System.Text.DecoderFallback]::ExceptionFallback)

$rootFull = (Resolve-Path -LiteralPath $Root).Path
$files = Get-ChildItem -LiteralPath $rootFull -Recurse -File -Include '*.hlsl', '*.hlsli', '*.fx', '*.fxh' |
    Where-Object { $_.FullName -notmatch '\\(ThirdParty|x64|Win32|packages)\\' }

$fixed = 0
$bad = 0
foreach ($f in $files) {
    $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
    $hadBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $start = 0
    if ($hadBom) { $start = 3 }

    $text = $null
    $fromSjis = $false
    try {
        $text = $utf8Strict.GetString($bytes, $start, $bytes.Length - $start)
    } catch {
        try {
            $text = $cp932Strict.GetString($bytes, $start, $bytes.Length - $start)
            # CP932 の復号はほとんどのバイト列を通してしまうので、制御文字・私用領域・半角カナが出たら
            # Shift-JIS ではないとみなす（GBK の中国語は 0xB0〜0xDF が半角カナに化ける。間違って変換してコメントを壊さないため）。
            if ($text -match '[\u0080-\u009F-｡-ﾟ�]') { throw 'not cp932' }
            $fromSjis = $true
        } catch {
            Write-Output ("{0}(1): error HLSLENC: UTF-8 としても Shift-JIS としても読めない。UTF-8（BOM なし）で保存し直してください。" -f $f.FullName)
            $bad++
            continue
        }
    }

    if (-not $hadBom -and -not $fromSjis) { continue }

    $what = @()
    if ($Check) {
        if ($hadBom)   { $what += 'BOM が付いている' }
        if ($fromSjis) { $what += 'Shift-JIS で保存されている' }
    } else {
        if ($hadBom)   { $what += 'BOM を外した' }
        if ($fromSjis) { $what += 'Shift-JIS を UTF-8 に変換した' }
    }
    $msg = ($what -join '・') + '（HLSL は UTF-8・BOM なしで保存する）'

    if (-not $Check) {
        [System.IO.File]::WriteAllBytes($f.FullName, $utf8NoBom.GetBytes($text))
    }
    Write-Output ("{0}(1): warning HLSLENC: {1}" -f $f.FullName, $msg)
    $fixed++
}

if ($bad -gt 0) { exit 2 }
if ($Check -and $fixed -gt 0) { exit 1 }
exit 0
