# 文件与设置

本页列出 WhiteoutDex 把文件保存在哪里。通常你不需要打开它们。出现问题或换新电脑时，它们会有帮助。

## 安装文件夹

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

| 文件夹或文件 | 里面有什么 |
|---|---|
| `native plugins\Max<year>\` | 每个 3ds Max 版本的插件（导入器、导出器、粒子、飘带、预览器、BLP 读取器）。 |
| `scripts\` | WhiteoutDex 工具（MAXScript）。 |
| `lang\` | 预览器的翻译。 |
| `whiteoutdex_icons\` | 侧边栏和工具的图标。 |
| `maps\WhiteoutDex\TeamGlow\` | 队伍光晕纹理。 |
| `version.txt` | 已安装的版本。 |

不要修改这里的文件。更新会替换它们。

## 你的设置

你的设置保存在 3ds Max 的设置文件夹 **plugcfg** 中。要找到它，请打开 MAXScript Listener（[[key:F11]]），然后输入 `getDir #plugcfg`。

| 文件 | 保存什么 |
|---|---|
| `WhiteoutDex_Settings.ini` | 语言、重制版文件夹、MPQ 顺序、侧边栏、更新检查。 |
| `WhiteoutDexMPQ.ini` | 经典版游戏的文件夹。 |
| `WhiteoutDexImporter.ini` | 你上次的导入选项。 |
| `MDLXExporter.ini` | 你上次的导出选项。 |
| `WhiteoutDex_MaterialFix.ini` | SD 和 HD 的 *材质修复* 设置。 |

!!! tip
    要把 WhiteoutDex 恢复到初次启动的状态，请关闭 3ds Max 并删除这些文件。WhiteoutDex 会用默认值创建新文件。

## 预览器

| 文件 | 保存什么 |
|---|---|
| `WhiteoutFlakes.ini`（位于 `3dsmax.exe` 旁边） | WhiteoutFlakes 预览器的设置。 |

## 日志文件

出现问题时，这些文件会记录发生了什么。它们位于你的临时文件夹中。要打开它，请按 [[key:Win+R]]，输入 `%TEMP%`，然后按 [[key:Enter]]。

| 文件 | 由谁写入 |
|---|---|
| `mdlx_import_debug.log` | 导入器 |
| `mdlx_export_debug.log` | 导出器，包含完整的警告列表 |
| `WhiteoutDex_render.log` | WhiteoutFlakes 预览器 |

报告问题时，请发送这些文件。
