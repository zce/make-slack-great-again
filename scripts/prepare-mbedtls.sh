#!/usr/bin/env bash
# Prepare the exact Mbed TLS release used by src/net/posix/posix.cmake.
# CI runs this once before parallel build/test jobs, then restores the
# extracted source from Actions cache. Local builds may still use FetchContent.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
POSIX_CMAKE="${PROJECT_ROOT}/src/net/posix/posix.cmake"
CACHE_DIR="${1:-${PROJECT_ROOT}/.ci-cache/fetchcontent}"

URL="$(sed -nE 's/^[[:space:]]*URL[[:space:]]+([^[:space:]]+).*$/\1/p' "${POSIX_CMAKE}")"
SHA256="$(sed -nE 's/^[[:space:]]*URL_HASH[[:space:]]+SHA256=([[:xdigit:]]+).*$/\1/p' "${POSIX_CMAKE}")"
if [[ -z "${URL}" || -z "${SHA256}" ]]; then
    echo "Could not read the pinned Mbed TLS URL and SHA256 from ${POSIX_CMAKE}" >&2
    exit 1
fi

mkdir -p "${CACHE_DIR}"
CACHE_DIR="$(cd -- "${CACHE_DIR}" && pwd)"
ARCHIVE="${CACHE_DIR}/${URL##*/}"
SOURCE_DIR="${CACHE_DIR}/msga_mbedtls-src"
MARKER="${SOURCE_DIR}/.msga-source-sha256"

valid_archive() {
    [[ -s "${ARCHIVE}" ]] &&
        printf '%s  %s\n' "${SHA256}" "${ARCHIVE}" | sha256sum --check --status
}

if ! valid_archive; then
    rm -f "${ARCHIVE}"
    TEMP_ARCHIVE="$(mktemp "${ARCHIVE}.XXXXXX")"
    trap 'rm -f "${TEMP_ARCHIVE}"' EXIT
    echo "Downloading pinned Mbed TLS release with retry support..."
    curl --fail --location \
        --retry 6 --retry-all-errors --retry-delay 3 --retry-max-time 240 \
        --connect-timeout 20 --max-time 180 \
        --output "${TEMP_ARCHIVE}" "${URL}"
    printf '%s  %s\n' "${SHA256}" "${TEMP_ARCHIVE}" | sha256sum --check
    mv "${TEMP_ARCHIVE}" "${ARCHIVE}"
    trap - EXIT
fi

if [[ ! -f "${MARKER}" ]] ||
   [[ "$(cat "${MARKER}" 2>/dev/null || true)" != "${SHA256}" ]] ||
   [[ ! -f "${SOURCE_DIR}/include/mbedtls/version.h" ]] ||
   [[ ! -f "${SOURCE_DIR}/library/psa_crypto_driver_wrappers_no_static.c" ]]; then
    rm -rf "${SOURCE_DIR}"
    mkdir -p "${SOURCE_DIR}"
    tar -xjf "${ARCHIVE}" --strip-components=1 -C "${SOURCE_DIR}"
    [[ -f "${SOURCE_DIR}/include/mbedtls/version.h" ]]
    [[ -f "${SOURCE_DIR}/library/psa_crypto_driver_wrappers_no_static.c" ]]
    printf '%s\n' "${SHA256}" > "${MARKER}"
fi

echo "Mbed TLS source ready: ${SOURCE_DIR}"
