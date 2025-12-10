#!/bin/bash

# IPB Dependencies Installation Script for Linux
# Supports Ubuntu 20.04+, Debian 11+, CentOS 8+, Fedora 35+

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
}

# Install dependencies for Ubuntu/Debian
install_ubuntu_debian() {
    log_info "Installing dependencies for Ubuntu/Debian..."
    
    # Update package list
    sudo apt update
    
    # Essential build tools
    sudo apt install -y \
        build-essential \
        cmake \
        git \
        pkg-config \
        ninja-build \
        ccache
    
    # C++ libraries
    sudo apt install -y \
        libjsoncpp-dev \
        libyaml-cpp-dev \
        libgtest-dev \
        libgmock-dev \
        libfmt-dev \
        libspdlog-dev
    
    # MQTT dependencies
    sudo apt install -y \
        libpaho-mqtt-dev \
        libpaho-mqttpp-dev
    
    # ZeroMQ dependencies
    sudo apt install -y \
        libzmq3-dev \
        libczmq-dev
    
    # Kafka dependencies (librdkafka)
    sudo apt install -y \
        librdkafka-dev
    
    # Modbus dependencies
    sudo apt install -y \
        libmodbus-dev
    
    # OPC UA dependencies (open62541)
    if ! pkg-config --exists open62541; then
        log_warning "open62541 not available in package manager. Installing from source..."
        install_open62541_from_source
    else
        sudo apt install -y libopen62541-dev
    fi
    
    # InfluxDB client (optional)
    if ! pkg-config --exists influxdb-cxx; then
        log_warning "InfluxDB C++ client not available in package manager"
        log_info "You may need to install it manually if using InfluxDB sink"
    fi
    
    log_success "Ubuntu/Debian dependencies installed successfully"
}

# Install dependencies for CentOS/RHEL/Fedora
install_redhat_fedora() {
    log_info "Installing dependencies for RedHat/Fedora..."
    
    # Determine package manager
    if command -v dnf &> /dev/null; then
        PKG_MGR="dnf"
    elif command -v yum &> /dev/null; then
        PKG_MGR="yum"
    else
        log_error "No suitable package manager found"
        exit 1
    fi
    
    # Enable EPEL repository for CentOS/RHEL
    if [[ "$DISTRO" == "centos" || "$DISTRO" == "rhel" ]]; then
        sudo $PKG_MGR install -y epel-release
    fi
    
    # Essential build tools
    sudo $PKG_MGR groupinstall -y "Development Tools"
    sudo $PKG_MGR install -y \
        cmake \
        git \
        pkgconfig \
        ninja-build \
        ccache
    
    # C++ libraries
    sudo $PKG_MGR install -y \
        jsoncpp-devel \
        yaml-cpp-devel \
        gtest-devel \
        gmock-devel \
        fmt-devel \
        spdlog-devel
    
    # MQTT dependencies
    sudo $PKG_MGR install -y \
        paho-c-devel \
        paho-cpp-devel
    
    # ZeroMQ dependencies
    sudo $PKG_MGR install -y \
        zeromq-devel \
        czmq-devel
    
    # Kafka dependencies
    sudo $PKG_MGR install -y \
        librdkafka-devel
    
    # Modbus dependencies
    sudo $PKG_MGR install -y \
        libmodbus-devel
    
    # OPC UA dependencies
    log_warning "open62541 may need to be installed from source on RedHat/Fedora"
    
    log_success "RedHat/Fedora dependencies installed successfully"
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
        -DUA_ENABLE_ENCRYPTION=ON
    
    make -j$(nproc)
    sudo make install
    sudo ldconfig
    
    cd /
    rm -rf "$TEMP_DIR"
    
    log_success "open62541 installed from source"
}

# Install additional tools
install_additional_tools() {
    log_info "Installing additional development tools..."
    
    # Valgrind for memory debugging
    if [[ "$DISTRO" == "ubuntu" || "$DISTRO" == "debian" ]]; then
        sudo apt install -y valgrind
    elif [[ "$DISTRO" == "centos" || "$DISTRO" == "rhel" || "$DISTRO" == "fedora" ]]; then
        sudo $PKG_MGR install -y valgrind
    fi
    
    # Clang tools (optional)
    if [[ "$DISTRO" == "ubuntu" || "$DISTRO" == "debian" ]]; then
        sudo apt install -y clang clang-tools clang-tidy clang-format
    elif [[ "$DISTRO" == "fedora" ]]; then
        sudo dnf install -y clang clang-tools-extra
    fi
    
    log_success "Additional tools installed"
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
    if [ -f /usr/lib/libpaho-mqtt3as.so ] || [ -f /usr/local/lib/libpaho-mqtt3as.so ]; then
        log_success "Paho MQTT C library found"
    else
        log_warning "Paho MQTT C library not found"
    fi
    
    if [ -f /usr/lib/libpaho-mqttpp3.so ] || [ -f /usr/local/lib/libpaho-mqttpp3.so ]; then
        log_success "Paho MQTT C++ library found"
    else
        log_warning "Paho MQTT C++ library not found"
    fi
    
    log_success "Installation verification completed"
}

# Print build instructions
print_build_instructions() {
    cat << EOF

${GREEN}=== IPB Build Instructions ===${NC}

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
         -DBUILD_TESTING=ON

${BLUE}# Build the project${NC}
make -j\$(nproc)

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
cmake .. && make

${BLUE}# Build only MQTT sink${NC}
cd libipb-sink-mqtt && mkdir build && cd build
cmake .. && make

For more information, see the README.md file.

EOF
}

# Main installation function
main() {
    echo "=== IPB Dependencies Installation Script for Linux ==="
    echo "This script will install all required dependencies for building IPB"
    echo ""
    
    # Check if running as root
    if [[ $EUID -eq 0 ]]; then
        log_error "This script should not be run as root"
        log_info "Please run as a regular user with sudo privileges"
        exit 1
    fi
    
    # Check sudo access
    if ! sudo -n true 2>/dev/null; then
        log_info "This script requires sudo privileges"
        sudo -v
    fi
    
    detect_distro
    
    case "$DISTRO" in
        ubuntu|debian)
            install_ubuntu_debian
            ;;
        centos|rhel|fedora)
            install_redhat_fedora
            ;;
        *)
            log_error "Unsupported distribution: $DISTRO"
            log_info "Supported distributions: Ubuntu, Debian, CentOS, RHEL, Fedora"
            exit 1
            ;;
    esac
    
    install_additional_tools
    verify_installation
    print_build_instructions
    
    log_success "IPB dependencies installation completed successfully!"
}

# Run main function
main "$@"

