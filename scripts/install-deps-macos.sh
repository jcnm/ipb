#!/bin/bash

# IPB Dependencies Installation Script for macOS
# Supports macOS 12+ (Monterey and later) with Homebrew
#
# This script uses Homebrew to install dependencies without requiring root.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
INSTALL_MODE="full"
DRY_RUN=""
SKIP_BREW_UPDATE=""
TARGET_ARCH=""  # x86_64, arm64 (empty = native)

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

IPB Dependencies Installation Script for macOS

Options:
  -h, --help              Show this help message
  -m, --minimal           Install only essential dependencies
  -f, --full              Install all optional dependencies (default)
  --arch ARCH             Target architecture: x86_64 or arm64 (default: native)
  --skip-brew-update      Skip Homebrew update (faster)
  --dry-run               Show what would be installed without installing

Examples:
  $(basename "$0")                     # Full installation (native arch)
  $(basename "$0") --arch x86_64       # Install for Intel Macs
  $(basename "$0") --arch arm64        # Install for Apple Silicon
  $(basename "$0") --minimal           # Essential deps only
  $(basename "$0") --dry-run           # Preview changes

Requirements:
  - macOS 12+ (Monterey or later)
  - Xcode Command Line Tools
  - Homebrew (will be installed if missing)
EOF
}

# Check macOS version
check_macos_version() {
    MACOS_VERSION=$(sw_vers -productVersion)
    MACOS_MAJOR=$(echo "$MACOS_VERSION" | cut -d. -f1)

    log_info "Detected macOS version: $MACOS_VERSION"

    if [[ $MACOS_MAJOR -lt 12 ]]; then
        log_warning "macOS version $MACOS_VERSION may have compatibility issues"
        log_warning "This script is optimized for macOS 12+ (Monterey and later)"
    fi
}

# Check and install Xcode Command Line Tools
install_xcode_tools() {
    log_info "Checking Xcode Command Line Tools..."

    if ! xcode-select -p &> /dev/null; then
        log_info "Installing Xcode Command Line Tools..."
        xcode-select --install

        log_info "Please complete the installation in the dialog that appeared"
        log_info "Press any key to continue after installation is complete..."
        read -n 1 -s
    else
        log_success "Xcode Command Line Tools already installed"
    fi

    # Verify installation
    if ! xcode-select -p &> /dev/null; then
        log_error "Xcode Command Line Tools installation failed"
        exit 1
    fi
}

# Check and install Homebrew
install_homebrew() {
    log_info "Checking Homebrew installation..."

    if ! command -v brew &> /dev/null; then
        log_info "Installing Homebrew (no root required)..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

        # Add Homebrew to PATH for Apple Silicon Macs
        if [[ $(uname -m) == "arm64" ]]; then
            echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
            eval "$(/opt/homebrew/bin/brew shellenv)"
        else
            echo 'eval "$(/usr/local/bin/brew shellenv)"' >> ~/.zprofile
            eval "$(/usr/local/bin/brew shellenv)"
        fi
    else
        log_success "Homebrew already installed"
        if [[ -z "$SKIP_BREW_UPDATE" ]]; then
            log_info "Updating Homebrew..."
            brew update
        fi
    fi

    # Verify Homebrew installation
    if ! command -v brew &> /dev/null; then
        log_error "Homebrew installation failed"
        exit 1
    fi

    BREW_VERSION=$(brew --version | head -n1)
    log_success "Homebrew installed: $BREW_VERSION"
}

# Check what's already installed
check_existing_deps() {
    log_info "Checking existing dependencies..."

    MISSING_ESSENTIAL=()
    MISSING_OPTIONAL=()

    # Check build tools
    command -v cmake &> /dev/null || MISSING_ESSENTIAL+=("cmake")
    command -v ninja &> /dev/null || MISSING_ESSENTIAL+=("ninja")
    command -v pkg-config &> /dev/null || MISSING_ESSENTIAL+=("pkg-config")

    # Check libraries via pkg-config
    pkg-config --exists jsoncpp 2>/dev/null || MISSING_ESSENTIAL+=("jsoncpp")
    pkg-config --exists yaml-cpp 2>/dev/null || MISSING_ESSENTIAL+=("yaml-cpp")
    pkg-config --exists openssl 2>/dev/null || MISSING_ESSENTIAL+=("openssl")
    pkg-config --exists gtest 2>/dev/null || MISSING_ESSENTIAL+=("googletest")

    # Check optional libraries (MQTT)
    pkg-config --exists paho-mqtt3as 2>/dev/null || MISSING_OPTIONAL+=("libpaho-mqtt")
    pkg-config --exists paho-mqttpp3 2>/dev/null || MISSING_OPTIONAL+=("paho-mqtt-cpp (build from source)")
    pkg-config --exists libzmq 2>/dev/null || MISSING_OPTIONAL+=("zeromq")

    if [ ${#MISSING_ESSENTIAL[@]} -eq 0 ]; then
        log_success "All essential dependencies are installed"
    else
        log_info "Missing essential dependencies: ${MISSING_ESSENTIAL[*]}"
    fi

    if [ ${#MISSING_OPTIONAL[@]} -gt 0 ]; then
        log_info "Missing optional dependencies: ${MISSING_OPTIONAL[*]}"
    fi
}

# Install essential build tools
install_build_tools() {
    log_info "Installing essential build tools..."

    local tools=(
        cmake
        ninja
        pkg-config
        ccache
        git
    )

    for tool in "${tools[@]}"; do
        if ! command -v "$tool" &> /dev/null; then
            log_info "Installing $tool..."
            brew install "$tool"
        else
            log_info "$tool already installed"
        fi
    done

    log_success "Essential build tools installed"
}

# Install C++ libraries
install_cpp_libraries() {
    log_info "Installing C++ libraries..."

    local libs=(
        jsoncpp
        yaml-cpp
        openssl@3
        curl
        zlib
        zstd
        lz4
        xxhash
        googletest
    )

    for lib in "${libs[@]}"; do
        if ! brew list "$lib" &> /dev/null; then
            log_info "Installing $lib..."
            brew install "$lib"
        else
            log_info "$lib already installed"
        fi
    done

    log_success "C++ libraries installed"
}

# Install MQTT dependencies
install_mqtt_dependencies() {
    log_info "Installing MQTT dependencies..."

    # Paho MQTT C library (correct Homebrew formula name)
    if ! brew list libpaho-mqtt &> /dev/null; then
        log_info "Installing libpaho-mqtt (C library)..."
        brew install libpaho-mqtt
    else
        log_info "libpaho-mqtt already installed"
    fi

    # Paho MQTT C++ library - must be built from source (no Homebrew formula)
    # Check if already installed
    if pkg-config --exists paho-mqttpp3 2>/dev/null; then
        log_info "paho-mqtt-cpp already installed"
    else
        log_info "Building paho-mqtt-cpp from source (no Homebrew formula available)..."
        build_paho_mqtt_cpp
    fi

    log_success "MQTT dependencies installed"
}

# Build Paho MQTT C++ from source
build_paho_mqtt_cpp() {
    local build_dir
    build_dir=$(mktemp -d)
    local paho_cpp_version="1.4.1"

    log_info "Cloning paho.mqtt.cpp v${paho_cpp_version}..."
    git clone --depth 1 --branch "v${paho_cpp_version}" \
        https://github.com/eclipse/paho.mqtt.cpp.git "$build_dir/paho.mqtt.cpp"

    cd "$build_dir/paho.mqtt.cpp"

    local brew_prefix
    if [[ $(uname -m) == "arm64" ]]; then
        brew_prefix="/opt/homebrew"
    else
        brew_prefix="/usr/local"
    fi

    log_info "Configuring paho.mqtt.cpp..."
    cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$brew_prefix" \
        -DPAHO_BUILD_SHARED=ON \
        -DPAHO_BUILD_STATIC=OFF \
        -DPAHO_WITH_SSL=ON \
        -DCMAKE_PREFIX_PATH="$brew_prefix"

    log_info "Building paho.mqtt.cpp..."
    cmake --build build --parallel "$(sysctl -n hw.ncpu)"

    log_info "Installing paho.mqtt.cpp (requires sudo)..."
    sudo cmake --install build

    cd /
    rm -rf "$build_dir"

    log_success "paho.mqtt.cpp built and installed successfully"
}

# Install optional dependencies
install_optional_dependencies() {
    log_info "Installing optional dependencies..."

    local optional_libs=(
        zeromq
        czmq
        librdkafka
        libmodbus
        fmt
        spdlog
        llvm
    )

    for lib in "${optional_libs[@]}"; do
        if ! brew list "$lib" &> /dev/null; then
            log_info "Installing $lib..."
            brew install "$lib" || log_warning "Failed to install $lib (may be optional)"
        else
            log_info "$lib already installed"
        fi
    done

    log_success "Optional dependencies installed"
}

# Configure environment
configure_environment() {
    log_info "Configuring environment..."

    local brew_prefix
    if [[ $(uname -m) == "arm64" ]]; then
        brew_prefix="/opt/homebrew"
    else
        brew_prefix="/usr/local"
    fi

    # Create or update shell profile
    local profile="$HOME/.zprofile"
    local marker="# IPB Development Environment"

    if ! grep -q "$marker" "$profile" 2>/dev/null; then
        cat >> "$profile" << EOF

$marker
export PATH="$brew_prefix/opt/llvm/bin:\$PATH"
export LDFLAGS="-L$brew_prefix/lib"
export CPPFLAGS="-I$brew_prefix/include"
export PKG_CONFIG_PATH="$brew_prefix/lib/pkgconfig:\$PKG_CONFIG_PATH"
export CMAKE_PREFIX_PATH="$brew_prefix:\$CMAKE_PREFIX_PATH"
EOF
        log_info "Added environment configuration to $profile"
    fi

    log_success "Environment configured"
}

# Verify installation
verify_installation() {
    log_info "Verifying installation..."

    local status=0

    # Check CMake version
    if command -v cmake &> /dev/null; then
        CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
        log_success "CMake version: $CMAKE_VERSION"
    else
        log_error "CMake not found"
        status=1
    fi

    # Check Ninja
    if command -v ninja &> /dev/null; then
        log_success "Ninja found"
    else
        log_warning "Ninja not found"
    fi

    # Check required libraries
    local required_libs=("jsoncpp" "yaml-cpp" "openssl")
    for lib in "${required_libs[@]}"; do
        if pkg-config --exists "$lib" 2>/dev/null; then
            VERSION=$(pkg-config --modversion "$lib" 2>/dev/null || echo "unknown")
            log_success "$lib found (version: $VERSION)"
        else
            log_warning "$lib not found via pkg-config"
        fi
    done

    # Check MQTT libraries
    if pkg-config --exists paho-mqtt3as 2>/dev/null; then
        VERSION=$(pkg-config --modversion paho-mqtt3as)
        log_success "Paho MQTT C library found (version: $VERSION)"
    else
        log_warning "Paho MQTT C library not found"
    fi

    if pkg-config --exists paho-mqttpp3 2>/dev/null; then
        VERSION=$(pkg-config --modversion paho-mqttpp3)
        log_success "Paho MQTT C++ library found (version: $VERSION)"
    else
        log_warning "Paho MQTT C++ library not found"
    fi

    log_success "Installation verification completed"
    return $status
}

# Print build instructions
print_build_instructions() {
    local brew_prefix
    if [[ $(uname -m) == "arm64" ]]; then
        brew_prefix="/opt/homebrew"
    else
        brew_prefix="/usr/local"
    fi

    cat << EOF

${GREEN}=== IPB Build Instructions for macOS ===${NC}

${BLUE}# Configure with CMake${NC}
cmake -B build -G Ninja \\
    -DCMAKE_BUILD_TYPE=Release \\
    -DBUILD_TESTING=ON \\
    -DIPB_SINK_CONSOLE=ON \\
    -DIPB_SINK_MQTT=ON \\
    -DCMAKE_PREFIX_PATH=$brew_prefix

${BLUE}# Build the project${NC}
cmake --build build --parallel \$(sysctl -n hw.ncpu)

${BLUE}# Run tests${NC}
cd build && ctest --output-on-failure

${YELLOW}=== macOS Notes ===${NC}

1. Restart your terminal or run: source ~/.zprofile
2. For debugging, use AddressSanitizer:
   cmake -B build -DCMAKE_CXX_FLAGS="-fsanitize=address -g"

${GREEN}=== Quick Build Script ===${NC}
You can also use: ./scripts/build.sh

EOF
}

# Main function
main() {
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                print_usage
                exit 0
                ;;
            -m|--minimal)
                INSTALL_MODE="minimal"
                shift
                ;;
            -f|--full)
                INSTALL_MODE="full"
                shift
                ;;
            --skip-brew-update)
                SKIP_BREW_UPDATE="true"
                shift
                ;;
            --dry-run)
                DRY_RUN="true"
                shift
                ;;
            --arch)
                TARGET_ARCH="$2"
                if [[ ! "$TARGET_ARCH" =~ ^(x86_64|arm64)$ ]]; then
                    log_error "Invalid architecture: $TARGET_ARCH (must be x86_64 or arm64)"
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
        TARGET_ARCH=$(uname -m)  # x86_64 or arm64
    fi

    # Check for cross-compilation
    local native_arch
    native_arch=$(uname -m)
    if [[ "$native_arch" != "$TARGET_ARCH" ]]; then
        log_warning "Cross-compilation detected: native=$native_arch, target=$TARGET_ARCH"
        log_warning "Homebrew packages are architecture-specific."
        if [[ "$native_arch" == "arm64" && "$TARGET_ARCH" == "x86_64" ]]; then
            log_info "You can use Rosetta 2 to run x86_64 binaries on Apple Silicon."
        fi
    fi

    echo "=== IPB Dependencies Installation Script for macOS ==="
    log_info "Target architecture: $TARGET_ARCH"
    echo ""

    check_macos_version
    check_existing_deps

    if [ "$DRY_RUN" = "true" ]; then
        log_info "Dry run mode - no changes will be made"
        log_info "Would install: Xcode CLI Tools, Homebrew, CMake, Ninja, and C++ libraries"
        exit 0
    fi

    install_xcode_tools
    install_homebrew
    install_build_tools
    install_cpp_libraries
    install_mqtt_dependencies

    if [ "$INSTALL_MODE" = "full" ]; then
        install_optional_dependencies
    fi

    configure_environment
    verify_installation
    print_build_instructions

    log_success "IPB dependencies installation completed!"
    log_info "Please restart your terminal or run 'source ~/.zprofile'"
}

# Run main function
main "$@"
