#!/bin/bash

# IPB Dependencies Installation Script for macOS 15.5+
# Supports macOS 15.5+ with Homebrew

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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

# Check macOS version
check_macos_version() {
    MACOS_VERSION=$(sw_vers -productVersion)
    MACOS_MAJOR=$(echo "$MACOS_VERSION" | cut -d. -f1)
    MACOS_MINOR=$(echo "$MACOS_VERSION" | cut -d. -f2)
    
    log_info "Detected macOS version: $MACOS_VERSION"
    
    if [[ $MACOS_MAJOR -lt 15 ]] || [[ $MACOS_MAJOR -eq 15 && $MACOS_MINOR -lt 5 ]]; then
        log_warning "macOS version $MACOS_VERSION detected. This script is optimized for macOS 15.5+"
        log_warning "Some packages may not be available or may require different versions"
    fi
}

# Check and install Xcode Command Line Tools
install_xcode_tools() {
    log_info "Checking Xcode Command Line Tools..."
    
    if ! xcode-select -p &> /dev/null; then
        log_info "Installing Xcode Command Line Tools..."
        xcode-select --install
        
        log_info "Please complete the Xcode Command Line Tools installation in the dialog"
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
        log_info "Installing Homebrew..."
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
        log_info "Updating Homebrew..."
        brew update
    fi
    
    # Verify Homebrew installation
    if ! command -v brew &> /dev/null; then
        log_error "Homebrew installation failed"
        exit 1
    fi
    
    BREW_VERSION=$(brew --version | head -n1)
    log_success "Homebrew installed: $BREW_VERSION"
}

# Install essential build tools
install_build_tools() {
    log_info "Installing essential build tools..."
    
    # Essential tools
    brew install \
        cmake \
        ninja \
        pkg-config \
        git \
        ccache
    
    # Install modern GCC (optional, for better C++20 support)
    brew install gcc
    
    log_success "Essential build tools installed"
}

# Install C++ libraries
install_cpp_libraries() {
    log_info "Installing C++ libraries..."
    
    # Core C++ libraries
    brew install \
        jsoncpp \
        yaml-cpp \
        fmt \
        spdlog \
        googletest
    
    log_success "C++ libraries installed"
}

# Install MQTT dependencies
install_mqtt_dependencies() {
    log_info "Installing MQTT dependencies..."
    
    # Paho MQTT C library
    brew install paho-mqtt-c
    
    # Paho MQTT C++ library
    brew install paho-mqtt-cpp
    
    log_success "MQTT dependencies installed"
}

# Install ZeroMQ dependencies
install_zeromq_dependencies() {
    log_info "Installing ZeroMQ dependencies..."
    
    brew install \
        zeromq \
        czmq
    
    log_success "ZeroMQ dependencies installed"
}

# Install Kafka dependencies
install_kafka_dependencies() {
    log_info "Installing Kafka dependencies..."
    
    brew install librdkafka
    
    log_success "Kafka dependencies installed"
}

# Install Modbus dependencies
install_modbus_dependencies() {
    log_info "Installing Modbus dependencies..."
    
    brew install libmodbus
    
    log_success "Modbus dependencies installed"
}

# Install OPC UA dependencies
install_opcua_dependencies() {
    log_info "Installing OPC UA dependencies..."
    
    # Try to install open62541 from Homebrew
    if brew list open62541 &> /dev/null || brew install open62541; then
        log_success "open62541 installed from Homebrew"
    else
        log_warning "open62541 not available in Homebrew. Installing from source..."
        install_open62541_from_source
    fi
}

# Install open62541 from source
install_open62541_from_source() {
    log_info "Installing open62541 from source..."
    
    TEMP_DIR=$(mktemp -d)
    cd "$TEMP_DIR"
    
    git clone https://github.com/open62541/open62541.git
    cd open62541
    git checkout v1.3.8  # Use stable version
    
    mkdir build && cd build
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DUA_ENABLE_AMALGAMATION=ON \
        -DUA_ENABLE_ENCRYPTION=ON \
        -DCMAKE_INSTALL_PREFIX=/usr/local
    
    make -j$(sysctl -n hw.ncpu)
    sudo make install
    
    cd /
    rm -rf "$TEMP_DIR"
    
    log_success "open62541 installed from source"
}

# Install additional development tools
install_additional_tools() {
    log_info "Installing additional development tools..."
    
    # Clang tools (usually included with Xcode, but ensure latest versions)
    brew install \
        llvm \
        clang-format
    
    # Valgrind alternative for macOS (AddressSanitizer is built into clang)
    log_info "Note: Valgrind is not available on macOS. Use AddressSanitizer instead:"
    log_info "  cmake .. -DCMAKE_CXX_FLAGS=\"-fsanitize=address -g\""
    
    # Optional: Install Docker for containerized builds
    if ! command -v docker &> /dev/null; then
        log_info "Docker not found. You may want to install Docker Desktop for macOS"
        log_info "Download from: https://www.docker.com/products/docker-desktop"
    fi
    
    log_success "Additional tools installed"
}

# Configure environment
configure_environment() {
    log_info "Configuring environment..."
    
    # Add LLVM to PATH (for latest clang tools)
    LLVM_PATH="/opt/homebrew/opt/llvm/bin"
    if [[ $(uname -m) == "x86_64" ]]; then
        LLVM_PATH="/usr/local/opt/llvm/bin"
    fi
    
    if [[ -d "$LLVM_PATH" ]]; then
        echo "export PATH=\"$LLVM_PATH:\$PATH\"" >> ~/.zprofile
        log_info "Added LLVM to PATH in ~/.zprofile"
    fi
    
    # Set up pkg-config paths
    PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig"
    if [[ $(uname -m) == "x86_64" ]]; then
        PKG_CONFIG_PATH="/usr/local/lib/pkgconfig"
    fi
    
    echo "export PKG_CONFIG_PATH=\"$PKG_CONFIG_PATH:\$PKG_CONFIG_PATH\"" >> ~/.zprofile
    
    # Set up CMake to find Homebrew packages
    CMAKE_PREFIX_PATH="/opt/homebrew"
    if [[ $(uname -m) == "x86_64" ]]; then
        CMAKE_PREFIX_PATH="/usr/local"
    fi
    
    echo "export CMAKE_PREFIX_PATH=\"$CMAKE_PREFIX_PATH:\$CMAKE_PREFIX_PATH\"" >> ~/.zprofile
    
    log_success "Environment configured"
}

# Verify installation
verify_installation() {
    log_info "Verifying installation..."
    
    # Check CMake version
    CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
    log_info "CMake version: $CMAKE_VERSION"
    
    # Check required libraries
    REQUIRED_LIBS=("jsoncpp" "yaml-cpp")
    
    for lib in "${REQUIRED_LIBS[@]}"; do
        if pkg-config --exists "$lib"; then
            VERSION=$(pkg-config --modversion "$lib")
            log_success "$lib found (version: $VERSION)"
        else
            log_warning "$lib not found via pkg-config"
        fi
    done
    
    # Check MQTT libraries
    if pkg-config --exists paho-mqtt3as; then
        VERSION=$(pkg-config --modversion paho-mqtt3as)
        log_success "Paho MQTT C library found (version: $VERSION)"
    else
        log_warning "Paho MQTT C library not found"
    fi
    
    if pkg-config --exists paho-mqttpp3; then
        VERSION=$(pkg-config --modversion paho-mqttpp3)
        log_success "Paho MQTT C++ library found (version: $VERSION)"
    else
        log_warning "Paho MQTT C++ library not found"
    fi
    
    # Check ZeroMQ
    if pkg-config --exists libzmq; then
        VERSION=$(pkg-config --modversion libzmq)
        log_success "ZeroMQ found (version: $VERSION)"
    else
        log_warning "ZeroMQ not found"
    fi
    
    log_success "Installation verification completed"
}

# Print build instructions
print_build_instructions() {
    cat << EOF

${GREEN}=== IPB Build Instructions for macOS ===${NC}

Now you can build IPB with the following commands:

${BLUE}# Extract the archive${NC}
tar -xzf ipb-monorepo-v*.tar.gz
cd ipb

${BLUE}# Create build directory${NC}
mkdir build && cd build

${BLUE}# Configure with CMake (all components)${NC}
cmake .. -DCMAKE_BUILD_TYPE=Release \\
         -DENABLE_CONSOLE_SINK=ON \\
         -DENABLE_SYSLOG_SINK=ON \\
         -DENABLE_MQTT_SINK=ON \\
         -DBUILD_EXAMPLES=ON \\
         -DBUILD_TESTING=ON \\
         -DCMAKE_PREFIX_PATH=\$(brew --prefix)

${BLUE}# Build the project${NC}
make -j\$(sysctl -n hw.ncpu)

${BLUE}# Install (optional)${NC}
sudo make install

${BLUE}# Run tests${NC}
ctest

${BLUE}# Run examples${NC}
./examples/mock_data_flow_test

${GREEN}=== Individual Component Build ===${NC}

You can also build individual components:

${BLUE}# Build only libipb-common${NC}
cd libipb-common && mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=\$(brew --prefix) && make

${BLUE}# Build only MQTT sink${NC}
cd libipb-sink-mqtt && mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=\$(brew --prefix) && make

${YELLOW}=== macOS Specific Notes ===${NC}

1. Restart your terminal or run: source ~/.zprofile
2. If you encounter linking issues, try:
   export LDFLAGS="-L\$(brew --prefix)/lib"
   export CPPFLAGS="-I\$(brew --prefix)/include"

3. For debugging, use AddressSanitizer instead of Valgrind:
   cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=address -g"

For more information, see the README.md file.

EOF
}

# Main installation function
main() {
    echo "=== IPB Dependencies Installation Script for macOS 15.5+ ==="
    echo "This script will install all required dependencies for building IPB"
    echo ""
    
    check_macos_version
    install_xcode_tools
    install_homebrew
    install_build_tools
    install_cpp_libraries
    install_mqtt_dependencies
    install_zeromq_dependencies
    install_kafka_dependencies
    install_modbus_dependencies
    install_opcua_dependencies
    install_additional_tools
    configure_environment
    verify_installation
    print_build_instructions
    
    log_success "IPB dependencies installation completed successfully!"
    log_info "Please restart your terminal or run 'source ~/.zprofile' to update your environment"
}

# Run main function
main "$@"

