#!/usr/bin/env bash
#
# Assemble and verify a RadioGeddon release directory.
#
#   scripts/package_release.sh DIR
#
# DIR must contain radiogeddon-<target>.fap and radiogeddon-<target>.build-info
# for each of catalog-official, full-roguemaster, full-momentum and
# full-unleashed, and for the device-specific RogueMaster build
# full-roguemaster-<commit>-api<api> pinned in scripts/firmware_pins.sh (as
# written by scripts/build_target.sh). The script re-verifies every .fap
# manifest (API, edition name, version) against its pin, checks that all of
# them were built from the same source revision, then writes:
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
device="full-roguemaster-${ROGUEMASTER_DEVICE_REF:0:9}-api${ROGUEMASTER_DEVICE_API}"
targets=(catalog-official full-roguemaster full-momentum full-unleashed "${device}")

faps=()
sources=()
for t in "${targets[@]}"; do
    [ -f "radiogeddon-${t}.fap" ] || { echo "!! missing radiogeddon-${t}.fap" >&2; exit 1; }
    [ -f "radiogeddon-${t}.build-info" ] || { echo "!! missing radiogeddon-${t}.build-info" >&2; exit 1; }
    if [ "${t}" = "${device}" ]; then
        api="${ROGUEMASTER_DEVICE_API}"
        # The record must name the pinned device commit, not the release pin.
        grep -q "^sdk: ${ROGUEMASTER_REPO}@${ROGUEMASTER_DEVICE_REF}\$" "radiogeddon-${t}.build-info" ||
            { echo "!! radiogeddon-${t}.fap was not built from RogueMaster ${ROGUEMASTER_DEVICE_REF}" >&2; exit 1; }
    else
        firmware="${t#*-}"
        api_var="$(echo "${firmware}" | tr '[:lower:]' '[:upper:]')_API"
        api="${!api_var}"
    fi
    name="RadioGeddon"
    [ "${t%%-*}" = full ] && name="RadioGeddon Full"
    python3 "${PROJECT_DIR}/scripts/verify_fap.py" "radiogeddon-${t}.fap" \
        --api "${api}" --name "${name}" --version "${app_version}"
    grep -q "^firmware: " "radiogeddon-${t}.build-info" || { echo "!! radiogeddon-${t}.build-info is incomplete" >&2; exit 1; }
    grep -qx "api: ${api}" "radiogeddon-${t}.build-info" || { echo "!! radiogeddon-${t}.build-info does not record API ${api}" >&2; exit 1; }
    grep -qx "version: ${release_version}" "radiogeddon-${t}.build-info" ||
        { echo "!! radiogeddon-${t}.build-info is not version ${release_version}" >&2; exit 1; }
    sources+=("$(sed -n 's/^source: //p' "radiogeddon-${t}.build-info")")
    faps+=("radiogeddon-${t}.fap")
done

# Every artifact from one source revision.
if [ "$(printf '%s\n' "${sources[@]}" | sort -u | wc -l)" -ne 1 ] || [ -z "${sources[0]}" ]; then
    echo "!! artifacts were built from different source revisions: ${sources[*]}" >&2
    exit 1
fi

for f in ./*.fap; do
    f="${f#./}"
    case " ${faps[*]} " in
        *" ${f} "*) ;;
        *) echo "!! unexpected file in release: ${f}" >&2; exit 1 ;;
    esac
done

sha256sum "${faps[@]}" > SHA256SUMS
sha256sum -c --quiet SHA256SUMS

{
    echo "RadioGeddon ${release_version} - build information"
    echo "Each .fap is one edition built for one firmware family; install the one matching your firmware."
    echo "Catalog edition: Official firmware. Full edition: RogueMaster, Momentum, Unleashed."
    echo "radiogeddon-${device}.fap: device-specific RogueMaster build (commit ${ROGUEMASTER_DEVICE_REF:0:9}, API ${ROGUEMASTER_DEVICE_API})."
    for t in "${targets[@]}"; do
        echo
        cat "radiogeddon-${t}.build-info"
    done
} > BUILD_INFO.txt
rm -f ./*.build-info

echo "==> release ready in $(pwd)"
cat SHA256SUMS
