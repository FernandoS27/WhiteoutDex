# 安装

一个安装程序就能把 WhiteoutDex 装进你电脑上的每个 3ds Max 版本（2016 到 2027）。

## 安装

1. 关闭 3ds Max。
2. 下载最新的 `WhiteoutDex_Setup_v<version>.exe`。你可以在 GitHub 的 **Releases** 页面和 Hive Workshop 上找到它。
3. 启动安装程序。Windows 会要求管理员权限。点击 **是**。
4. 选择你的语言，然后按步骤操作。安装程序会自动找到你所有的 3ds Max 版本。
5. 启动 3ds Max。现在你会看到 **WhiteoutDex** 菜单和 WhiteoutDex 侧边栏。

!!! note
    3ds Max 2026 及更新版本需要 **.NET 8**。如果缺少它，安装程序会告诉你。

## 文件安装在哪里

WhiteoutDex 安装在一个文件夹中：

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

3ds Max 从这里加载所有内容。你不需要自己复制任何文件。

你的个人设置（语言、游戏文件夹、上次的导出选项）保存在 3ds Max 的设置文件夹 `plugcfg` 中。更新 WhiteoutDex 时，这些设置会保留。

## 更新

3ds Max 启动时，WhiteoutDex 可以检查新版本（3ds Max 2018 及更新版本）。有新版本时，它会询问你：

- 点击 **是** 下载并安装新版本。3ds Max 会关闭以完成更新。
- 点击 **否** 继续工作。第二天它会再次询问。

你可以在 [[t:menu_settings]] 中关闭此功能，或立即检查更新。

## 卸载

打开 **Windows 设置 > 应用**，找到 **WhiteoutDex**，然后点击 **卸载**。
