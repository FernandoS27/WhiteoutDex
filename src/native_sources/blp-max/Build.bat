@echo off
setlocal EnableDelayedExpansion
title blp-max - Build All Max Versions

set "PROJECT_DIR=%~dp0"
set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "OUTPUT_DIR=%PROJECT_DIR%\output"
set "WHITEOUTDEX_DIR=%APPDATA%\Autodesk\ApplicationPlugins\WhiteoutDex\native plugins"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

echo ============================================================
echo  blp-max - Building for all installed Max SDKs
echo  Project: %PROJECT_DIR%
echo ============================================================
echo.

set BUILT=0
set FAILED=0
set SKIPPED=0

for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027) do (
    set "SDK_PATH=C:\Program Files\Autodesk\3ds Max %%V SDK\maxsdk"

    if exist "!SDK_PATH!\include\max.h" (
        echo ============================================================
        echo [%%V] SDK found - Configuring...
        echo ============================================================

        if exist "%PROJECT_DIR%\build_%%V" rmdir /s /q "%PROJECT_DIR%\build_%%V"
        mkdir "%PROJECT_DIR%\build_%%V"
        pushd "%PROJECT_DIR%\build_%%V"

        set "SRC_FWD=!PROJECT_DIR:\=/!"
        set "SDK_FWD=!SDK_PATH:\=/!"

        set "CMAKE_GEN=Visual Studio 17 2022"
        set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
        if exist "!VSWHERE!" (
            for /f "delims=" %%R in ('"!VSWHERE!" -version [18.0^,19.0^) -property installationPath 2^>nul') do (
                if exist "%%R" set "CMAKE_GEN=Visual Studio 18 2026"
            )
        )
        echo [%%V] Using generator: !CMAKE_GEN!

        cmake -G "!CMAKE_GEN!" -A x64 -DMAXSDK_PATH="!SDK_FWD!" "!SRC_FWD!"

        if errorlevel 1 (
            echo [%%V] *** CMake configure FAILED ***
            set /a FAILED+=1
            popd
        ) else (
            echo [%%V] Building...
            cmake --build . --config Release

            if errorlevel 1 (
                echo [%%V] *** Build FAILED ***
                set /a FAILED+=1
            ) else (
                echo [%%V] === SUCCESS ===
                set /a BUILT+=1

                REM Direct path to built .bmi
                set "BMI_FILE=%PROJECT_DIR%\build_%%V\Release\blp.bmi"

                if exist "!BMI_FILE!" (
                    REM Copy to local output
                    copy /Y "!BMI_FILE!" "%OUTPUT_DIR%\blp_%%V.bmi" >nul
                    echo [%%V] Copied to output\blp_%%V.bmi

                    REM Copy to WhiteoutDex native plugins
                    if not exist "%WHITEOUTDEX_DIR%\Max%%V" mkdir "%WHITEOUTDEX_DIR%\Max%%V"
                    copy /Y "!BMI_FILE!" "%WHITEOUTDEX_DIR%\Max%%V\blp.bmi" >nul
                    echo [%%V] Installed to WhiteoutDex\native plugins\Max%%V\blp.bmi
                ) else (
                    echo [%%V] WARNING: .bmi not found at expected path
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

echo ============================================================
echo  Done: %BUILT% built, %FAILED% failed, %SKIPPED% skipped
echo ============================================================
echo.
echo Output files:
dir /b "%OUTPUT_DIR%\*.bmi" 2>nul || echo   (none)
echo.
pause
