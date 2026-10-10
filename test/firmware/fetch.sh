#!/usr/bin/env bash
#
# Download the Flipper Zero firmware files that test_formats builds and reads:
# the firmware's FlipperFormat and stream code, and the Sub-GHz test files of
# its own unit tests. They come from the commit of the pinned Official release
# (OFFICIAL_SOURCE_COMMIT in scripts/firmware_pins.sh) and are checked against
# test/firmware/files.sha256.
#
#   test/firmware/fetch.sh [DEST]      (default: test/build/fw)
#
# The files are GPL-3.0 (https://github.com/flipperdevices/flipperzero-firmware).
# They are only used to build and run tests here: they are not committed and
# not part of RadioGeddon or its releases.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/firmware_pins.sh
source "${here}/../../scripts/firmware_pins.sh"
dest="${1:-${here}/../build/fw}"
base="https://raw.githubusercontent.com/flipperdevices/flipperzero-firmware/${OFFICIAL_SOURCE_COMMIT}"
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
    curl -sSfL --retry 3 -o "${path}.part" "${base}/${path}"
    mv "${path}.part" "${path}"
done < "${manifest}"

sha256sum --quiet --strict -c "${manifest}"
echo "firmware test files ready in ${dest} ($(wc -l < "${manifest}") files, ${OFFICIAL_SOURCE_COMMIT:0:7})"
