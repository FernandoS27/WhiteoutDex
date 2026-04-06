@echo off
setlocal EnableDelayedExpansion
title WhiteoutDex - Unified Native Plugin Build

set "PROJECT_DIR=%~dp0"
set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "OUTPUT_DIR=%PROJECT_DIR%\output"
set "WHITEOUTDEX_DIR=%APPDATA%\Autodesk\ApplicationPlugins\WhiteoutDex\native plugins"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

echo ============================================================
echo  WhiteoutDex - Unified Native Plugin Build
echo  Project: %PROJECT_DIR%
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

cmake --build . --config Release -- /m

if errorlevel 1 (
    echo *** Common libraries build FAILED ***
    popd
    goto :done
)

echo === Common libraries built successfully ===
popd
echo.

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
                cmake --build . --config Release -- /m

                if errorlevel 1 (
                    echo [%%V] *** Some targets FAILED ***
                    set /a FAILED+=1
                ) else (
                    echo [%%V] === SUCCESS ===
                    set /a BUILT+=1
                )

                REM Copy whatever was built, even if some targets failed
                if not exist "%OUTPUT_DIR%\Max%%V" mkdir "%OUTPUT_DIR%\Max%%V"
                if not exist "%WHITEOUTDEX_DIR%\Max%%V" mkdir "%WHITEOUTDEX_DIR%\Max%%V"

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
                        copy /Y "%%~F" "%WHITEOUTDEX_DIR%\Max%%V\" >nul
                        echo [%%V]   %%~nxF
                    )
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
    goto :done
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
        if not exist "%WHITEOUTDEX_DIR%\Max%%V" mkdir "%WHITEOUTDEX_DIR%\Max%%V"
        if not exist "%OUTPUT_DIR%\Max%%V" mkdir "%OUTPUT_DIR%\Max%%V"
        copy /Y "%PROJECT_DIR%\WhiteoutDexNative\output\Max2016-2025\WhiteoutDexNative.dll" "%WHITEOUTDEX_DIR%\Max%%V\WhiteoutDexNative.dll" >nul 2>nul
        copy /Y "%PROJECT_DIR%\WhiteoutDexNative\output\Max2016-2025\WhiteoutDexNative.dll" "%OUTPUT_DIR%\Max%%V\WhiteoutDexNative.dll" >nul 2>nul
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
        if not exist "%WHITEOUTDEX_DIR%\Max%%V" mkdir "%WHITEOUTDEX_DIR%\Max%%V"
        if not exist "%OUTPUT_DIR%\Max%%V" mkdir "%OUTPUT_DIR%\Max%%V"
        copy /Y "%PROJECT_DIR%\WhiteoutDexNative\output\Max2026-2027\WhiteoutDexNative.dll" "%WHITEOUTDEX_DIR%\Max%%V\WhiteoutDexNative.dll" >nul 2>nul
        copy /Y "%PROJECT_DIR%\WhiteoutDexNative\output\Max2026-2027\WhiteoutDexNative.dll" "%OUTPUT_DIR%\Max%%V\WhiteoutDexNative.dll" >nul 2>nul
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
pause
