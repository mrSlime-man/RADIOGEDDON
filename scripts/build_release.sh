#!/usr/bin/env bash
#
# Build RadioGeddon .fap packages for each supported firmware family.
#
# Each firmware embeds an "API version" in its SDK; a .fap only loads on a
# firmware whose API is compatible with the one the .fap was built against.
# We therefore build a SEPARATE .fap per target against that target's own SDK.
# We NEVER edit the embedded API/metadata to force a mismatch to load.
#
# Requirements: ufbt (pip install ufbt). Network access to each SDK index.
#
# Usage:
#   ./scripts/build_release.sh                 # build all reachable targets
#   ./scripts/build_release.sh official         # build only official
#
# Output: dist/release/<target>/radiogeddon-<target>.fap  (+ SHA256SUMS)

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RELEASE_DIR="${PROJECT_DIR}/dist/release"
cd "${PROJECT_DIR}"

# target_name|ufbt_update_args
TARGETS=(
    "official|--channel release"
    "unleashed|--index-url https://up.unleashedflip.com/directory.json"
    # RogueMaster: uncomment and set the correct index URL for your setup.
    # "roguemaster|--index-url https://<roguemaster-sdk-index>/directory.json"
)

want="${1:-all}"
mkdir -p "${RELEASE_DIR}"

build_one() {
    local name="$1"; shift
    local update_args="$*"
    local home="${PROJECT_DIR}/.ufbt-${name}"
    local out="${RELEASE_DIR}/${name}"

    echo "==> [${name}] deploying SDK (${update_args})"
    mkdir -p "${home}" "${out}"
    UFBT_HOME="${home}" ufbt update ${update_args}

    echo "==> [${name}] building"
    UFBT_HOME="${home}" ufbt

    cp "dist/radiogeddon.fap" "${out}/radiogeddon-${name}.fap"
    # Record the API version this artifact targets, for the release notes.
    local api
    api="$(grep -m1 '^Version,' "${home}/current/sdk_headers/f7_sdk/targets/f7/api_symbols.csv" | cut -d, -f3 || true)"
    echo "radiogeddon-${name}.fap  target=f7  api=${api}" > "${out}/BUILD_INFO.txt"
    echo "==> [${name}] done (API ${api})"
}

built=0
for entry in "${TARGETS[@]}"; do
    name="${entry%%|*}"
    args="${entry#*|}"
    if [[ "${want}" != "all" && "${want}" != "${name}" ]]; then
        continue
    fi
    if build_one "${name}" "${args}"; then
        built=$((built + 1))
    else
        echo "!! [${name}] build failed or SDK unreachable - skipping" >&2
    fi
done

if [[ "${built}" -eq 0 ]]; then
    echo "No targets built." >&2
    exit 1
fi

echo "==> writing checksums"
( cd "${RELEASE_DIR}" && find . -name '*.fap' -print0 | xargs -0 sha256sum > SHA256SUMS )
cat "${RELEASE_DIR}/SHA256SUMS"
echo "==> release artifacts in ${RELEASE_DIR}"
