#!/bin/bash
# IPB Code Quality Check Script
# Usage: ./scripts/check-quality.sh [--fix] [--format] [--tidy] [--docs]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default options
DO_FORMAT=false
DO_TIDY=false
DO_DOCS=false
FIX_MODE=false
VERBOSE=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --fix)
            FIX_MODE=true
            shift
            ;;
        --format)
            DO_FORMAT=true
            shift
            ;;
        --tidy)
            DO_TIDY=true
            shift
            ;;
        --docs)
            DO_DOCS=true
            shift
            ;;
        --all)
            DO_FORMAT=true
            DO_TIDY=true
            DO_DOCS=true
            shift
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --format    Run clang-format check"
            echo "  --tidy      Run clang-tidy check"
            echo "  --docs      Generate documentation"
            echo "  --all       Run all checks"
            echo "  --fix       Apply fixes (for format and tidy)"
            echo "  --verbose   Show detailed output"
            echo "  --help      Show this help"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# If no specific checks requested, do all
if ! $DO_FORMAT && ! $DO_TIDY && ! $DO_DOCS; then
    DO_FORMAT=true
    DO_TIDY=true
fi

cd "$PROJECT_ROOT"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}     IPB Code Quality Check${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

ERRORS=0

#---------------------------------------------------------------------------
# clang-format check
#---------------------------------------------------------------------------
if $DO_FORMAT; then
    echo -e "${YELLOW}[1/3] Running clang-format...${NC}"

    # Find source files
    FILES=$(find core benchmarks -name "*.hpp" -o -name "*.cpp" 2>/dev/null | grep -v build | head -100)

    if [ -z "$FILES" ]; then
        echo -e "${YELLOW}  No source files found${NC}"
    else
        FORMAT_ISSUES=0

        for file in $FILES; do
            if $FIX_MODE; then
                clang-format -i "$file" 2>/dev/null || true
            else
                if ! clang-format --dry-run -Werror "$file" 2>/dev/null; then
                    if $VERBOSE; then
                        echo -e "  ${RED}Format issue: $file${NC}"
                    fi
                    FORMAT_ISSUES=$((FORMAT_ISSUES + 1))
                fi
            fi
        done

        if [ $FORMAT_ISSUES -eq 0 ]; then
            echo -e "  ${GREEN}✓ All files properly formatted${NC}"
        else
            echo -e "  ${RED}✗ $FORMAT_ISSUES files need formatting${NC}"
            ERRORS=$((ERRORS + FORMAT_ISSUES))
        fi
    fi
    echo ""
fi

#---------------------------------------------------------------------------
# clang-tidy check
#---------------------------------------------------------------------------
if $DO_TIDY; then
    echo -e "${YELLOW}[2/3] Running clang-tidy...${NC}"

    # Check if compile_commands.json exists
    if [ -f "build/compile_commands.json" ]; then
        # Find headers to check (limit to avoid long runs)
        HEADERS=$(find core -name "*.hpp" 2>/dev/null | grep -v build | head -20)

        TIDY_ISSUES=0

        for header in $HEADERS; do
            if $VERBOSE; then
                echo -e "  Checking: $header"
            fi

            FIX_ARG=""
            if $FIX_MODE; then
                FIX_ARG="--fix"
            fi

            if ! clang-tidy -p build $FIX_ARG "$header" 2>/dev/null | grep -q "warning:"; then
                : # No warnings
            else
                TIDY_ISSUES=$((TIDY_ISSUES + 1))
            fi
        done

        if [ $TIDY_ISSUES -eq 0 ]; then
            echo -e "  ${GREEN}✓ No clang-tidy issues found${NC}"
        else
            echo -e "  ${YELLOW}⚠ $TIDY_ISSUES files have warnings${NC}"
        fi
    else
        echo -e "  ${YELLOW}⚠ compile_commands.json not found, skipping${NC}"
        echo -e "  Run 'cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..' first"
    fi
    echo ""
fi

#---------------------------------------------------------------------------
# Documentation generation
#---------------------------------------------------------------------------
if $DO_DOCS; then
    echo -e "${YELLOW}[3/3] Generating documentation...${NC}"

    if command -v doxygen &> /dev/null; then
        if doxygen Doxyfile 2>&1 | grep -c "warning:" > /tmp/doxy_warn_count; then
            WARN_COUNT=$(cat /tmp/doxy_warn_count)
            if [ "$WARN_COUNT" -gt 0 ]; then
                echo -e "  ${YELLOW}⚠ $WARN_COUNT documentation warnings${NC}"
            else
                echo -e "  ${GREEN}✓ Documentation generated successfully${NC}"
            fi
        fi
        echo -e "  Output: docs/api/html/index.html"
    else
        echo -e "  ${YELLOW}⚠ Doxygen not installed, skipping${NC}"
    fi
    echo ""
fi

#---------------------------------------------------------------------------
# Summary
#---------------------------------------------------------------------------
echo -e "${BLUE}========================================${NC}"
if [ $ERRORS -eq 0 ]; then
    echo -e "${GREEN}     All checks passed!${NC}"
else
    echo -e "${RED}     $ERRORS issues found${NC}"
fi
echo -e "${BLUE}========================================${NC}"

exit $ERRORS
