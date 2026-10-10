#!/usr/bin/env bash
#
# Download the Flipper Zero firmware files that test_formats and test_fwdecode
# build and read: the firmware's FlipperFormat and stream code, its Sub-GHz
# protocol decoders, and the Sub-GHz test files and test source of its own unit
# tests. They come from the commit of the pinned Official release
# (OFFICIAL_SOURCE_COMMIT in scripts/firmware_pins.sh); lib/mlib, a submodule
# there, comes from M*LIB at OFFICIAL_MLIB_COMMIT. All are checked against
# test/firmware/files.sha256.
#
#   test/firmware/fetch.sh [DEST]      (default: test/build/fw)
#
# The firmware files are GPL-3.0 (https://github.com/flipperdevices/flipperzero-firmware),
# M*LIB is BSD-2-Clause (https://github.com/P-p-H-d/mlib). They are only used to
# build and run tests here: they are not committed and not part of RadioGeddon
# or its releases.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/firmware_pins.sh
source "${here}/../../scripts/firmware_pins.sh"
dest="${1:-${here}/../build/fw}"
base="https://raw.githubusercontent.com/flipperdevices/flipperzero-firmware/${OFFICIAL_SOURCE_COMMIT}"
mlib_base="https://raw.githubusercontent.com/P-p-H-d/mlib/${OFFICIAL_MLIB_COMMIT}"
manifest="${here}/files.sha256"

mkdir -p "${dest}"
cd "${dest}"
if sha256sum --quiet --strict -c "${manifest}" > /dev/null 2>&1; then
    exit 0
fi

while read -r sum path; do
    if [ -f "${path}" ] && echo "${sum}  ${path}" | sha256sum --quiet -c - > /dev/null 2>&1; then
        continue
    fi
    mkdir -p "$(dirname "${path}")"
    case "${path}" in
        lib/mlib/*) url="${mlib_base}/${path#lib/mlib/}" ;;
        *) url="${base}/${path}" ;;
    esac
    curl -sSfL --retry 3 -o "${path}.part" "${url}"
    mv "${path}.part" "${path}"
done < "${manifest}"

sha256sum --quiet --strict -c "${manifest}"
echo "firmware test files ready in ${dest} ($(wc -l < "${manifest}") files, ${OFFICIAL_SOURCE_COMMIT:0:7})"
