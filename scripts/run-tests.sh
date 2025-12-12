#!/bin/bash
# =============================================================================
# IPB Test Runner with Coverage Report
# =============================================================================
# This script runs all unit tests and generates comprehensive test and coverage
# reports in multiple formats (console, HTML, XML/JUnit, Cobertura).
#
# Usage:
#   ./scripts/run-tests.sh [OPTIONS]
#
# Options:
#   --build-dir DIR    Build directory (default: build)
#   --output-dir DIR   Output directory for reports (default: test-reports)
#   --no-coverage      Skip coverage generation
#   --html             Generate HTML reports
#   --xml              Generate XML/JUnit reports (for CI)
#   --verbose          Verbose output
#   --filter PATTERN   Run only tests matching pattern
#   --help             Show this help
# =============================================================================

set -e

# Default values
BUILD_DIR="build"
OUTPUT_DIR="test-reports"
GENERATE_COVERAGE=true
GENERATE_HTML=false
GENERATE_XML=false
VERBOSE=false
TEST_FILTER=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color
BOLD='\033[1m'

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --no-coverage)
            GENERATE_COVERAGE=false
            shift
            ;;
        --html)
            GENERATE_HTML=true
            shift
            ;;
        --xml)
            GENERATE_XML=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --filter)
            TEST_FILTER="$2"
            shift 2
            ;;
        --help)
            head -28 "$0" | tail -26
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Functions
print_header() {
    echo -e "\n${BOLD}${BLUE}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${BLUE}  $1${NC}"
    echo -e "${BOLD}${BLUE}═══════════════════════════════════════════════════════════════${NC}\n"
}

print_section() {
    echo -e "\n${CYAN}▶ $1${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

# Check prerequisites
check_prerequisites() {
    print_section "Checking prerequisites..."

    if [[ ! -d "$PROJECT_ROOT/$BUILD_DIR" ]]; then
        print_error "Build directory not found: $BUILD_DIR"
        echo "Please run cmake and build first:"
        echo "  mkdir -p $BUILD_DIR && cd $BUILD_DIR"
        echo "  cmake .. -DBUILD_TESTING=ON -DENABLE_COVERAGE=ON"
        echo "  make -j\$(nproc)"
        exit 1
    fi

    if ! command -v ctest &> /dev/null; then
        print_error "ctest not found. Please install CMake."
        exit 1
    fi

    if $GENERATE_COVERAGE; then
        if ! command -v gcovr &> /dev/null; then
            print_warning "gcovr not found. Installing..."
            pip3 install gcovr --quiet || {
                print_error "Failed to install gcovr"
                exit 1
            }
        fi
    fi

    print_success "Prerequisites OK"
}

# Create output directory
setup_output_dir() {
    mkdir -p "$PROJECT_ROOT/$OUTPUT_DIR"

    # Clean old coverage data
    if $GENERATE_COVERAGE; then
        print_section "Cleaning old coverage data..."
        find "$PROJECT_ROOT/$BUILD_DIR" -name "*.gcda" -delete 2>/dev/null || true
        print_success "Coverage data cleaned"
    fi
}

# Run tests
run_tests() {
    print_header "Running Unit Tests"

    cd "$PROJECT_ROOT/$BUILD_DIR"

    local ctest_args=("--output-on-failure" "-j$(nproc)")

    if [[ -n "$TEST_FILTER" ]]; then
        ctest_args+=("-R" "$TEST_FILTER")
    fi

    if $VERBOSE; then
        ctest_args+=("-V")
    fi

    if $GENERATE_XML; then
        ctest_args+=("--output-junit" "$PROJECT_ROOT/$OUTPUT_DIR/test-results.xml")
    fi

    # Run tests and capture output
    local start_time=$(date +%s)

    if ctest "${ctest_args[@]}" 2>&1 | tee "$PROJECT_ROOT/$OUTPUT_DIR/test-output.log"; then
        TEST_RESULT=0
    else
        TEST_RESULT=$?
    fi

    local end_time=$(date +%s)
    local duration=$((end_time - start_time))

    # Parse results from ctest output
    # Format: "100% tests passed, 0 tests failed out of 12"
    local test_summary=$(grep -E "tests passed.*out of" "$PROJECT_ROOT/$OUTPUT_DIR/test-output.log" || echo "")
    local total_tests=$(echo "$test_summary" | grep -oE "out of [0-9]+" | grep -oE "[0-9]+" || echo "0")
    local failed_tests=$(echo "$test_summary" | grep -oE "[0-9]+ tests failed" | grep -oE "[0-9]+" | head -1 || echo "0")
    local passed_tests=$((total_tests - failed_tests))

    echo ""
    echo -e "${BOLD}Test Results Summary:${NC}"
    echo "  Total:   $total_tests"
    echo -e "  Passed:  ${GREEN}$passed_tests${NC}"
    if [[ $failed_tests -gt 0 ]]; then
        echo -e "  Failed:  ${RED}$failed_tests${NC}"
    else
        echo -e "  Failed:  $failed_tests"
    fi
    echo "  Duration: ${duration}s"

    return $TEST_RESULT
}

# Generate coverage report
generate_coverage() {
    if ! $GENERATE_COVERAGE; then
        return 0
    fi

    print_header "Generating Coverage Report"

    cd "$PROJECT_ROOT"

    # Common gcovr arguments
    local gcovr_args=(
        "--root" "$PROJECT_ROOT"
        "--filter" "$PROJECT_ROOT/core/"
        "--exclude" ".*test.*"
        "--exclude" ".*examples.*"
        "--exclude" "/usr/.*"
        "--gcov-ignore-parse-errors"
    )

    # Console summary
    print_section "Coverage Summary"
    gcovr "${gcovr_args[@]}" --print-summary 2>/dev/null | tee "$OUTPUT_DIR/coverage-summary.txt"

    # Detailed text report
    print_section "Generating detailed coverage report..."
    gcovr "${gcovr_args[@]}" --txt "$OUTPUT_DIR/coverage-report.txt" 2>/dev/null
    print_success "Text report: $OUTPUT_DIR/coverage-report.txt"

    # HTML report
    if $GENERATE_HTML; then
        print_section "Generating HTML coverage report..."
        gcovr "${gcovr_args[@]}" \
            --html-details "$OUTPUT_DIR/coverage.html" \
            --html-title "IPB Code Coverage Report" \
            2>/dev/null
        print_success "HTML report: $OUTPUT_DIR/coverage.html"
    fi

    # XML reports (Cobertura format for CI tools)
    if $GENERATE_XML; then
        print_section "Generating XML coverage report (Cobertura)..."
        gcovr "${gcovr_args[@]}" \
            --xml "$OUTPUT_DIR/coverage.xml" \
            2>/dev/null
        print_success "XML report: $OUTPUT_DIR/coverage.xml"
    fi

    # Parse and display per-file coverage
    print_section "Per-file Coverage (core/):"
    echo ""

    # Use gcovr text output and parse it for a cleaner display
    gcovr "${gcovr_args[@]}" 2>/dev/null | grep -E "^core/" | while read -r line; do
        filename=$(echo "$line" | awk '{print $1}')
        coverage=$(echo "$line" | grep -oE "[0-9]+%" | tail -1)
        coverage_num=${coverage%\%}

        # Truncate filename for display
        short_name=$(echo "$filename" | cut -c1-55)

        # Color based on coverage level
        if [[ -n "$coverage_num" ]]; then
            if (( coverage_num >= 90 )); then
                color=$GREEN
            elif (( coverage_num >= 70 )); then
                color=$YELLOW
            else
                color=$RED
            fi
            printf "  %-55s ${color}%6s${NC}\n" "$short_name" "$coverage"
        fi
    done
}

# Generate final report
generate_final_report() {
    print_header "Test Report Generated"

    echo -e "Reports available in: ${BOLD}$OUTPUT_DIR/${NC}"
    echo ""
    echo "  📄 test-output.log       - Complete test output"

    if $GENERATE_COVERAGE; then
        echo "  📄 coverage-summary.txt  - Coverage summary"
        echo "  📄 coverage-report.txt   - Detailed coverage by file"
    fi

    if $GENERATE_HTML; then
        echo "  🌐 coverage.html         - Interactive HTML coverage report"
    fi

    if $GENERATE_XML; then
        echo "  📊 test-results.xml      - JUnit test results (for CI)"
        echo "  📊 coverage.xml          - Cobertura coverage (for CI)"
    fi

    echo ""

    # Check for coverage threshold
    if $GENERATE_COVERAGE; then
        local overall_coverage=$(grep -E "^lines:" "$OUTPUT_DIR/coverage-summary.txt" 2>/dev/null | grep -oE "[0-9]+\.[0-9]+" | head -1)
        if [[ -n "$overall_coverage" ]]; then
            echo -e "Overall Line Coverage: ${BOLD}${overall_coverage}%${NC}"

            if (( $(echo "$overall_coverage < 80" | bc -l) )); then
                print_warning "Coverage is below 80% threshold"
            else
                print_success "Coverage meets 80% threshold"
            fi
        fi
    fi
}

# Main execution
main() {
    print_header "IPB Test Runner"

    check_prerequisites
    setup_output_dir

    if run_tests; then
        print_success "All tests passed!"
    else
        print_error "Some tests failed!"
    fi

    generate_coverage
    generate_final_report

    # Return test result
    exit $TEST_RESULT
}

main
