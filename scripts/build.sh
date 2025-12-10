#!/bin/bash

# IPB Build Script
# Supports individual component builds and full project builds

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

# Default values
BUILD_TYPE="Release"
BUILD_DIR="build"
CLEAN_BUILD=false
INSTALL=false
RUN_TESTS=false
VERBOSE=false
PARALLEL_JOBS=""
COMPONENT=""

# Available components
AVAILABLE_COMPONENTS=(
    "common"
    "router" 
    "console-sink"
    "syslog-sink"
    "mqtt-sink"
    "gate"
    "examples"
    "tests"
)

# Platform detection
detect_platform() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        PLATFORM="macos"
        NPROC_CMD="sysctl -n hw.ncpu"
    else
        PLATFORM="linux"
        NPROC_CMD="nproc"
    fi
    
    if [[ -z "$PARALLEL_JOBS" ]]; then
        PARALLEL_JOBS=$($NPROC_CMD)
    fi
}

# Print usage
print_usage() {
    cat << EOF
IPB Build Script

Usage: $0 [OPTIONS] [COMPONENT]

OPTIONS:
    -t, --type TYPE         Build type: Debug, Release, RelWithDebInfo (default: Release)
    -d, --build-dir DIR     Build directory (default: build)
    -c, --clean             Clean build (remove build directory first)
    -i, --install           Install after build
    -T, --test              Run tests after build
    -v, --verbose           Verbose output
    -j, --jobs N            Number of parallel jobs (default: auto-detect)
    -h, --help              Show this help

COMPONENT:
    If specified, build only the given component. Available components:
$(printf "    %s\n" "${AVAILABLE_COMPONENTS[@]}")
    
    If no component is specified, builds the entire project.

EXAMPLES:
    $0                              # Build entire project in Release mode
    $0 -t Debug -c                  # Clean Debug build
    $0 common                       # Build only libipb-common
    $0 mqtt-sink -t Debug -v        # Build MQTT sink in Debug mode with verbose output
    $0 -i -T                        # Build, install, and test
    $0 examples -j 4                # Build examples with 4 parallel jobs

INDIVIDUAL COMPONENT BUILDS:
    You can also build components individually by going to their directories:
    
    cd libipb-common && mkdir build && cd build
    cmake .. && make
    
    cd libipb-sink-mqtt && mkdir build && cd build  
    cmake .. && make

EOF
}

# Parse command line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -t|--type)
                BUILD_TYPE="$2"
                shift 2
                ;;
            -d|--build-dir)
                BUILD_DIR="$2"
                shift 2
                ;;
            -c|--clean)
                CLEAN_BUILD=true
                shift
                ;;
            -i|--install)
                INSTALL=true
                shift
                ;;
            -T|--test)
                RUN_TESTS=true
                shift
                ;;
            -v|--verbose)
                VERBOSE=true
                shift
                ;;
            -j|--jobs)
                PARALLEL_JOBS="$2"
                shift 2
                ;;
            -h|--help)
                print_usage
                exit 0
                ;;
            -*)
                log_error "Unknown option: $1"
                print_usage
                exit 1
                ;;
            *)
                if [[ -z "$COMPONENT" ]]; then
                    COMPONENT="$1"
                else
                    log_error "Multiple components specified"
                    exit 1
                fi
                shift
                ;;
        esac
    done
}

# Validate component
validate_component() {
    if [[ -n "$COMPONENT" ]]; then
        local valid=false
        for comp in "${AVAILABLE_COMPONENTS[@]}"; do
            if [[ "$comp" == "$COMPONENT" ]]; then
                valid=true
                break
            fi
        done
        
        if [[ "$valid" == false ]]; then
            log_error "Invalid component: $COMPONENT"
            log_info "Available components: ${AVAILABLE_COMPONENTS[*]}"
            exit 1
        fi
    fi
}

# Get CMake options for component
get_cmake_options() {
    local cmake_opts="-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    
    # Platform-specific options
    if [[ "$PLATFORM" == "macos" ]]; then
        if command -v brew &> /dev/null; then
            cmake_opts="$cmake_opts -DCMAKE_PREFIX_PATH=$(brew --prefix)"
        fi
    fi
    
    if [[ -n "$COMPONENT" ]]; then
        # Component-specific builds
        case "$COMPONENT" in
            "common")
                cmake_opts="$cmake_opts -DBUILD_ALL_COMPONENTS=OFF -DBUILD_COMMON=ON"
                ;;
            "router")
                cmake_opts="$cmake_opts -DBUILD_ALL_COMPONENTS=OFF -DBUILD_ROUTER=ON"
                ;;
            "console-sink")
                cmake_opts="$cmake_opts -DBUILD_ALL_COMPONENTS=OFF -DENABLE_CONSOLE_SINK=ON"
                ;;
            "syslog-sink")
                cmake_opts="$cmake_opts -DBUILD_ALL_COMPONENTS=OFF -DENABLE_SYSLOG_SINK=ON"
                ;;
            "mqtt-sink")
                cmake_opts="$cmake_opts -DBUILD_ALL_COMPONENTS=OFF -DENABLE_MQTT_SINK=ON"
                ;;
            "gate")
                cmake_opts="$cmake_opts -DBUILD_ALL_COMPONENTS=OFF -DBUILD_GATE=ON"
                ;;
            "examples")
                cmake_opts="$cmake_opts -DBUILD_EXAMPLES=ON"
                ;;
            "tests")
                cmake_opts="$cmake_opts -DBUILD_TESTING=ON"
                ;;
        esac
    else
        # Full build
        cmake_opts="$cmake_opts -DBUILD_ALL_COMPONENTS=ON"
        cmake_opts="$cmake_opts -DENABLE_CONSOLE_SINK=ON"
        cmake_opts="$cmake_opts -DENABLE_SYSLOG_SINK=ON"
        cmake_opts="$cmake_opts -DENABLE_MQTT_SINK=ON"
        cmake_opts="$cmake_opts -DBUILD_EXAMPLES=ON"
        
        if [[ "$RUN_TESTS" == true ]]; then
            cmake_opts="$cmake_opts -DBUILD_TESTING=ON"
        fi
    fi
    
    echo "$cmake_opts"
}

# Build individual component
build_component() {
    local component="$1"
    local component_dir=""
    
    case "$component" in
        "common")
            component_dir="libipb-common"
            ;;
        "router")
            component_dir="libipb-router"
            ;;
        "console-sink")
            component_dir="libipb-sink-console"
            ;;
        "syslog-sink")
            component_dir="libipb-sink-syslog"
            ;;
        "mqtt-sink")
            component_dir="libipb-sink-mqtt"
            ;;
        "gate")
            component_dir="ipb-gate"
            ;;
        *)
            log_error "Cannot build component $component individually"
            log_info "Use full project build for: examples, tests"
            return 1
            ;;
    esac
    
    if [[ ! -d "$component_dir" ]]; then
        log_error "Component directory not found: $component_dir"
        return 1
    fi
    
    log_info "Building component: $component ($component_dir)"
    
    cd "$component_dir"
    
    if [[ "$CLEAN_BUILD" == true ]] && [[ -d "$BUILD_DIR" ]]; then
        log_info "Cleaning build directory: $BUILD_DIR"
        rm -rf "$BUILD_DIR"
    fi
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    local cmake_opts=$(get_cmake_options)
    
    log_info "Configuring with CMake..."
    if [[ "$VERBOSE" == true ]]; then
        cmake .. $cmake_opts
    else
        cmake .. $cmake_opts > /dev/null
    fi
    
    log_info "Building with $PARALLEL_JOBS parallel jobs..."
    if [[ "$VERBOSE" == true ]]; then
        make -j"$PARALLEL_JOBS"
    else
        make -j"$PARALLEL_JOBS" > /dev/null
    fi
    
    if [[ "$INSTALL" == true ]]; then
        log_info "Installing component..."
        sudo make install
    fi
    
    cd ../..
    log_success "Component $component built successfully"
}

# Build full project
build_full_project() {
    log_info "Building full IPB project"
    
    if [[ "$CLEAN_BUILD" == true ]] && [[ -d "$BUILD_DIR" ]]; then
        log_info "Cleaning build directory: $BUILD_DIR"
        rm -rf "$BUILD_DIR"
    fi
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    local cmake_opts=$(get_cmake_options)
    
    log_info "Configuring with CMake..."
    if [[ "$VERBOSE" == true ]]; then
        cmake .. $cmake_opts
    else
        cmake .. $cmake_opts > /dev/null
    fi
    
    log_info "Building with $PARALLEL_JOBS parallel jobs..."
    if [[ "$VERBOSE" == true ]]; then
        make -j"$PARALLEL_JOBS"
    else
        make -j"$PARALLEL_JOBS" > /dev/null
    fi
    
    if [[ "$INSTALL" == true ]]; then
        log_info "Installing..."
        sudo make install
    fi
    
    if [[ "$RUN_TESTS" == true ]]; then
        log_info "Running tests..."
        ctest --output-on-failure
    fi
    
    cd ..
    log_success "Full project built successfully"
}

# Main function
main() {
    echo "=== IPB Build Script ==="
    
    detect_platform
    parse_args "$@"
    validate_component
    
    log_info "Platform: $PLATFORM"
    log_info "Build type: $BUILD_TYPE"
    log_info "Build directory: $BUILD_DIR"
    log_info "Parallel jobs: $PARALLEL_JOBS"
    
    if [[ -n "$COMPONENT" ]]; then
        log_info "Component: $COMPONENT"
        build_component "$COMPONENT"
    else
        log_info "Building all components"
        build_full_project
    fi
    
    log_success "Build completed successfully!"
    
    # Print next steps
    echo ""
    echo "=== Next Steps ==="
    if [[ -n "$COMPONENT" ]]; then
        echo "Component built in: $COMPONENT/$BUILD_DIR"
    else
        echo "Project built in: $BUILD_DIR"
        if [[ -f "$BUILD_DIR/examples/mock_data_flow_test" ]]; then
            echo "Run example: ./$BUILD_DIR/examples/mock_data_flow_test"
        fi
        if [[ -f "$BUILD_DIR/ipb-gate/ipb-gate" ]]; then
            echo "Run IPB Gate: ./$BUILD_DIR/ipb-gate/ipb-gate --config examples/gateway-config.yaml"
        fi
    fi
}

# Run main function
main "$@"

