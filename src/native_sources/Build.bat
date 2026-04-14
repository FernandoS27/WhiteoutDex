@echo off
setlocal EnableDelayedExpansion
title WhiteoutDex - Unified Native Plugin Build

set "PROJECT_DIR=%~dp0"
set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "OUTPUT_DIR=%PROJECT_DIR%\output"
set "APPDATA_DIR=%APPDATA%\Autodesk\ApplicationPlugins\WhiteoutDex"
set "NATIVE_DIR=%APPDATA_DIR%\native plugins"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
if not exist "%APPDATA_DIR%" mkdir "%APPDATA_DIR%"
if not exist "%NATIVE_DIR%" mkdir "%NATIVE_DIR%"

echo ============================================================
echo  WhiteoutDex - Unified Native Plugin Build
echo  Project:  %PROJECT_DIR%
echo  AppData:  %APPDATA_DIR%
echo ============================================================
echo.

REM ============================================================
REM  Optional: Build a single version
REM    Build.bat 2025    - build for Max 2025 only
REM    Build.bat         - build for all installed SDKs
REM ============================================================
set "SINGLE_VERSION=%~1"

REM ============================================================
REM  Find Visual Studio and determine generator
REM ============================================================
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "CMAKE_GEN=Visual Studio 17 2022"
set "PLATFORM_TOOLSET=v143"

if exist "%VSWHERE%" (
    for /f "delims=" %%R in ('"%VSWHERE%" -version [18.0^,19.0^) -property installationPath 2^>nul') do (
        if exist "%%R" (
            set "CMAKE_GEN=Visual Studio 18 2026"
            set "PLATFORM_TOOLSET=v145"
        )
    )
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
        set "MSBUILD=%%i"
    )
)

echo Generator: %CMAKE_GEN%
echo.

REM ============================================================
REM  Build common libraries once (WhiteoutLib, zlib, WhiteoutDexNative)
REM  These have no Max SDK dependency and are shared across all versions.
REM ============================================================
set "COMMON_BUILD_DIR=%PROJECT_DIR%\build_common"
set "COMMON_FWD=%COMMON_BUILD_DIR:\=/%"
set "SRC_FWD=%PROJECT_DIR:\=/%"

echo ============================================================
echo  Building common libraries (WhiteoutLib, zlib, WhiteoutDexNative)
echo ============================================================

if exist "%COMMON_BUILD_DIR%" rmdir /s /q "%COMMON_BUILD_DIR%"
mkdir "%COMMON_BUILD_DIR%"
pushd "%COMMON_BUILD_DIR%"

cmake -G "%CMAKE_GEN%" -A x64 ^
    -DBUILD_COMMON_ONLY=ON ^
    "%SRC_FWD%"

if errorlevel 1 (
    echo *** Common libraries CMake configure FAILED ***
    popd
    goto :done
)

cmake --build . --config Release -- /m:4

if errorlevel 1 (
    echo *** Common libraries build FAILED ***
    popd
    goto :done
)

echo === Common libraries built successfully ===
popd
echo.

REM ============================================================
REM  Build per-version SDK plugins (Max 2016-2027)
REM ============================================================
set BUILT=0
set FAILED=0
set SKIPPED=0

for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027) do (
    set "DO_BUILD=1"
    if defined SINGLE_VERSION (
        if not "%%V"=="!SINGLE_VERSION!" set "DO_BUILD=0"
    )
    if "!DO_BUILD!"=="1" (
        set "SDK_PATH=C:\Program Files\Autodesk\3ds Max %%V SDK\maxsdk"

        if exist "!SDK_PATH!\include\max.h" (
            echo ============================================================
            echo [%%V] SDK found - Configuring all plugins...
            echo ============================================================

            set "BUILD_DIR=%PROJECT_DIR%\build_%%V"
            if exist "!BUILD_DIR!" rmdir /s /q "!BUILD_DIR!"
            mkdir "!BUILD_DIR!"
            pushd "!BUILD_DIR!"

            set "SDK_FWD=!SDK_PATH:\=/!"

            cmake -G "!CMAKE_GEN!" -A x64 ^
                -DMAX_VERSION=%%V ^
                -DMAXSDK_PATH="!SDK_FWD!" ^
                -DPREBUILT_COMMON_DIR="!COMMON_FWD!" ^
                "!SRC_FWD!"

            if errorlevel 1 (
                echo [%%V] *** CMake configure FAILED ***
                set /a FAILED+=1
                popd
            ) else (
                cmake --build . --config Release -- /m:4

                if errorlevel 1 (
                    echo [%%V] *** Some targets FAILED ***
                    set /a FAILED+=1
                ) else (
                    echo [%%V] === SUCCESS ===
                    set /a BUILT+=1
                )

                REM Create output and AppData folders
                if not exist "%OUTPUT_DIR%\Max%%V" mkdir "%OUTPUT_DIR%\Max%%V"
                if not exist "%NATIVE_DIR%\Max%%V" mkdir "%NATIVE_DIR%\Max%%V"

                REM Copy all native plugins to both output and AppData
                for %%F in (
                    "plugins\Release\blp.bmi"
                    "plugins\Release\MDLXExporter.dle"
                    "plugins\Release\MDLXImporter.dle"
                    "plugins\Release\Wc3Particles1.dlo"
                    "plugins\Release\Wc3Particles2.dlo"
                    "plugins\Release\Wc3Ribbon.dlo"
                    "plugins\Release\WhiteoutDexExtractor.dlx"
                ) do (
                    if exist "%%~F" (
                        copy /Y "%%~F" "%OUTPUT_DIR%\Max%%V\" >nul
                        copy /Y "%%~F" "%NATIVE_DIR%\Max%%V\" >nul
                        echo [%%V]   %%~nxF  --^> output + AppData
                    )
                )

                REM Standalone renderer (no Max SDK dependency, but built alongside)
                if exist "standalone\WhiteoutDexRenderer.exe" (
                    if not exist "%OUTPUT_DIR%\Standalone" mkdir "%OUTPUT_DIR%\Standalone"
                    copy /Y "standalone\WhiteoutDexRenderer.exe" "%OUTPUT_DIR%\Standalone\" >nul
                    echo [%%V]   WhiteoutDexRenderer.exe -^> Standalone\
                )
                popd
            )
        ) else (
            echo [%%V] SDK not found, skipping
            set /a SKIPPED+=1
        )
        echo.
    )
)

echo.
echo ============================================================
echo  Max SDK Plugins: %BUILT% built, %FAILED% failed, %SKIPPED% skipped
echo ============================================================

REM ============================================================
REM  Managed Wrappers (WhiteoutDexNative.dll)
REM  Uses the native static lib from the common build.
REM ============================================================
if not defined MSBUILD (
    echo.
    echo WARNING: MSBuild not found - skipping managed wrappers.
    goto :deploy_scripts
)

echo.
echo ============================================================
echo  Building WhiteoutDexNative managed wrappers...
echo  Using native libs from: %COMMON_BUILD_DIR%
echo ============================================================

REM --- .NET Framework 4.8 (Max 2016-2025) ---
echo.
echo [fw48] WhiteoutDexNative.dll (.NET Framework 4.8)
"%MSBUILD%" "%PROJECT_DIR%\WhiteoutDexNative\wrapper\WhiteoutDexNative_fw48.vcxproj" ^
    /p:Configuration=Release ^
    /p:Platform=x64 ^
    /p:PlatformToolset=%PLATFORM_TOOLSET% ^
    /p:NativeBuildDir="%COMMON_BUILD_DIR%" ^
    /v:minimal

if errorlevel 1 (
    echo [fw48] *** FAILED ***
) else (
    echo [fw48] OK
    for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025) do (
        if not exist "%NATIVE_DIR%\Max%%V" mkdir "%NATIVE_DIR%\Max%%V"
        if not exist "%OUTPUT_DIR%\Max%%V" mkdir "%OUTPUT_DIR%\Max%%V"
        copy /Y "%PROJECT_DIR%\WhiteoutDexNative\output\Max2016-2025\WhiteoutDexNative.dll" "%NATIVE_DIR%\Max%%V\WhiteoutDexNative.dll" >nul 2>nul
        copy /Y "%PROJECT_DIR%\WhiteoutDexNative\output\Max2016-2025\WhiteoutDexNative.dll" "%OUTPUT_DIR%\Max%%V\WhiteoutDexNative.dll" >nul 2>nul
        echo [%%V]   WhiteoutDexNative.dll (fw48)  --^> output + AppData
    )
)

REM --- .NET 8.0 (Max 2026-2027) ---
echo.
echo [net8] WhiteoutDexNative.dll (.NET 8.0)

if exist "%ProgramFiles%\dotnet\dotnet.exe" (
    dotnet restore "%PROJECT_DIR%\WhiteoutDexNative\wrapper\WhiteoutDexNative_net8.vcxproj" >nul 2>nul
)

"%MSBUILD%" "%PROJECT_DIR%\WhiteoutDexNative\wrapper\WhiteoutDexNative_net8.vcxproj" ^
    /p:Configuration=Release ^
    /p:Platform=x64 ^
    /p:PlatformToolset=%PLATFORM_TOOLSET% ^
    /p:NativeBuildDir="%COMMON_BUILD_DIR%" ^
    /v:minimal

if errorlevel 1 (
    echo [net8] *** FAILED ***
) else (
    echo [net8] OK
    for %%V in (2026 2027) do (
        if not exist "%NATIVE_DIR%\Max%%V" mkdir "%NATIVE_DIR%\Max%%V"
        if not exist "%OUTPUT_DIR%\Max%%V" mkdir "%OUTPUT_DIR%\Max%%V"
        copy /Y "%PROJECT_DIR%\WhiteoutDexNative\output\Max2026-2027\WhiteoutDexNative.dll" "%NATIVE_DIR%\Max%%V\WhiteoutDexNative.dll" >nul 2>nul
        copy /Y "%PROJECT_DIR%\WhiteoutDexNative\output\Max2026-2027\WhiteoutDexNative.dll" "%OUTPUT_DIR%\Max%%V\WhiteoutDexNative.dll" >nul 2>nul
        echo [%%V]   WhiteoutDexNative.dll (net8)  --^> output + AppData
    )
)

REM ============================================================
REM  Deploy Scripts + PackageContents.xml to AppData
REM  Scripts are shared across all Max versions.
REM ============================================================
:deploy_scripts
echo.
echo ============================================================
echo  Deploying scripts and PackageContents.xml to AppData...
echo ============================================================

set "SRC_SCRIPTS=%PROJECT_DIR%\..\src"

REM PackageContents.xml
if exist "%SRC_SCRIPTS%\..\PackageContents.xml" (
    copy /Y "%SRC_SCRIPTS%\..\PackageContents.xml" "%APPDATA_DIR%\PackageContents.xml" >nul
    echo   PackageContents.xml
)

REM Macroscripts
set "DST_MACRO=%APPDATA_DIR%\scripts\macroscripts"
if not exist "%DST_MACRO%" mkdir "%DST_MACRO%"
if exist "%SRC_SCRIPTS%\macroscripts" (
    for %%F in ("%SRC_SCRIPTS%\macroscripts\*.mcr") do (
        copy /Y "%%F" "%DST_MACRO%\" >nul
        echo   macroscripts\%%~nxF
    )
)

REM Pre-Startup Scripts
set "DST_PRE=%APPDATA_DIR%\scripts\pre_startup_scripts"
if not exist "%DST_PRE%" mkdir "%DST_PRE%"
if exist "%SRC_SCRIPTS%\pre_startup_scripts" (
    for %%F in ("%SRC_SCRIPTS%\pre_startup_scripts\*.ms") do (
        copy /Y "%%F" "%DST_PRE%\" >nul
        echo   pre_startup_scripts\%%~nxF
    )
)

REM Post-Startup Scripts
set "DST_POST=%APPDATA_DIR%\scripts\post_startup_scripts"
if not exist "%DST_POST%" mkdir "%DST_POST%"
if exist "%SRC_SCRIPTS%\post_startup_scripts" (
    for %%F in ("%SRC_SCRIPTS%\post_startup_scripts\*.ms") do (
        copy /Y "%%F" "%DST_POST%\" >nul
        echo   post_startup_scripts\%%~nxF
    )
)

REM Scripted Plugins
set "DST_PLUG=%APPDATA_DIR%\scripts\scripted_plugins"
if not exist "%DST_PLUG%" mkdir "%DST_PLUG%"
if exist "%SRC_SCRIPTS%\scripted_plugins" (
    for %%F in ("%SRC_SCRIPTS%\scripted_plugins\*.ms") do (
        copy /Y "%%F" "%DST_PLUG%\" >nul
        echo   scripted_plugins\%%~nxF
    )
)

:done
echo.
echo ============================================================
echo  Done!
echo ============================================================
echo.
echo Output directory: %OUTPUT_DIR%
if exist "%OUTPUT_DIR%" (
    for /d %%D in ("%OUTPUT_DIR%\Max*") do (
        echo   %%~nxD\
        for %%F in ("%%D\*") do echo     %%~nxF
    )
)
echo.
echo AppData directory: %APPDATA_DIR%
echo   native plugins\
if exist "%NATIVE_DIR%" (
    for /d %%D in ("%NATIVE_DIR%\Max*") do (
        set "FILECOUNT=0"
        for %%F in ("%%D\*") do set /a FILECOUNT+=1
        echo     %%~nxD\  (!FILECOUNT! files^)
    )
)
echo   scripts\
if exist "%APPDATA_DIR%\scripts" (
    for /d %%D in ("%APPDATA_DIR%\scripts\*") do (
        set "FILECOUNT=0"
        for %%F in ("%%D\*") do set /a FILECOUNT+=1
        echo     %%~nxD\  (!FILECOUNT! files^)
    )
)
echo.
pause
