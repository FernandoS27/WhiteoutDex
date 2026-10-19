# Installation

One installer puts WhiteoutDex into every 3ds Max version on your computer (2016 to 2027).

## Install

1. Close 3ds Max.
2. Download the newest `WhiteoutDex_Setup_v<version>.exe`. You find it on the GitHub **Releases** page and on Hive Workshop.
3. Start the installer. Windows asks for administrator rights. Click **Yes**.
4. Choose your language and follow the steps. The installer finds all your 3ds Max versions by itself.
5. Start 3ds Max. You now see the **WhiteoutDex** menu and the WhiteoutDex sidebar.

!!! note
    3ds Max 2026 and newer need **.NET 8**. The installer tells you if it is missing.

## Where the files go

WhiteoutDex installs into one folder:

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

3ds Max loads everything from there. You do not need to copy any files yourself.

Your personal settings (language, game folders, last export options) are in the 3ds Max settings folder `plugcfg`. They stay when you update WhiteoutDex.

## Update

WhiteoutDex can look for new versions when 3ds Max starts (3ds Max 2018 and newer). When a new version is out, it asks you:

- Click **Yes** to download and install it. 3ds Max closes for the update.
- Click **No** to keep working. It asks again the next day.

You can switch this off or check now in [[t:menu_settings]].

## Uninstall

Open **Windows Settings > Apps**, find **WhiteoutDex** and click **Uninstall**.
