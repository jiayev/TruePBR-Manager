@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM  TruePBR Manager - Release Build
REM
REM  Builds with normal logging (info level).
REM  Produces a packaged zip in dist/package/.
REM
REM  Prerequisites:
REM    - Set VCPKG_ROOT environment variable
REM    - Run from VS Developer Command Prompt, or let this
REM      script auto-detect via vswhere
REM ============================================================

REM --- Validate VCPKG_ROOT ---
if not defined VCPKG_ROOT (
    echo ERROR: VCPKG_ROOT environment variable is not set.
    echo Please set it to your vcpkg installation directory, e.g.:
    echo   set VCPKG_ROOT=C:\path\to\vcpkg
    exit /b 1
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo ERROR: vcpkg.cmake not found at %VCPKG_ROOT%
    exit /b 1
)

REM Save user's VCPKG_ROOT before vcvarsall potentially overwrites it
set "USER_VCPKG_ROOT=%VCPKG_ROOT%"
set "SRC_DIR=%~dp0"
set "BUILD_DIR=%SRC_DIR%build"

REM --- Detect VS Developer Environment ---
where cl.exe >nul 2>&1
if !errorlevel! neq 0 (
    echo cl.exe not found in PATH. Attempting to set up VS environment...

    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo ERROR: vswhere.exe not found. Please run from VS Developer Command Prompt.
        exit /b 1
    )

    set "VS_PATH="
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -property installationPath`) do set "VS_PATH=%%i"

    if not defined VS_PATH (
        echo ERROR: Could not find Visual Studio installation via vswhere.
        exit /b 1
    )

    echo Found VS at: !VS_PATH!

    if not exist "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" (
        echo ERROR: vcvarsall.bat not found at !VS_PATH!
        exit /b 1
    )

    call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" amd64
    if !errorlevel! neq 0 (
        echo ERROR: vcvarsall.bat failed
        exit /b 1
    )
)

REM --- Restore VCPKG_ROOT ---
set "VCPKG_ROOT=!USER_VCPKG_ROOT!"
set "TOOLCHAIN=!VCPKG_ROOT!\scripts\buildsystems\vcpkg.cmake"

REM --- Verify compiler is available ---
where cl.exe >nul 2>&1
if !errorlevel! neq 0 (
    echo ERROR: cl.exe still not found after environment setup.
    exit /b 1
)

set "GENERATOR=Visual Studio 17 2022"

echo.
echo ============ RELEASE BUILD ============
echo Generator:  !GENERATOR!
echo VCPKG_ROOT: !VCPKG_ROOT!
echo Toolchain:  !TOOLCHAIN!
echo Source:     %SRC_DIR%
echo Build:      %BUILD_DIR%
echo.

REM --- Auto-clean stale CMake cache if platform mismatches ---
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /C:"CMAKE_GENERATOR_PLATFORM:INTERNAL=x64" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if !errorlevel! neq 0 (
        echo Detected platform mismatch in CMake cache. Cleaning build cache...
        del /q "%BUILD_DIR%\CMakeCache.txt" 2>nul
        rmdir /s /q "%BUILD_DIR%\CMakeFiles" 2>nul
    )
)

REM --- Configure (with pack enabled) ---
cmake -S "%SRC_DIR%." -B "%BUILD_DIR%" -G "!GENERATOR!" -A x64 ^
    "-DCMAKE_TOOLCHAIN_FILE=!TOOLCHAIN!" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DTRUEPBR_PACK=ON
if !errorlevel! neq 0 (
    echo CMake configure failed
    exit /b !errorlevel!
)

REM --- Build (Release) ---
cmake --build "%BUILD_DIR%" --config Release
if !errorlevel! neq 0 (
    echo Build failed
    exit /b !errorlevel!
)

echo.
echo ============================================================
echo  Release build succeeded!
echo  Output:  %SRC_DIR%dist\TruePBR-Manager\TruePBR-Manager.exe
echo  Package: %SRC_DIR%dist\package\
echo  Log level: Normal (info)
echo ============================================================
endlocal
