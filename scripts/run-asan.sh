#!/usr/bin/env bash
# Build, test and run the memory soak under AddressSanitizer/LeakSanitizer.
# Usage: scripts/run-asan.sh [rounds] [fixture-dir]
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
ROUNDS="${1:-10}"
FIXTURE_DIR="${2:-}"
if [[ -z "${FIXTURE_DIR}" ]]; then
    if [[ -f "${PROJECT_ROOT}/demo/fixture.json" ]]; then
        FIXTURE_DIR="${PROJECT_ROOT}/demo"
    else
        FIXTURE_DIR="${PROJECT_ROOT}/tests/assets"
    fi
fi
if [[ ! -f "${FIXTURE_DIR}/fixture.json" ]]; then
    echo "fixture directory must contain fixture.json: ${FIXTURE_DIR}" >&2
    exit 1
fi
FIXTURE_DIR="$(cd -- "${FIXTURE_DIR}" && pwd)"

if [[ ! "${ROUNDS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "usage: scripts/run-asan.sh [positive-round-count] [fixture-dir]" >&2
    exit 2
fi

"${SCRIPT_DIR}/build.sh" --asan --demo --test

APP="${PROJECT_ROOT}/build-asan/msga"
if [[ "$(uname -s)" == "Darwin" ]]; then
    APP="${PROJECT_ROOT}/build-asan/msga.app/Contents/MacOS/msga"
fi
if [[ ! -x "${APP}" ]]; then
    echo "ASan executable not found: ${APP}" >&2
    exit 1
fi

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}"
export MSGA_MEMSTATS="${MSGA_MEMSTATS:-60}"
exec "${APP}" --demo "${FIXTURE_DIR}" --soak "${ROUNDS}"
