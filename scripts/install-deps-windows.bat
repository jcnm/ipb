@echo off
REM IPB Dependencies Installation Script for Windows
REM This is a wrapper that calls the PowerShell script

setlocal enabledelayedexpansion

echo === IPB Dependencies Installation Script for Windows ===
echo.

REM Check if PowerShell is available
where powershell >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] PowerShell is required but not found
    echo Please install PowerShell or run install-deps-windows.ps1 directly
    exit /b 1
)

REM Get the directory of this script
set "SCRIPT_DIR=%~dp0"

REM Check for help flag
if "%1"=="-h" goto :help
if "%1"=="--help" goto :help
if "%1"=="/?" goto :help

REM Run the PowerShell script with all arguments
powershell -ExecutionPolicy Bypass -File "%SCRIPT_DIR%install-deps-windows.ps1" %*
exit /b %errorlevel%

:help
echo Usage: install-deps-windows.bat [OPTIONS]
echo.
echo IPB Dependencies Installation Script for Windows
echo.
echo Options:
echo   -h, --help, /?     Show this help message
echo   -Minimal           Install only essential dependencies
echo   -Full              Install all optional dependencies (default)
echo   -UseVcpkg          Use vcpkg package manager (recommended)
echo   -UseChoco          Use Chocolatey package manager
echo   -DryRun            Show what would be installed without installing
echo   -Arch x64          Target architecture: x64, x86, or arm64 (default: x64)
echo.
echo Examples:
echo   install-deps-windows.bat                    Auto-detect and install (x64)
echo   install-deps-windows.bat -Arch x86          Install for 32-bit
echo   install-deps-windows.bat -UseVcpkg          Use vcpkg
echo   install-deps-windows.bat -Minimal           Essential deps only
echo.
echo For more options, see: install-deps-windows.ps1 -Help
echo.
exit /b 0
