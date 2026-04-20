@echo off
setlocal EnableDelayedExpansion
title WhiteoutDexTextureBrowser - Build

set "PROJECT_DIR=%~dp0"
set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "OUTPUT_DIR=%PROJECT_DIR%\output"
set "WHITEOUTDEX_DIR=%APPDATA%\Autodesk\ApplicationPlugins\WhiteoutDex\native plugins"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

echo ============================================================
echo  WhiteoutDexTextureBrowser - Build
echo  Project: %PROJECT_DIR%
echo ============================================================
echo.
echo  Max 2016-2025:  .NET Framework 4.8
echo  Max 2026-2027:  .NET 8.0
echo.

REM ============================================================
REM  Find Visual Studio and MSBuild
REM ============================================================
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found!
    echo Visual Studio 2022 or later must be installed.
    pause
    exit /b 1
)

set "CMAKE_GEN=Visual Studio 17 2022"
set "PLATFORM_TOOLSET=v143"
for /f "delims=" %%R in ('"%VSWHERE%" -version [18.0^,19.0^) -property installationPath 2^>nul') do (
    if exist "%%R" (
        set "CMAKE_GEN=Visual Studio 18 2026"
        set "PLATFORM_TOOLSET=v145"
    )
)
echo Using generator: %CMAKE_GEN%
echo Platform toolset: %PLATFORM_TOOLSET%

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD=%%i"
)

if not defined MSBUILD (
    echo ERROR: MSBuild not found!
    echo Install Visual Studio with "Desktop development with C++" workload.
    pause
    exit /b 1
)

echo MSBuild: %MSBUILD%
echo.

REM ============================================================
REM  Step 1: Configure native static library (CMake)
REM ============================================================
echo ============================================================
echo  [1/4] Configuring native library (CMake)
echo ============================================================

if exist "%PROJECT_DIR%\build" rmdir /s /q "%PROJECT_DIR%\build"

set "SRC_FWD=%PROJECT_DIR:\=/%"

cmake -B build -G "%CMAKE_GEN%" -A x64 ^
    -DWHITEOUTDEX_BUILD_TESTS=OFF ^
    "%SRC_FWD%"

if errorlevel 1 (
    echo.
    echo [1/4] *** CMake configure FAILED ***
    pause
    exit /b 1
)

echo.
echo ============================================================
echo  [2/4] Building native library
echo ============================================================

cmake --build build --config Release -- /m

if errorlevel 1 (
    echo.
    echo [2/4] *** Native library build FAILED ***
    pause
    exit /b 1
)

echo.
echo  Native library: OK
echo.

REM ============================================================
REM  Step 3: Managed DLL for Max 2022-2025 (.NET Framework 4.8)
REM ============================================================
echo ============================================================
echo  [3/4] WhiteoutDexTextureBrowser.dll for Max 2016-2025
echo        (.NET Framework 4.8, /clr)
echo ============================================================

mkdir "%OUTPUT_DIR%\Max2016-2025" >nul 2>&1

"%MSBUILD%" wrapper\WhiteoutDexTextureBrowser_fw48.vcxproj ^
    /p:Configuration=Release ^
    /p:Platform=x64 ^
    /p:PlatformToolset=%PLATFORM_TOOLSET% ^
    /p:NativeBuildDir="%PROJECT_DIR%\build" ^
    /v:minimal

if errorlevel 1 (
    echo.
    echo [3/4] *** .NET Framework 4.8 DLL build FAILED ***
    echo Check that "C++/CLI support for v143 build tools" is enabled
    echo in the Visual Studio Installer.
    echo.
) else (
    echo.
    echo  Max 2016-2025 DLL: OK
    echo.

    REM Install to WhiteoutDex native plugins for each applicable version
    for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025) do (
        if not exist "%WHITEOUTDEX_DIR%\Max%%V" mkdir "%WHITEOUTDEX_DIR%\Max%%V"
        copy /Y "%OUTPUT_DIR%\Max2016-2025\WhiteoutDexTextureBrowser.dll" "%WHITEOUTDEX_DIR%\Max%%V\WhiteoutDexTextureBrowser.dll" >nul
    )
    echo  Installed to WhiteoutDex native plugins (Max 2016-2025)
)

REM ============================================================
REM  Step 4: Managed DLL for Max 2026+ (.NET 8)
REM ============================================================
echo ============================================================
echo  [4/4] WhiteoutDexTextureBrowser.dll for Max 2026-2027
echo        (.NET 8.0, /clr:netcore)
echo ============================================================

set NET8_FAILED=0
mkdir "%OUTPUT_DIR%\Max2026-2027" >nul 2>&1

REM NuGet Restore
if exist "%ProgramFiles%\dotnet\dotnet.exe" (
    dotnet restore wrapper\WhiteoutDexTextureBrowser_net8.vcxproj
) else (
    echo WARNING: dotnet CLI not found, NuGet packages may not resolve.
    echo Install .NET 8 SDK from https://dotnet.microsoft.com
)

REM Find .NET 8 SDK Sdks path for MSBuild
set "SDKBASE=%ProgramFiles%\dotnet\sdk"
set "MSBuildSDKsPath="
for /d %%d in ("%SDKBASE%\8.0.*") do set "MSBuildSDKsPath=%%d\Sdks"

if not defined MSBuildSDKsPath (
    echo WARNING: .NET 8 SDK not found in %SDKBASE%
    echo Install .NET 8 SDK from https://dotnet.microsoft.com
    set NET8_FAILED=1
    goto :skip_net8
)

echo  .NET SDK Sdks: %MSBuildSDKsPath%

"%MSBUILD%" wrapper\WhiteoutDexTextureBrowser_net8.vcxproj ^
    /p:Configuration=Release ^
    /p:Platform=x64 ^
    /p:PlatformToolset=%PLATFORM_TOOLSET% ^
    /p:NativeBuildDir="%PROJECT_DIR%\build" ^
    /v:minimal

if errorlevel 1 (
    echo.
    echo WARNING: .NET 8 DLL build failed.
    echo.
    echo Possible causes:
    echo   - .NET 8 SDK not installed
    echo   - "C++/CLI for .NET Core" component missing
    echo.
    set NET8_FAILED=1
) else (
    echo.
    echo  Max 2026-2027 DLL: OK
    echo.

    REM Install to WhiteoutDex native plugins for each applicable version
    for %%V in (2026 2027) do (
        if not exist "%WHITEOUTDEX_DIR%\Max%%V" mkdir "%WHITEOUTDEX_DIR%\Max%%V"
        copy /Y "%OUTPUT_DIR%\Max2026-2027\WhiteoutDexTextureBrowser.dll" "%WHITEOUTDEX_DIR%\Max%%V\WhiteoutDexTextureBrowser.dll" >nul
    )
    echo  Installed to WhiteoutDex native plugins (Max 2026-2027)
)

:skip_net8

REM ============================================================
REM  Summary
REM ============================================================
echo.
echo ============================================================
echo  Summary
echo ============================================================
echo.

if exist "%OUTPUT_DIR%\Max2016-2025\WhiteoutDexTextureBrowser.dll" (
    echo  Max 2016-2025:  OK  -^> output\Max2016-2025\WhiteoutDexTextureBrowser.dll
) else (
    echo  Max 2016-2025:  MISSING
)

if exist "%OUTPUT_DIR%\Max2026-2027\WhiteoutDexTextureBrowser.dll" (
    echo  Max 2026-2027:  OK  -^> output\Max2026-2027\WhiteoutDexTextureBrowser.dll
) else (
    echo  Max 2026-2027:  MISSING
)

echo.
pause
