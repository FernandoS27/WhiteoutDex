/*
 * WhiteoutDex Auto-Updater
 * -------------------
 * Checks on 3ds Max startup if a new version of WhiteoutDex is available on GitHub.
 * All UI strings use ::WdxL.t for localization.
 *
 * Compatibility: 3ds Max 2016 - 2027
 *   Max 2016-2025: System.Net.WebClient
 *   Max 2026+:     System.Net.Http.HttpClient (.NET 8)
 */

(
    local _githubUser   = "DennisHerrm"
    local _githubRepo   = "WhiteoutDex"
    local _checkInterval = 86400

    -- True if running on Max 2026+ (.NET 8 where WebClient is obsolete)
    local _useHttpClient = ((maxVersion())[1] >= 28000)

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
        OK
    )

    -- ========================================
    -- HTTP Download helpers (dual WebClient / HttpClient)
    -- ========================================

    -- Download a string from a URL
    fn _ndxDownloadString url = (
        if _useHttpClient then
        (
            -- .NET 8: use HttpClient
            local httpClient = dotNetObject "System.Net.Http.HttpClient"
            try (
                httpClient.DefaultRequestHeaders.Add "User-Agent" "WhiteoutDex-Updater"
                httpClient.DefaultRequestHeaders.Add "Accept" "application/vnd.github.v3+json"
                local task = httpClient.GetStringAsync url
                task.Wait()
                local result = task.Result
                httpClient.Dispose()
                result
            ) catch (
                try (httpClient.Dispose()) catch ()
                throw
            )
        )
        else
        (
            -- .NET Framework: use WebClient
            local wc = dotNetObject "System.Net.WebClient"
            wc.Headers.Add "User-Agent" "WhiteoutDex-Updater"
            wc.Headers.Add "Accept" "application/vnd.github.v3+json"
            local result = wc.DownloadString url
            wc.Dispose()
            result
        )
    )

    -- Download a file from a URL
    fn _ndxDownloadFile url destPath = (
        if _useHttpClient then
        (
            -- .NET 8: use HttpClient
            local httpClient = dotNetObject "System.Net.Http.HttpClient"
            try (
                httpClient.DefaultRequestHeaders.Add "User-Agent" "WhiteoutDex-Updater"
                local task = httpClient.GetByteArrayAsync url
                task.Wait()
                local bytes = task.Result
                (dotNetClass "System.IO.File").WriteAllBytes destPath bytes
                httpClient.Dispose()
            ) catch (
                try (httpClient.Dispose()) catch ()
                throw
            )
        )
        else
        (
            -- .NET Framework: use WebClient
            local wc = dotNetObject "System.Net.WebClient"
            wc.Headers.Add "User-Agent" "WhiteoutDex-Updater"
            wc.DownloadFile url destPath
            wc.Dispose()
        )
    )

    -- ========================================
    -- GitHub API query
    -- ========================================

    fn _ndxCheckGitHub = (
        local result = #(undefined, undefined)
        try (
            local url = "https://api.github.com/repos/" + _githubUser + "/" + _githubRepo + "/releases/latest"
            local resp = _ndxDownloadString url
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
            progressStart (::WdxL.t "upd_downloading")
            _ndxDownloadFile dlUrl dlFile
            progressEnd()
            if doesFileExist dlFile then (
                -- Ask user if Max should be closed
                if queryBox ((::WdxL.t "upd_close_max_msg") + "\n\n" + (::WdxL.t "upd_save_work_msg")) title:(::WdxL.t "upd_title") then (
                    -- Batch script: waits until Max is closed, then installs silently
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

                    -- Start batch script hidden
                    local psi = dotNetObject "System.Diagnostics.ProcessStartInfo"
                    psi.FileName = batFile
                    psi.WindowStyle = (dotNetClass "System.Diagnostics.ProcessWindowStyle").Hidden
                    psi.CreateNoWindow = true
                    (dotNetClass "System.Diagnostics.Process").Start psi

                    -- Close Max gracefully (user gets save dialog if needed)
                    quitMax #prompt
                )
            ) else (
                messageBox (::WdxL.t "upd_download_failed") title:(::WdxL.t "upd_title") beep:false
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

                local msg = (::WdxL.t "upd_new_version_msg") + "\n\n"
                msg += (::WdxL.t "upd_installed") + "  v" + localVer + "\n"
                msg += (::WdxL.t "upd_available") + "   v" + remoteVer + "\n\n"
                msg += (::WdxL.t "upd_changelog_at") + "\ngithub.com/" + _githubUser + "/" + _githubRepo + "/releases\n\n"
                msg += (::WdxL.t "upd_install_now")

                if dlUrl != undefined then (
                    if queryBox msg title:(::WdxL.t "upd_title_available") then
                        _ndxDownloadAndInstall dlUrl remoteVer
                ) else (
                    if queryBox msg title:(::WdxL.t "upd_title_available") then
                        ShellLaunch ("https://github.com/" + _githubUser + "/" + _githubRepo + "/releases/latest") ""
                )
            ) else (
                format "[WhiteoutDex Updater] Version is up to date.\n"
            )
        )
    )
)
