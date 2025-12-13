# IPB Dependencies Installation Script for Windows
# Requires PowerShell 5.1+ or PowerShell Core 7+
# Supports vcpkg and Chocolatey package managers

param(
    [switch]$Help,
    [switch]$Minimal,
    [switch]$Full,
    [switch]$UseVcpkg,
    [switch]$UseChoco,
    [switch]$SkipVcpkg,
    [switch]$DryRun,
    [string]$VcpkgRoot = "",
    [string]$InstallDir = ""
)

$ErrorActionPreference = "Stop"

# Colors for output
function Write-Info { Write-Host "[INFO] $args" -ForegroundColor Blue }
function Write-Success { Write-Host "[SUCCESS] $args" -ForegroundColor Green }
function Write-Warn { Write-Host "[WARNING] $args" -ForegroundColor Yellow }
function Write-Err { Write-Host "[ERROR] $args" -ForegroundColor Red }

function Show-Help {
    @"
IPB Dependencies Installation Script for Windows

Usage: .\install-deps-windows.ps1 [OPTIONS]

Options:
  -Help              Show this help message
  -Minimal           Install only essential dependencies
  -Full              Install all optional dependencies (default)
  -UseVcpkg          Use vcpkg package manager (recommended)
  -UseChoco          Use Chocolatey package manager
  -SkipVcpkg         Skip vcpkg installation (use system packages)
  -DryRun            Show what would be installed without installing
  -VcpkgRoot <path>  Path to vcpkg installation
  -InstallDir <path> Custom installation directory

Examples:
  .\install-deps-windows.ps1                    # Auto-detect and install
  .\install-deps-windows.ps1 -UseVcpkg          # Use vcpkg
  .\install-deps-windows.ps1 -Minimal           # Essential deps only
  .\install-deps-windows.ps1 -DryRun            # Show what would happen

Requirements:
  - Visual Studio 2019 or later with C++ workload
  - Git for Windows
  - PowerShell 5.1+ or PowerShell Core 7+
"@
}

# Check if running as administrator
function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]$identity
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Check if a command exists
function Test-Command {
    param([string]$Command)
    return $null -ne (Get-Command $Command -ErrorAction SilentlyContinue)
}

# Check Visual Studio installation
function Test-VisualStudio {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -property installationPath 2>$null
        if ($vsPath) {
            Write-Success "Visual Studio found at: $vsPath"
            return $true
        }
    }

    # Check for Build Tools
    if (Test-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools") {
        Write-Success "Visual Studio Build Tools 2022 found"
        return $true
    }
    if (Test-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\BuildTools") {
        Write-Success "Visual Studio Build Tools 2019 found"
        return $true
    }

    return $false
}

# Install Chocolatey if needed
function Install-Chocolatey {
    if (Test-Command "choco") {
        Write-Info "Chocolatey is already installed"
        return $true
    }

    Write-Info "Installing Chocolatey..."

    if (-not (Test-Administrator)) {
        Write-Err "Administrator privileges required to install Chocolatey"
        Write-Info "Please run this script as Administrator or install Chocolatey manually"
        return $false
    }

    Set-ExecutionPolicy Bypass -Scope Process -Force
    [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
    Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

    # Refresh environment
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")

    Write-Success "Chocolatey installed successfully"
    return $true
}

# Install vcpkg
function Install-Vcpkg {
    param([string]$InstallPath)

    if (-not $InstallPath) {
        $InstallPath = Join-Path $env:USERPROFILE "vcpkg"
    }

    if (Test-Path (Join-Path $InstallPath "vcpkg.exe")) {
        Write-Info "vcpkg found at: $InstallPath"
        return $InstallPath
    }

    Write-Info "Installing vcpkg to: $InstallPath"

    if (-not (Test-Command "git")) {
        Write-Err "Git is required to install vcpkg"
        Write-Info "Please install Git for Windows first"
        return $null
    }

    # Clone vcpkg
    git clone https://github.com/Microsoft/vcpkg.git $InstallPath

    # Bootstrap vcpkg
    Push-Location $InstallPath
    .\bootstrap-vcpkg.bat
    Pop-Location

    # Add to PATH
    $userPath = [System.Environment]::GetEnvironmentVariable("Path", "User")
    if ($userPath -notlike "*$InstallPath*") {
        [System.Environment]::SetEnvironmentVariable("Path", "$userPath;$InstallPath", "User")
        $env:Path += ";$InstallPath"
    }

    Write-Success "vcpkg installed successfully"
    return $InstallPath
}

# Install dependencies using vcpkg
function Install-VcpkgDependencies {
    param(
        [string]$VcpkgPath,
        [bool]$Minimal
    )

    $vcpkg = Join-Path $VcpkgPath "vcpkg.exe"

    if (-not (Test-Path $vcpkg)) {
        Write-Err "vcpkg not found at: $vcpkg"
        return $false
    }

    Write-Info "Installing dependencies via vcpkg..."

    # Essential packages
    $essential = @(
        "jsoncpp:x64-windows",
        "yaml-cpp:x64-windows",
        "openssl:x64-windows",
        "curl:x64-windows",
        "zlib:x64-windows",
        "gtest:x64-windows"
    )

    # MQTT packages
    $mqtt = @(
        "paho-mqtt:x64-windows",
        "paho-mqttpp3:x64-windows"
    )

    # Optional packages
    $optional = @(
        "zeromq:x64-windows",
        "czmq:x64-windows",
        "librdkafka:x64-windows",
        "libmodbus:x64-windows",
        "benchmark:x64-windows"
    )

    $packages = $essential
    if (-not $Minimal) {
        $packages += $mqtt + $optional
    }

    foreach ($pkg in $packages) {
        Write-Info "Installing $pkg..."
        & $vcpkg install $pkg
        if ($LASTEXITCODE -ne 0) {
            Write-Warn "Failed to install $pkg (may be optional)"
        }
    }

    Write-Success "vcpkg dependencies installed"
    return $true
}

# Install dependencies using Chocolatey
function Install-ChocoDependencies {
    param([bool]$Minimal)

    if (-not (Test-Administrator)) {
        Write-Err "Administrator privileges required to install via Chocolatey"
        return $false
    }

    Write-Info "Installing dependencies via Chocolatey..."

    # Build tools
    $buildTools = @(
        "cmake",
        "ninja",
        "git"
    )

    foreach ($tool in $buildTools) {
        if (-not (Test-Command $tool)) {
            Write-Info "Installing $tool..."
            choco install $tool -y
        } else {
            Write-Info "$tool is already installed"
        }
    }

    Write-Success "Chocolatey dependencies installed"
    Write-Info "Note: C++ libraries should be installed via vcpkg"
    return $true
}

# Install CMake
function Install-CMake {
    if (Test-Command "cmake") {
        $version = (cmake --version | Select-Object -First 1) -replace "cmake version ", ""
        Write-Info "CMake $version is already installed"
        return $true
    }

    Write-Info "Installing CMake..."

    $cmakeVersion = "3.28.1"
    $cmakeUrl = "https://github.com/Kitware/CMake/releases/download/v$cmakeVersion/cmake-$cmakeVersion-windows-x86_64.msi"
    $installer = Join-Path $env:TEMP "cmake-installer.msi"

    Invoke-WebRequest -Uri $cmakeUrl -OutFile $installer

    if (Test-Administrator) {
        Start-Process msiexec.exe -ArgumentList "/i", $installer, "/quiet", "/norestart" -Wait
    } else {
        Write-Info "Please install CMake manually or run as Administrator"
        Start-Process $installer
        return $false
    }

    Remove-Item $installer -Force

    # Refresh PATH
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")

    Write-Success "CMake installed"
    return $true
}

# Install Ninja
function Install-Ninja {
    if (Test-Command "ninja") {
        Write-Info "Ninja is already installed"
        return $true
    }

    Write-Info "Installing Ninja..."

    $ninjaVersion = "1.11.1"
    $ninjaUrl = "https://github.com/ninja-build/ninja/releases/download/v$ninjaVersion/ninja-win.zip"
    $ninjaZip = Join-Path $env:TEMP "ninja.zip"
    $ninjaDir = Join-Path $env:USERPROFILE ".local\bin"

    New-Item -ItemType Directory -Force -Path $ninjaDir | Out-Null

    Invoke-WebRequest -Uri $ninjaUrl -OutFile $ninjaZip
    Expand-Archive -Path $ninjaZip -DestinationPath $ninjaDir -Force
    Remove-Item $ninjaZip -Force

    # Add to PATH
    $userPath = [System.Environment]::GetEnvironmentVariable("Path", "User")
    if ($userPath -notlike "*$ninjaDir*") {
        [System.Environment]::SetEnvironmentVariable("Path", "$userPath;$ninjaDir", "User")
        $env:Path += ";$ninjaDir"
    }

    Write-Success "Ninja installed to $ninjaDir"
    return $true
}

# Verify installation
function Test-Installation {
    Write-Info "Verifying installation..."

    $status = $true

    # Check CMake
    if (Test-Command "cmake") {
        $version = (cmake --version | Select-Object -First 1) -replace "cmake version ", ""
        Write-Success "CMake version: $version"
    } else {
        Write-Err "CMake not found"
        $status = $false
    }

    # Check Ninja
    if (Test-Command "ninja") {
        $version = ninja --version
        Write-Success "Ninja version: $version"
    } else {
        Write-Warn "Ninja not found (will use MSBuild instead)"
    }

    # Check Git
    if (Test-Command "git") {
        $version = (git --version) -replace "git version ", ""
        Write-Success "Git version: $version"
    } else {
        Write-Warn "Git not found"
    }

    # Check Visual Studio
    if (-not (Test-VisualStudio)) {
        Write-Err "Visual Studio not found"
        Write-Info "Please install Visual Studio 2019+ with C++ workload"
        $status = $false
    }

    return $status
}

# Print build instructions
function Show-BuildInstructions {
    param([string]$VcpkgPath)

    $toolchainFile = ""
    if ($VcpkgPath) {
        $toolchainFile = "-DCMAKE_TOOLCHAIN_FILE=`"$VcpkgPath\scripts\buildsystems\vcpkg.cmake`""
    }

    @"

=== IPB Build Instructions ===

Open "Developer PowerShell for VS" or "x64 Native Tools Command Prompt" and run:

# Configure with CMake
cmake -B build -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_TESTING=ON `
    -DIPB_SINK_CONSOLE=ON `
    $toolchainFile

# Build the project
cmake --build build --config Release --parallel

# Run tests
cd build
ctest -C Release --output-on-failure

=== Alternative: Using Ninja ===

cmake -B build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_TESTING=ON `
    $toolchainFile

cmake --build build --parallel

"@
}

# Main function
function Main {
    if ($Help) {
        Show-Help
        return
    }

    Write-Host "=== IPB Dependencies Installation Script for Windows ===" -ForegroundColor Cyan
    Write-Host ""

    # Check prerequisites
    Write-Info "Checking prerequisites..."

    if (-not (Test-VisualStudio)) {
        Write-Err "Visual Studio with C++ workload is required"
        Write-Info "Please install from: https://visualstudio.microsoft.com/"
        Write-Info "Select 'Desktop development with C++' workload"
        return
    }

    if ($DryRun) {
        Write-Info "Dry run mode - no changes will be made"
        Write-Info "Would install: CMake, Ninja, vcpkg, and C++ libraries"
        return
    }

    # Install build tools
    Install-CMake | Out-Null
    Install-Ninja | Out-Null

    # Determine package manager
    $vcpkgPath = $VcpkgRoot

    if ($UseChoco -and -not $UseVcpkg) {
        if (Install-Chocolatey) {
            Install-ChocoDependencies -Minimal:$Minimal
        }
        Write-Info "For C++ libraries, vcpkg is recommended"
    }

    if (-not $SkipVcpkg) {
        if (-not $vcpkgPath) {
            $vcpkgPath = $env:VCPKG_ROOT
        }

        $vcpkgPath = Install-Vcpkg -InstallPath $vcpkgPath

        if ($vcpkgPath) {
            Install-VcpkgDependencies -VcpkgPath $vcpkgPath -Minimal:$Minimal

            # Set environment variable
            [System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgPath, "User")
            $env:VCPKG_ROOT = $vcpkgPath
        }
    }

    # Verify installation
    Write-Host ""
    if (Test-Installation) {
        Write-Success "All dependencies installed successfully!"
    } else {
        Write-Warn "Some dependencies may be missing"
    }

    # Show build instructions
    Show-BuildInstructions -VcpkgPath $vcpkgPath
}

# Run main
Main
