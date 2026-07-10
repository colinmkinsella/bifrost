#!/usr/bin/env bash
# test.sh  —  Build and run the Bifrost test suite (Linux / macOS)
#
# Usage:  ./test.sh [--no-build] [--verbose]
#
#   (no args)   Build the fixture binaries + the headless diff harness, then run
#               the harness and assert the expected diff between target_v1 and
#               target_v2.
#   --no-build  Skip the cmake --build step (use existing test binaries)
#   --verbose   Show the harness's full per-function diff output
#
# The harness (tests/harness/diff_harness.cpp) links the Binary Ninja API/core
# and the Qt-free engine sources, so it exercises real feature extraction plus
# the full matching pipeline without the UI. It needs a Binary Ninja core that
# can initialise headlessly (a licensed install).
#
# Prerequisites:
#   The build must have been configured — run ./build.sh first (it resolves
#   BN_API_PATH and Qt, which the cmake build directory needs).
#
# Exit code:  0 = all checks passed,  1 = a check failed or the harness errored.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BIN_DIR="$BUILD_DIR/tests/bin"
NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

if [[ -t 1 ]]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BOLD='\033[1m'; RESET='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; BOLD=''; RESET=''
fi
info()    { echo -e "${BOLD}$*${RESET}"; }
success() { echo -e "${GREEN}$*${RESET}"; }
warn()    { echo -e "${YELLOW}$*${RESET}"; }
fail()    { echo -e "${RED}$*${RESET}"; }

DO_BUILD=1
VERBOSE=0
for arg in "$@"; do
    case "$arg" in
        --no-build) DO_BUILD=0 ;;
        --verbose)  VERBOSE=1 ;;
        -h|--help)  sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)          warn "Unknown option: $arg  (try --help)"; exit 1 ;;
    esac
done

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    fail "ERROR: the build directory is not configured."
    fail "       Run ./build.sh first, then re-run ./test.sh."
    exit 1
fi

# ---------------------------------------------------------------------------
# Build the fixtures + harness
# ---------------------------------------------------------------------------
if [[ $DO_BUILD -eq 1 ]]; then
    echo
    info "====== Building fixtures + harness ======"
    cmake --build "$BUILD_DIR" --target target_v1 target_v2 diff_harness -j "$NPROC" \
        || { fail "ERROR: build failed."; exit 1; }
fi

HARNESS="$BIN_DIR/diff_harness"
V1="$BIN_DIR/libtarget_v1.dylib"
V2="$BIN_DIR/libtarget_v2.dylib"
[[ "$(uname)" != "Darwin" ]] && { V1="$BIN_DIR/libtarget_v1.so"; V2="$BIN_DIR/libtarget_v2.so"; }

for f in "$HARNESS" "$V1" "$V2"; do
    [[ -e "$f" ]] || { fail "ERROR: missing $f — run ./test.sh without --no-build."; exit 1; }
done

# ---------------------------------------------------------------------------
# Run the headless diff harness
# ---------------------------------------------------------------------------
echo
info "====== Running diff harness ======"
set +e
if [[ $VERBOSE -eq 1 ]]; then
    "$HARNESS" "$V1" "$V2"
    STATUS=$?
else
    # Drop Binary Ninja's analysis chatter; keep the summary + checks.
    "$HARNESS" "$V1" "$V2" 2>&1 | grep -E "summary:|PASS|FAIL|RESULT"
    STATUS=${PIPESTATUS[0]}
fi
set -e

echo
if [[ $STATUS -eq 0 ]]; then
    success "====== All checks passed ======"
else
    fail "====== Tests FAILED (exit $STATUS) ======"
fi
exit $STATUS
