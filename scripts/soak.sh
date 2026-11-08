#!/usr/bin/env bash
# Build and run the fixture-driven long-session memory soak.
# Usage: scripts/soak.sh [rounds] [fixture-dir]
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
ROUNDS="${1:-20}"
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
    echo "usage: scripts/soak.sh [positive-round-count] [fixture-dir]" >&2
    exit 2
fi

"${SCRIPT_DIR}/build.sh" --debug --demo

APP="${PROJECT_ROOT}/build-debug/msga"
if [[ "$(uname -s)" == "Darwin" ]]; then
    APP="${PROJECT_ROOT}/build-debug/msga.app/Contents/MacOS/msga"
fi
if [[ ! -x "${APP}" ]]; then
    echo "soak executable not found: ${APP}" >&2
    exit 1
fi

export MSGA_MEMSTATS="${MSGA_MEMSTATS:-60}"
exec "${APP}" --demo "${FIXTURE_DIR}" --soak "${ROUNDS}"
