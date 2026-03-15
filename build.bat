@echo off
REM ============================================================
REM  TruePBR Manager - Build Entry Point
REM
REM  Usage:
REM    build.bat debug     - Debug build (advanced logging, deploy to dist/)
REM    build.bat release   - Release build (normal logging, packaged zip)
REM    build.bat           - Defaults to debug
REM ============================================================

set "MODE=%~1"
if /I "%MODE%"=="" set "MODE=debug"

if /I "%MODE%"=="debug" (
    call "%~dp0builddebug.bat"
    exit /b %errorlevel%
)

if /I "%MODE%"=="release" (
    call "%~dp0buildrelease.bat"
    exit /b %errorlevel%
)

echo ERROR: Unknown build mode "%MODE%"
echo Usage: build.bat [debug^|release]
exit /b 1
