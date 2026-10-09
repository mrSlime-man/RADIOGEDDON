#!/usr/bin/env bash
#
# Assemble and verify a RadioGeddon release directory.
#
#   scripts/package_release.sh DIR
#
# DIR must contain radiogeddon-<target>.fap and radiogeddon-<target>.build-info
# for each of official, unleashed and roguemaster (as written by
# scripts/build_target.sh). The script re-verifies every .fap manifest against
# its pinned API, then writes:
#   DIR/SHA256SUMS     sha256 of every .fap (verify with: sha256sum -c SHA256SUMS)
#   DIR/BUILD_INFO.txt firmware, API, SDK and source revision of each artifact
# and removes the per-target .build-info files. It fails if any artifact is
# missing or invalid, so a partial release can never be packaged.

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/firmware_pins.sh
source "${PROJECT_DIR}/scripts/firmware_pins.sh"

dir="${1:?usage: $0 DIR}"
cd "${dir}"

app_version="$(sed -n 's/^[[:space:]]*fap_version="\([0-9.]*\)".*/\1/p' "${PROJECT_DIR}/application.fam")"
release_version="$(sed -n 's/^#define RADIOGEDDON_VERSION "\(.*\)"$/\1/p' "${PROJECT_DIR}/radiogeddon_version.h")"
targets=(official unleashed roguemaster)

for t in "${targets[@]}"; do
    [ -f "radiogeddon-${t}.fap" ] || { echo "!! missing radiogeddon-${t}.fap" >&2; exit 1; }
    [ -f "radiogeddon-${t}.build-info" ] || { echo "!! missing radiogeddon-${t}.build-info" >&2; exit 1; }
    api_var="$(echo "${t}" | tr '[:lower:]' '[:upper:]')_API"
    python3 "${PROJECT_DIR}/scripts/verify_fap.py" "radiogeddon-${t}.fap" \
        --api "${!api_var}" --name RadioGeddon --version "${app_version}"
done

extra="$(find . -maxdepth 1 -name '*.fap' ! -name 'radiogeddon-official.fap' \
    ! -name 'radiogeddon-unleashed.fap' ! -name 'radiogeddon-roguemaster.fap' -printf '%f ')"
[ -z "${extra}" ] || { echo "!! unexpected files in release: ${extra}" >&2; exit 1; }

sha256sum radiogeddon-official.fap radiogeddon-unleashed.fap radiogeddon-roguemaster.fap > SHA256SUMS
sha256sum -c --quiet SHA256SUMS

{
    echo "RadioGeddon ${release_version} - build information"
    echo "Each .fap targets one firmware family; install the one matching your firmware."
    for t in "${targets[@]}"; do
        echo
        cat "radiogeddon-${t}.build-info"
    done
} > BUILD_INFO.txt
rm -f ./*.build-info

echo "==> release ready in $(pwd)"
cat SHA256SUMS
