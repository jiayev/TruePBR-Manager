@echo off
setlocal

REM ============================================================
REM  TruePBR Manager - Build Script
REM  
REM  Prerequisites:
REM    - Set VCPKG_ROOT environment variable to your vcpkg directory
REM    - Run from VS Developer Command Prompt / Terminal
REM      OR have vcvarsall.bat accessible via vswhere
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

set "TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
set "SRC_DIR=%~dp0"
set "BUILD_DIR=%SRC_DIR%build"

REM --- Detect VS Developer Environment ---
REM If cl.exe is already in PATH (e.g. running from VS Developer Terminal), skip.
where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo cl.exe not found in PATH. Attempting to set up VS environment...
    REM Try vswhere to auto-detect VS installation
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "%VSWHERE%" (
        for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VS_PATH=%%i"
    )
    if defined VS_PATH (
        call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" amd64
    ) else (
        echo ERROR: Could not find Visual Studio. Please run from VS Developer Command Prompt.
        exit /b 1
    )
)

REM --- Configure ---
cmake -S "%SRC_DIR%." -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=%TOOLCHAIN%" -DVCPKG_TARGET_TRIPLET=x64-windows
if %errorlevel% neq 0 (
    echo CMake configure failed
    exit /b %errorlevel%
)

REM --- Build ---
cmake --build "%BUILD_DIR%"
if %errorlevel% neq 0 (
    echo Build failed
    exit /b %errorlevel%
)

echo.
echo ============================================================
echo  Build succeeded!
echo  Output: %BUILD_DIR%\src\TruePBR-Manager.exe
echo ============================================================
endlocal
