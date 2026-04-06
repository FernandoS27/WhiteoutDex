/*
 * WhiteoutDex Auto-Updater
 * -------------------
 * Checks on 3ds Max startup if a new version of WhiteoutDex is available on GitHub.
 * All UI strings use ::L.t for localization.
 */

(
    local _githubUser   = "DennisHerrm"
    local _githubRepo   = "WhiteoutDex"
    local _checkInterval = 86400

    fn _ndxGetInstallPath = (
        local p = undefined
        try (
            local subKey = (dotNetClass "Microsoft.Win32.Registry").CurrentUser.OpenSubKey "Software\\WhiteoutDex"
            if subKey != undefined then ( p = subKey.GetValue "InstallPath"; subKey.Close() )
        ) catch ()
        if p == undefined then
            p = (dotNetClass "System.Environment").GetFolderPath (dotNetClass "System.Environment+SpecialFolder").ApplicationData + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex"
        p
    )

    fn _ndxGetLocalVersion = (
        local vf = (_ndxGetInstallPath()) + "\\version.txt"
        if not (doesFileExist vf) then return "0.0.0"
        local f = openFile vf mode:"r"
        if f == undefined then return "0.0.0"
        local ver = trimRight (trimLeft (readLine f))
        close f
        ver
    )

    fn _ndxCompareVersions lv rv = (
        local lP = filterString lv "."
        local rP = filterString rv "."
        for i = 1 to (amax lP.count rP.count) do (
            local a = if i <= lP.count then (try(lP[i] as integer)catch 0) else 0
            local b = if i <= rP.count then (try(rP[i] as integer)catch 0) else 0
            if b > a then return 1
            if b < a then return -1
        )
        0
    )

    fn _ndxShouldCheck = (
        -- If force flag is set (from Settings "Check Now"), always check
        try (
            if ::_ndxForceUpdateCheck == true then (
                ::_ndxForceUpdateCheck = false
                return true
            )
        ) catch ()
        -- Otherwise check the auto-check setting
        local iniFile = (_ndxGetInstallPath()) + "\\WhiteoutDex_Settings.ini"
        if doesFileExist iniFile then (
            local autoCheck = getINISetting iniFile "Updater" "AutoCheck"
            if autoCheck == "0" then return false
        )
        true
    )

    fn _ndxSetLastCheckTime = (
        -- nicht mehr noetig
        OK
    )

    fn _ndxCheckGitHub = (
        local result = #(undefined, undefined)
        try (
            local url = "https://api.github.com/repos/" + _githubUser + "/" + _githubRepo + "/releases/latest"
            local wc = dotNetObject "System.Net.WebClient"
            wc.Headers.Add "User-Agent" "WhiteoutDex-Updater"
            wc.Headers.Add "Accept" "application/vnd.github.v3+json"
            local resp = wc.DownloadString url
            wc.Dispose()
            local rx = dotNetClass "System.Text.RegularExpressions.Regex"
            local tm = rx.Match resp "\"tag_name\"\\s*:\\s*\"([^\"]+)\""
            if tm.Success then (
                local v = tm.Groups.Item[1].Value
                if v[1] == "v" or v[1] == "V" then v = substring v 2 (v.count - 1)
                result[1] = v
            )
            local am = rx.Match resp "\"browser_download_url\"\\s*:\\s*\"([^\"]+\\.exe)\""
            if am.Success then result[2] = am.Groups.Item[1].Value
        ) catch (
            format "[WhiteoutDex Updater] GitHub query failed: %\n" (getCurrentException())
        )
        result
    )

    fn _ndxDownloadAndInstall dlUrl ver = (
        local tmpDir = (dotNetClass "System.IO.Path").GetTempPath()
        local dlFile = tmpDir + "WhiteoutDex_Setup_v" + ver + ".exe"
        local batFile = tmpDir + "WhiteoutDex_AutoInstall.bat"
        try (
            progressStart (::L.t "upd_downloading")
            local wc = dotNetObject "System.Net.WebClient"
            wc.Headers.Add "User-Agent" "WhiteoutDex-Updater"
            wc.DownloadFile dlUrl dlFile
            wc.Dispose()
            progressEnd()
            if doesFileExist dlFile then (
                -- User fragen ob Max jetzt geschlossen werden soll
                if queryBox ((::L.t "upd_close_max_msg") + "\n\n" + (::L.t "upd_save_work_msg")) title:(::L.t "upd_title") then (
                    -- Batch-Script: wartet bis Max zu ist, installiert silent
                    local bat = "@echo off\n"
                    bat += ":WAIT\n"
                    bat += "tasklist /FI \"IMAGENAME eq 3dsmax.exe\" 2>NUL | find /I \"3dsmax.exe\" >NUL\n"
                    bat += "if %ERRORLEVEL%==0 (\n"
                    bat += "    timeout /t 2 /nobreak >NUL\n"
                    bat += "    goto WAIT\n"
                    bat += ")\n"
                    bat += "timeout /t 1 /nobreak >NUL\n"
                    bat += "start \"\" \"" + dlFile + "\" /NORESTART /NOCLOSEAPPLICATIONS\n"
                    bat += "del \"%~f0\"\n"

                    local f = createFile batFile
                    format "%" bat to:f
                    close f

                    -- Batch-Script versteckt starten
                    local psi = dotNetObject "System.Diagnostics.ProcessStartInfo"
                    psi.FileName = batFile
                    psi.WindowStyle = (dotNetClass "System.Diagnostics.ProcessWindowStyle").Hidden
                    psi.CreateNoWindow = true
                    (dotNetClass "System.Diagnostics.Process").Start psi

                    -- Max ordentlich schliessen (User bekommt Speichern-Dialog falls noetig)
                    quitMax #prompt
                )
            ) else (
                messageBox (::L.t "upd_download_failed") title:(::L.t "upd_title") beep:false
            )
        ) catch (
            progressEnd()
            format "[WhiteoutDex Updater] Download failed: %\n" (getCurrentException())
        )
    )

    -- ========================================
    -- RUN CHECK
    -- ========================================

    if not (_ndxShouldCheck()) then (
        format "[WhiteoutDex Updater] Next check not due yet.\n"
    ) else (
        format "[WhiteoutDex Updater] Checking for updates...\n"

        local localVer = _ndxGetLocalVersion()
        format "[WhiteoutDex Updater] Local version: %\n" localVer

        local info = _ndxCheckGitHub()
        local remoteVer = info[1]
        local dlUrl     = info[2]

        _ndxSetLastCheckTime()

        if remoteVer == undefined then (
            format "[WhiteoutDex Updater] Could not retrieve remote version.\n"
        ) else (
            format "[WhiteoutDex Updater] Remote version: %\n" remoteVer

            if (_ndxCompareVersions localVer remoteVer) > 0 then (
                format "[WhiteoutDex Updater] New version available: % -> %\n" localVer remoteVer

                local msg = (::L.t "upd_new_version_msg") + "\n\n"
                msg += (::L.t "upd_installed") + "  v" + localVer + "\n"
                msg += (::L.t "upd_available") + "   v" + remoteVer + "\n\n"
                msg += (::L.t "upd_changelog_at") + "\ngithub.com/" + _githubUser + "/" + _githubRepo + "/releases\n\n"
                msg += (::L.t "upd_install_now")

                if dlUrl != undefined then (
                    if queryBox msg title:(::L.t "upd_title_available") then
                        _ndxDownloadAndInstall dlUrl remoteVer
                ) else (
                    if queryBox msg title:(::L.t "upd_title_available") then
                        ShellLaunch ("https://github.com/" + _githubUser + "/" + _githubRepo + "/releases/latest") ""
                )
            ) else (
                format "[WhiteoutDex Updater] Version is up to date.\n"
            )
        )
    )
)
