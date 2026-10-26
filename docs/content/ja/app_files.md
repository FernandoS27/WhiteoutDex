# ファイルと設定

このページでは、WhiteoutDex がファイルを置く場所を説明します。通常、これらのファイルを開く必要はありません。問題が起きたときや、新しいパソコンに移るときに役立ちます。

## インストールフォルダ

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

| フォルダまたはファイル | 内容 |
|---|---|
| `native plugins\Max<year>\` | 3ds Max の各バージョン用のプラグイン（インポーター、エクスポーター、パーティクル、リボン、プレビューア、BLP リーダー）。 |
| `scripts\` | WhiteoutDex のツール（MAXScript）。 |
| `lang\` | プレビューアの翻訳。 |
| `whiteoutdex_icons\` | サイドバーとツールのアイコン。 |
| `maps\WhiteoutDex\TeamGlow\` | チームグローのテクスチャ。 |
| `version.txt` | インストールされているバージョン。 |

ここにあるファイルは変更しないでください。更新すると置き換えられます。

## 個人の設定

個人の設定は、3ds Max の設定フォルダ **plugcfg** にあります。場所を調べるには、MAXScript Listener を開き（[[key:F11]]）、`getDir #plugcfg` と入力します。

| ファイル | 保存する内容 |
|---|---|
| `WhiteoutDex_Settings.ini` | 言語、Reforged のフォルダ、MPQ の順番、サイドバー、更新の確認。 |
| `WhiteoutDexMPQ.ini` | クラシック版のゲームのフォルダ。 |
| `WhiteoutDexImporter.ini` | 前回のインポートオプション。 |
| `MDLXExporter.ini` | 前回のエクスポートオプション。 |
| `WhiteoutDex_MaterialFix.ini` | SD と HD の *マテリアル修正* の設定。 |

!!! tip
    WhiteoutDex を最初に起動したときの状態に戻すには、3ds Max を閉じて、これらのファイルを削除します。WhiteoutDex が初期値で新しいファイルを作ります。

## プレビューア

| ファイル | 保存する内容 |
|---|---|
| `WhiteoutFlakes.ini`（`3dsmax.exe` の横） | WhiteoutFlakes プレビューアの設定。 |

## ログファイル

問題が起きたとき、これらのファイルに何が起きたかが書かれています。ファイルは一時フォルダにあります。一時フォルダを開くには、[[key:Win+R]] を押し、`%TEMP%` と入力して [[key:Enter]] を押します。

| ファイル | 書き込むもの |
|---|---|
| `mdlx_import_debug.log` | インポーター |
| `mdlx_export_debug.log` | エクスポーター（警告の完全な一覧を含みます） |
| `WhiteoutDex_render.log` | WhiteoutFlakes プレビューア |

問題を報告するときは、これらのファイルを送ってください。
