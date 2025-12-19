#!/bin/bash

# IPB Dependencies Installation Script for Linux
# Supports Ubuntu 20.04+, Debian 11+, CentOS 8+, Fedora 35+
#
# This script tries to install as much as possible without root,
# then prompts for root access only when needed.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
INSTALL_DIR="${IPB_INSTALL_DIR:-$HOME/.local}"
USE_SUDO=""
DISTRO=""
VERSION=""
PKG_MGR=""
TARGET_ARCH=""  # x64, x86, arm64 (empty = native)

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Print usage
print_usage() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS]

IPB Dependencies Installation Script for Linux

Options:
  -h, --help          Show this help message
  -l, --local         Install to user directory (~/.local) without root
  -s, --system        Install system-wide (requires root/sudo)
  -a, --auto          Auto-detect: try local first, then ask for root
  -m, --minimal       Install only essential dependencies
  -f, --full          Install all optional dependencies
  --arch ARCH         Target architecture: x64, x86, arm64 (default: native)
  --no-confirm        Skip confirmation prompts
  --dry-run           Show what would be installed without installing

Environment Variables:
  IPB_INSTALL_DIR     Custom installation directory (default: ~/.local)

Examples:
  $(basename "$0")                  # Auto-detect mode (native arch)
  $(basename "$0") --arch x86       # Install 32-bit libraries
  $(basename "$0") --local          # Install without root
  $(basename "$0") --system         # System-wide installation
  $(basename "$0") --minimal        # Essential deps only
EOF
}

# Detect Linux distribution
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO=$ID
        VERSION=$VERSION_ID
    else
        log_error "Cannot detect Linux distribution"
        exit 1
    fi

    log_info "Detected distribution: $DISTRO $VERSION"

    # Determine package manager
    if command -v apt-get &> /dev/null; then
        PKG_MGR="apt"
    elif command -v dnf &> /dev/null; then
        PKG_MGR="dnf"
    elif command -v yum &> /dev/null; then
        PKG_MGR="yum"
    elif command -v pacman &> /dev/null; then
        PKG_MGR="pacman"
    else
        log_warning "No supported package manager found"
        PKG_MGR=""
    fi
}

# Check if a command exists
command_exists() {
    command -v "$1" &> /dev/null
}

# Check if a library is available via pkg-config
lib_exists() {
    pkg-config --exists "$1" 2>/dev/null
}

# Try to get sudo access
request_sudo() {
    if [ "$USE_SUDO" = "never" ]; then
        return 1
    fi

    if [ "$(id -u)" -eq 0 ]; then
        USE_SUDO=""
        return 0
    fi

    if sudo -n true 2>/dev/null; then
        USE_SUDO="sudo"
        return 0
    fi

    echo ""
    log_info "Root access is required to install system packages."
    echo "You can:"
    echo "  1. Enter your password for sudo access"
    echo "  2. Press Ctrl+C to cancel and use --local mode instead"
    echo ""

    if sudo -v; then
        USE_SUDO="sudo"
        return 0
    else
        log_warning "Could not obtain root access"
        return 1
    fi
}

# Check what's already installed
check_existing_deps() {
    log_info "Checking existing dependencies..."

    MISSING_ESSENTIAL=()
    MISSING_OPTIONAL=()

    # Essential build tools
    command_exists cmake || MISSING_ESSENTIAL+=("cmake")
    command_exists ninja || command_exists ninja-build || MISSING_ESSENTIAL+=("ninja")
    command_exists pkg-config || MISSING_ESSENTIAL+=("pkg-config")
    command_exists g++ || command_exists clang++ || MISSING_ESSENTIAL+=("c++ compiler")

    # Essential libraries
    lib_exists jsoncpp || MISSING_ESSENTIAL+=("jsoncpp")
    lib_exists yaml-cpp || MISSING_ESSENTIAL+=("yaml-cpp")
    lib_exists openssl || MISSING_ESSENTIAL+=("openssl")
    lib_exists libcurl || MISSING_ESSENTIAL+=("curl")
    lib_exists gtest || MISSING_ESSENTIAL+=("gtest")

    # Optional libraries (transport/protocol specific)
    lib_exists paho-mqtt3as || MISSING_OPTIONAL+=("paho-mqtt-c")
    lib_exists paho-mqttpp3 || MISSING_OPTIONAL+=("paho-mqtt-cpp")
    lib_exists libzmq || MISSING_OPTIONAL+=("zeromq")
    lib_exists libmodbus || MISSING_OPTIONAL+=("libmodbus")
    lib_exists rdkafka || MISSING_OPTIONAL+=("librdkafka")

    if [ ${#MISSING_ESSENTIAL[@]} -eq 0 ]; then
        log_success "All essential dependencies are installed"
    else
        log_warning "Missing essential dependencies: ${MISSING_ESSENTIAL[*]}"
    fi

    if [ ${#MISSING_OPTIONAL[@]} -gt 0 ]; then
        log_info "Missing optional dependencies: ${MISSING_OPTIONAL[*]}"
    fi
}

# Install packages using system package manager
install_system_packages() {
    local packages=("$@")

    if [ -z "$PKG_MGR" ]; then
        log_warning "No package manager available for system installation"
        return 1
    fi

    case "$PKG_MGR" in
        apt)
            $USE_SUDO apt-get update
            $USE_SUDO apt-get install -y "${packages[@]}"
            ;;
        dnf|yum)
            $USE_SUDO $PKG_MGR install -y "${packages[@]}"
            ;;
        pacman)
            $USE_SUDO pacman -Sy --noconfirm "${packages[@]}"
            ;;
    esac
}

# Install dependencies for Ubuntu/Debian
install_ubuntu_debian() {
    log_info "Installing dependencies for Ubuntu/Debian (arch: $TARGET_ARCH)..."

    # Determine architecture suffix for packages
    local arch_suffix=""
    local cross_compile_pkgs=()
    local native_arch
    local is_cross_compile="false"
    native_arch=$(dpkg --print-architecture)

    case "$TARGET_ARCH" in
        x86)
            if [ "$native_arch" = "amd64" ]; then
                arch_suffix=":i386"
                is_cross_compile="true"
                cross_compile_pkgs=(gcc-multilib g++-multilib)
                log_info "Enabling i386 architecture for cross-compilation..."
                $USE_SUDO dpkg --add-architecture i386
                $USE_SUDO apt-get update
            fi
            ;;
        arm64)
            if [ "$native_arch" != "arm64" ]; then
                arch_suffix=":arm64"
                is_cross_compile="true"
                cross_compile_pkgs=(gcc-aarch64-linux-gnu g++-aarch64-linux-gnu)
                log_info "Enabling arm64 architecture for cross-compilation..."
                $USE_SUDO dpkg --add-architecture arm64
                $USE_SUDO apt-get update
            fi
            ;;
        x64)
            # Native on amd64, cross-compile on others
            if [ "$native_arch" != "amd64" ]; then
                arch_suffix=":amd64"
                is_cross_compile="true"
                log_info "Enabling amd64 architecture for cross-compilation..."
                $USE_SUDO dpkg --add-architecture amd64
                $USE_SUDO apt-get update
            fi
            ;;
    esac

    # Essential build tools (always native)
    local essential_pkgs=(
        build-essential
        cmake
        ninja-build
        pkg-config
        ccache
        git
    )

    # Add cross-compilation toolchain if needed
    if [ ${#cross_compile_pkgs[@]} -gt 0 ]; then
        essential_pkgs+=("${cross_compile_pkgs[@]}")
    fi

    # Essential libraries - only some have multiarch support
    # These packages typically have i386 versions
    local lib_pkgs=(
        "libjsoncpp-dev${arch_suffix}"
        "libssl-dev${arch_suffix}"
        "libcurl4-openssl-dev${arch_suffix}"
        "zlib1g-dev${arch_suffix}"
    )

    # These packages may not have i386/cross versions - install native only
    local lib_pkgs_native=(
        "libyaml-cpp-dev"
        "libgtest-dev"
        "libgmock-dev"
    )

    # Optional dependencies (native only for cross-compile, as most don't have multiarch)
    local optional_pkgs_native=(
        valgrind
        clang
        clang-tidy
        clang-format
        lcov
    )

    # MQTT and other optional libs - only install for native builds
    local optional_libs=()
    if [ "$is_cross_compile" = "false" ]; then
        optional_libs=(
            "libpaho-mqtt-dev"
            "libpaho-mqttpp-dev"
            "libzmq3-dev"
            "libczmq-dev"
            "librdkafka-dev"
            "libmodbus-dev"
        )
    else
        log_warning "Skipping optional libraries for cross-compilation (not available for $TARGET_ARCH)"
    fi

    # Install essential packages
    install_system_packages "${essential_pkgs[@]}"

    # Install libraries with architecture suffix (may fail for some)
    log_info "Installing architecture-specific libraries..."
    for pkg in "${lib_pkgs[@]}"; do
        $USE_SUDO apt-get install -y "$pkg" 2>/dev/null || \
            log_warning "Package $pkg not available, skipping..."
    done

    # Install native-only packages
    install_system_packages "${lib_pkgs_native[@]}"

    if [ "$INSTALL_MODE" != "minimal" ]; then
        # Install optional packages (native tools)
        install_system_packages "${optional_pkgs_native[@]}" || true

        # Install optional libraries if available
        for pkg in "${optional_libs[@]}"; do
            $USE_SUDO apt-get install -y "$pkg" 2>/dev/null || \
                log_warning "Optional package $pkg not available, skipping..."
        done
    fi

    log_success "Ubuntu/Debian dependencies installed successfully"
}

# Install dependencies for CentOS/RHEL/Fedora
install_redhat_fedora() {
    log_info "Installing dependencies for RedHat/Fedora..."

    # Enable EPEL for CentOS/RHEL
    if [[ "$DISTRO" == "centos" || "$DISTRO" == "rhel" ]]; then
        $USE_SUDO $PKG_MGR install -y epel-release
    fi

    local essential_pkgs=(
        gcc-c++
        cmake
        ninja-build
        pkgconfig
        ccache
        git
    )

    local lib_pkgs=(
        jsoncpp-devel
        yaml-cpp-devel
        openssl-devel
        libcurl-devel
        zlib-devel
        gtest-devel
        gmock-devel
    )

    local mqtt_pkgs=(
        paho-c-devel
        paho-cpp-devel
    )

    local optional_pkgs=(
        zeromq-devel
        czmq-devel
        librdkafka-devel
        libmodbus-devel
        valgrind
        clang
        clang-tools-extra
    )

    if [ "$INSTALL_MODE" = "minimal" ]; then
        install_system_packages "${essential_pkgs[@]}" "${lib_pkgs[@]}"
    else
        install_system_packages "${essential_pkgs[@]}" "${lib_pkgs[@]}" "${mqtt_pkgs[@]}" "${optional_pkgs[@]}"
    fi

    log_success "RedHat/Fedora dependencies installed successfully"
}

# Install to local directory without root
install_local() {
    log_info "Installing dependencies to $INSTALL_DIR (no root required)..."

    mkdir -p "$INSTALL_DIR"/{bin,lib,include,share}

    # Add to PATH if not already there
    if [[ ":$PATH:" != *":$INSTALL_DIR/bin:"* ]]; then
        log_info "Adding $INSTALL_DIR/bin to PATH"
        export PATH="$INSTALL_DIR/bin:$PATH"
        echo "export PATH=\"$INSTALL_DIR/bin:\$PATH\"" >> ~/.bashrc
        echo "export LD_LIBRARY_PATH=\"$INSTALL_DIR/lib:\$LD_LIBRARY_PATH\"" >> ~/.bashrc
        echo "export PKG_CONFIG_PATH=\"$INSTALL_DIR/lib/pkgconfig:\$PKG_CONFIG_PATH\"" >> ~/.bashrc
    fi

    # Install CMake if not present
    if ! command_exists cmake; then
        log_info "Installing CMake..."
        install_cmake_local
    fi

    # Install Ninja if not present
    if ! command_exists ninja; then
        log_info "Installing Ninja..."
        install_ninja_local
    fi

    log_warning "Some libraries may still need system installation."
    log_info "Consider using a package manager or building from source."

    log_success "Local installation completed"
}

# Install CMake locally
install_cmake_local() {
    local cmake_version="3.28.1"
    local cmake_url="https://github.com/Kitware/CMake/releases/download/v${cmake_version}/cmake-${cmake_version}-linux-x86_64.tar.gz"

    TEMP_DIR=$(mktemp -d)
    cd "$TEMP_DIR"

    curl -LO "$cmake_url"
    tar -xzf "cmake-${cmake_version}-linux-x86_64.tar.gz"
    cp -r "cmake-${cmake_version}-linux-x86_64"/* "$INSTALL_DIR/"

    cd /
    rm -rf "$TEMP_DIR"

    log_success "CMake ${cmake_version} installed to $INSTALL_DIR"
}

# Install Ninja locally
install_ninja_local() {
    local ninja_version="1.11.1"
    local ninja_url="https://github.com/ninja-build/ninja/releases/download/v${ninja_version}/ninja-linux.zip"

    TEMP_DIR=$(mktemp -d)
    cd "$TEMP_DIR"

    curl -LO "$ninja_url"
    unzip ninja-linux.zip
    cp ninja "$INSTALL_DIR/bin/"
    chmod +x "$INSTALL_DIR/bin/ninja"

    cd /
    rm -rf "$TEMP_DIR"

    log_success "Ninja ${ninja_version} installed to $INSTALL_DIR"
}

# Verify installation
verify_installation() {
    log_info "Verifying installation..."

    local status=0

    # Check CMake
    if command_exists cmake; then
        CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
        log_success "CMake version: $CMAKE_VERSION"
    else
        log_error "CMake not found"
        status=1
    fi

    # Check Ninja
    if command_exists ninja || command_exists ninja-build; then
        log_success "Ninja found"
    else
        log_warning "Ninja not found (will use Make instead)"
    fi

    # Check required libraries
    local required_libs=("jsoncpp" "yaml-cpp" "openssl")
    for lib in "${required_libs[@]}"; do
        if lib_exists "$lib"; then
            VERSION=$(pkg-config --modversion "$lib" 2>/dev/null || echo "unknown")
            log_success "$lib found (version: $VERSION)"
        else
            log_warning "$lib not found via pkg-config"
        fi
    done

    # Check MQTT libraries
    if [ -f /usr/lib/libpaho-mqtt3as.so ] || [ -f /usr/local/lib/libpaho-mqtt3as.so ] || \
       [ -f /usr/lib/x86_64-linux-gnu/libpaho-mqtt3as.so ]; then
        log_success "Paho MQTT C library found"
    else
        log_warning "Paho MQTT C library not found"
    fi

    log_success "Installation verification completed"
    return $status
}

# Print build instructions
print_build_instructions() {
    cat << EOF

${GREEN}=== IPB Build Instructions ===${NC}

Now you can build IPB with the following commands:

${BLUE}# Configure with CMake${NC}
cmake -B build -G Ninja \\
    -DCMAKE_BUILD_TYPE=Release \\
    -DBUILD_TESTING=ON \\
    -DIPB_SINK_CONSOLE=ON \\
    -DIPB_SINK_MQTT=ON

${BLUE}# Build the project${NC}
cmake --build build --parallel \$(nproc)

${BLUE}# Run tests${NC}
cd build && ctest --output-on-failure

${GREEN}=== Quick Build Script ===${NC}
You can also use: ./scripts/build.sh

EOF
}

# Main function
main() {
    local INSTALL_MODE="full"
    local AUTO_MODE="true"
    local NO_CONFIRM=""
    local DRY_RUN=""

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                print_usage
                exit 0
                ;;
            -l|--local)
                USE_SUDO="never"
                AUTO_MODE="false"
                shift
                ;;
            -s|--system)
                AUTO_MODE="false"
                shift
                ;;
            -a|--auto)
                AUTO_MODE="true"
                shift
                ;;
            -m|--minimal)
                INSTALL_MODE="minimal"
                shift
                ;;
            -f|--full)
                INSTALL_MODE="full"
                shift
                ;;
            --no-confirm)
                NO_CONFIRM="true"
                shift
                ;;
            --dry-run)
                DRY_RUN="true"
                shift
                ;;
            --arch)
                TARGET_ARCH="$2"
                if [[ ! "$TARGET_ARCH" =~ ^(x64|x86|arm64)$ ]]; then
                    log_error "Invalid architecture: $TARGET_ARCH (must be x64, x86, or arm64)"
                    exit 1
                fi
                shift 2
                ;;
            *)
                log_error "Unknown option: $1"
                print_usage
                exit 1
                ;;
        esac
    done

    # Set default architecture if not specified
    if [ -z "$TARGET_ARCH" ]; then
        case $(uname -m) in
            x86_64) TARGET_ARCH="x64" ;;
            i686|i386) TARGET_ARCH="x86" ;;
            aarch64|arm64) TARGET_ARCH="arm64" ;;
            *) TARGET_ARCH="x64" ;;
        esac
    fi

    echo "=== IPB Dependencies Installation Script for Linux ==="
    log_info "Target architecture: $TARGET_ARCH"
    echo ""

    detect_distro
    check_existing_deps

    # In auto mode, try to detect what's needed
    if [ "$AUTO_MODE" = "true" ]; then
        if [ ${#MISSING_ESSENTIAL[@]} -gt 0 ]; then
            log_info "Essential dependencies are missing. Requesting root access..."
            if ! request_sudo; then
                log_warning "Cannot install system packages without root."
                log_info "Installing what we can locally..."
                install_local
            fi
        fi
    elif [ "$USE_SUDO" != "never" ]; then
        request_sudo
    fi

    # Skip actual installation in dry-run mode
    if [ "$DRY_RUN" = "true" ]; then
        log_info "Dry run mode - no changes will be made"
        exit 0
    fi

    # Install based on distribution
    if [ "$USE_SUDO" != "never" ] && [ -n "$PKG_MGR" ]; then
        case "$DISTRO" in
            ubuntu|debian|pop|linuxmint)
                install_ubuntu_debian
                ;;
            centos|rhel|fedora|rocky|almalinux)
                install_redhat_fedora
                ;;
            arch|manjaro)
                log_info "Installing for Arch Linux..."
                $USE_SUDO pacman -Sy --noconfirm \
                    base-devel cmake ninja pkgconf ccache \
                    jsoncpp yaml-cpp openssl curl zlib gtest \
                    paho-mqtt-c zeromq czmq librdkafka libmodbus
                ;;
            *)
                log_warning "Unsupported distribution: $DISTRO"
                log_info "Attempting local installation..."
                install_local
                ;;
        esac
    else
        install_local
    fi

    verify_installation
    print_build_instructions

    log_success "IPB dependencies installation completed!"
}

# Run main function
main "$@"
