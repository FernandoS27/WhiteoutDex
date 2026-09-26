/*
 * WhiteoutDex Auto-Updater
 * -------------------
 * Checks GitHub for a newer WhiteoutDex release on 3ds Max startup (at most
 * once a day), and on demand through ::WdxUpdater.check manual:true, which is
 * what Settings > Check Now calls. All UI strings use ::WdxL.t for
 * localization.
 *
 * Compatibility: 3ds Max 2018 - 2027. The 2016/2017 loader never runs this
 * file and Settings hides the Auto-Update group there; check() refuses those
 * versions too, in case something fileIns it anyway.
 *   Max 2018-2025: System.Net.HttpWebRequest / WebClient
 *   Max 2026+:     System.Net.Http.HttpClient (.NET 8)
 *
 * Its state is per user (WdxGetUserSetting, WhiteoutDexGlobals.ms):
 *
 *   <plugcfg>\WhiteoutDex_Settings.ini
 *   [Updater]
 *   AutoCheck=0|1               absent = on
 *   LastCheckDate=yyyy-MM-dd    local date of the last automatic check
 */

struct WhiteoutDexUpdaterStruct
(
    githubUser = "FernandoS27",
    githubRepo = "WhiteoutDex",
    -- How long a query may take before the startup check gives up. The check
    -- runs on the UI thread, so this is how long an unreachable GitHub can
    -- hold up 3ds Max once a day.
    queryTimeoutSec = 10,

    fn isSupported = ((maxVersion())[1] >= 20000),

    -- True on Max 2026+ (.NET 8, where WebClient is obsolete)
    fn useHttpClient = ((maxVersion())[1] >= 28000),

    fn getAutoCheck = ((::WdxGetUserSetting "Updater" "AutoCheck" "1") != "0"),

    fn setAutoCheck state = (::WdxSetUserSetting "Updater" "AutoCheck" (if state then "1" else "0")),

    fn todayStamp =
    (
        (dotNetClass "System.DateTime").Now.ToString "yyyy-MM-dd" (dotNetClass "System.Globalization.CultureInfo").InvariantCulture
    ),

    -- The leading digits of one dot-separated part; 0 when there are none,
    -- so a suffix like "0-rc1" cannot turn the comparison into an error.
    fn versionPart parts i =
    (
        if i > parts.count then return 0
        local m = (dotNetClass "System.Text.RegularExpressions.Regex").Match parts[i] "^\\d+"
        if m.Success then (m.Value as integer) else 0
    ),

    -- 1 when rv is newer than lv, -1 when older, 0 when equal
    fn compareVersions lv rv =
    (
        local lP = filterString lv "."
        local rP = filterString rv "."
        for i = 1 to (amax lP.count rP.count) do
        (
            local a = versionPart lP i
            local b = versionPart rP i
            if b > a then return 1
            if b < a then return -1
        )
        0
    ),

    -- Max 2018 ships .NET 4.6, whose default SecurityProtocol can still be an
    -- explicit Ssl3|Tls list. GitHub disabled TLS < 1.2 in early 2018, so
    -- every request fails with "Could not create SSL/TLS secure channel"
    -- until we opt in. 3072 (0xC00) is SecurityProtocolType.Tls12, used as a
    -- number because the enum member does not exist before .NET 4.5. 0 is
    -- SystemDefault (.NET 4.7+), which already allows TLS 1.2 and must be
    -- left alone - pinning it to Tls12 would switch TLS 1.3 off for the
    -- whole process. The value has to go through Enum.ToObject: assigning
    -- the integer (or dotNet.valueToDotNetObject of it) throws "Unable to
    -- convert", verified on Max 2016.
    fn enableTls12 =
    (
        try
        (
            local spm = dotNetClass "System.Net.ServicePointManager"
            local current = spm.SecurityProtocol.value__ as integer
            if current != 0 and (bit.and current 3072) == 0 then
                spm.SecurityProtocol = (dotNetClass "System.Enum").ToObject (dotNetClass "System.Net.SecurityProtocolType") (bit.or current 3072)
        )
        catch (
            format "[WhiteoutDex Updater] Could not enable TLS 1.2: %\n" (getCurrentException())
        )
    ),

    -- ========================================
    -- HTTP Download helpers (dual HttpWebRequest / HttpClient)
    -- ========================================

    -- Download a string from a URL, giving up after queryTimeoutSec
    fn downloadString url =
    (
        local _ex = undefined
        local result = undefined
        if useHttpClient() then
        (
            -- .NET 8: use HttpClient
            local httpClient = dotNetObject "System.Net.Http.HttpClient"
            try (
                httpClient.Timeout = dotNetObject "System.TimeSpan" 0 0 queryTimeoutSec
                httpClient.DefaultRequestHeaders.Add "User-Agent" "WhiteoutDex-Updater"
                httpClient.DefaultRequestHeaders.Add "Accept" "application/vnd.github.v3+json"
                local task = httpClient.GetStringAsync url
                task.Wait()
                result = task.Result
            ) catch (
                _ex = getCurrentException()
            )
            try (httpClient.Dispose()) catch ()
        )
        else
        (
            -- .NET Framework: HttpWebRequest rather than WebClient, which
            -- has no timeout of its own and would wait 100 s by default
            local resp = undefined
            try (
                enableTls12()
                local req = (dotNetClass "System.Net.WebRequest").Create url
                req.Timeout = queryTimeoutSec * 1000
                req.ReadWriteTimeout = queryTimeoutSec * 1000
                req.UserAgent = "WhiteoutDex-Updater"
                req.Accept = "application/vnd.github.v3+json"
                resp = req.GetResponse()
                local reader = dotNetObject "System.IO.StreamReader" (resp.GetResponseStream())
                result = reader.ReadToEnd()
                reader.Close()
            ) catch (
                _ex = getCurrentException()
            )
            if resp != undefined do try (resp.Close()) catch ()
        )
        -- Rethrow outside the catch. No form of `throw` compiles on every
        -- Max inside one: 2027 rejects `throw <value>` there ("only throws
        -- without arguments are permitted in catch expressions") and 2016
        -- rejects the bare `throw` that would satisfy it. Carrying the
        -- exception out in a local sidesteps the restriction entirely,
        -- since it only applies inside the catch.
        if _ex != undefined do throw _ex
        result
    ),

    -- Download a file from a URL
    fn downloadFile url destPath =
    (
        if useHttpClient() then
        (
            -- .NET 8: use HttpClient. Its 100 s default timeout covers the
            -- whole body, which a slow connection can outlast on an installer.
            local httpClient = dotNetObject "System.Net.Http.HttpClient"
            local _ex = undefined
            try (
                httpClient.Timeout = dotNetObject "System.TimeSpan" 0 30 0
                httpClient.DefaultRequestHeaders.Add "User-Agent" "WhiteoutDex-Updater"
                local task = httpClient.GetByteArrayAsync url
                task.Wait()
                local bytes = task.Result
                (dotNetClass "System.IO.File").WriteAllBytes destPath bytes
            ) catch (
                _ex = getCurrentException()
            )
            try (httpClient.Dispose()) catch ()
            -- Rethrown out here rather than in the catch; see downloadString.
            if _ex != undefined do throw _ex
        )
        else
        (
            -- .NET Framework: use WebClient
            enableTls12()
            local wc = dotNetObject "System.Net.WebClient"
            wc.Headers.Add "User-Agent" "WhiteoutDex-Updater"
            wc.DownloadFile url destPath
            wc.Dispose()
        )
    ),

    -- ========================================
    -- GitHub API query
    -- ========================================

    -- #(version, installer URL), either undefined when not found. A release
    -- without an installer named like the one packaging/Whiteout_Setup.iss.in
    -- builds gets no URL, so no other executable attached to it is ever run.
    fn queryLatestRelease =
    (
        local result = #(undefined, undefined)
        try (
            local url = "https://api.github.com/repos/" + githubUser + "/" + githubRepo + "/releases/latest"
            local resp = downloadString url
            local rx = dotNetClass "System.Text.RegularExpressions.Regex"
            local tm = rx.Match resp "\"tag_name\"\\s*:\\s*\"([^\"]+)\""
            if tm.Success then (
                local v = tm.Groups.Item[1].Value
                if v[1] == "v" or v[1] == "V" then v = substring v 2 (v.count - 1)
                result[1] = v
            )
            local am = rx.Match resp "\"browser_download_url\"\\s*:\\s*\"([^\"]*/WhiteoutDex_Setup[^\"/]*\\.exe)\""
            if am.Success then result[2] = am.Groups.Item[1].Value
        ) catch (
            format "[WhiteoutDex Updater] GitHub query failed: %\n" (getCurrentException())
        )
        result
    ),

    fn downloadAndInstall dlUrl ver =
    (
        local tmpDir = (dotNetClass "System.IO.Path").GetTempPath()
        local dlFile = tmpDir + "WhiteoutDex_Setup_v" + ver + ".exe"
        local batFile = tmpDir + "WhiteoutDex_AutoInstall.bat"

        -- Only the download itself reports "Download failed". One try around
        -- everything used to say so for any later error as well, with the
        -- installer already sitting complete in %TEMP%.
        local downloaded = false
        progressStart (::WdxL.t "upd_downloading")
        try (
            downloadFile dlUrl dlFile
            downloaded = doesFileExist dlFile
        ) catch (
            format "[WhiteoutDex Updater] Download failed: %\n" (getCurrentException())
        )
        progressEnd()
        if not downloaded then (
            messageBox (::WdxL.t "upd_download_failed") title:(::WdxL.t "upd_title") beep:false
            return false
        )

        -- Ask user if Max should be closed
        if not (queryBox ((::WdxL.t "upd_close_max_msg") + "\n\n" + (::WdxL.t "upd_save_work_msg")) title:(::WdxL.t "upd_title")) then
            return false

        local started = false
        try (
            -- Batch script: waits until every 3ds Max is closed (the
            -- installer updates all of them), then starts the installer
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
            started = true
        ) catch (
            format "[WhiteoutDex Updater] Could not start the installer: %\n" (getCurrentException())
        )
        if not started then (
            messageBox ((::WdxL.t "upd_launch_failed") + "\n\n" + dlFile) title:(::WdxL.t "upd_title") beep:false
            ShellLaunch "explorer.exe" ("/select,\"" + dlFile + "\"")
            return false
        )

        -- Close Max gracefully; with no argument it offers to save a modified
        -- scene. #noPrompt is quitMax's only option: the #prompt this used to
        -- pass throws "Unknown argument" (verified on 2027), which left Max
        -- open and the installer waiting until the user closed it by hand.
        quitMax()
        true
    ),

    -- ========================================
    -- RUN CHECK
    -- ========================================

    -- manual:true is Settings > Check Now: it ignores AutoCheck and the daily
    -- limit, and reports every outcome instead of only a new version.
    fn check manual:false =
    (
        if not (isSupported()) then (
            format "[WhiteoutDex Updater] Not available on 3ds Max 2016/2017.\n"
            return false
        )
        if not manual then (
            if not (getAutoCheck()) then (
                format "[WhiteoutDex Updater] Automatic update checks are off.\n"
                return false
            )
            local today = todayStamp()
            if (::WdxGetUserSetting "Updater" "LastCheckDate" "") == today then (
                format "[WhiteoutDex Updater] Already checked today.\n"
                return false
            )
            -- Stamped before the query, so a GitHub that cannot be reached
            -- costs one timeout a day rather than one on every startup.
            ::WdxSetUserSetting "Updater" "LastCheckDate" today
        )

        format "[WhiteoutDex Updater] Checking for updates...\n"

        local localVer = ::WdxGetInstalledVersion()
        -- Without a version to compare against, every release would look
        -- new and the user would be offered an "update" to what they have.
        if localVer == undefined then (
            format "[WhiteoutDex Updater] Could not read the installed version.\n"
            if manual then messageBox (::WdxL.t "upd_check_failed") title:(::WdxL.t "upd_title") beep:false
            return false
        )
        format "[WhiteoutDex Updater] Local version: %\n" localVer

        local info = queryLatestRelease()
        local remoteVer = info[1]
        local dlUrl     = info[2]

        if remoteVer == undefined then (
            format "[WhiteoutDex Updater] Could not retrieve remote version.\n"
            if manual then messageBox (::WdxL.t "upd_check_failed") title:(::WdxL.t "upd_title") beep:false
            return false
        )
        format "[WhiteoutDex Updater] Remote version: %\n" remoteVer

        if (compareVersions localVer remoteVer) <= 0 then (
            format "[WhiteoutDex Updater] Version is up to date.\n"
            if manual then
                messageBox ((::WdxL.t "upd_up_to_date") + "\n\n" + (::WdxL.t "upd_installed") + "  v" + localVer) title:(::WdxL.t "upd_title") beep:false
            return false
        )

        format "[WhiteoutDex Updater] New version available: % -> %\n" localVer remoteVer

        local msg = (::WdxL.t "upd_new_version_msg") + "\n\n"
        msg += (::WdxL.t "upd_installed") + "  v" + localVer + "\n"
        msg += (::WdxL.t "upd_available") + "   v" + remoteVer + "\n\n"
        msg += (::WdxL.t "upd_changelog_at") + "\ngithub.com/" + githubUser + "/" + githubRepo + "/releases\n\n"
        msg += (::WdxL.t "upd_install_now")

        if queryBox msg title:(::WdxL.t "upd_title_available") then (
            if dlUrl != undefined then
                downloadAndInstall dlUrl remoteVer
            else
                ShellLaunch ("https://github.com/" + githubUser + "/" + githubRepo + "/releases/latest") ""
        )
        true
    )
)

global WdxUpdater = WhiteoutDexUpdaterStruct()
WdxUpdater.check()
