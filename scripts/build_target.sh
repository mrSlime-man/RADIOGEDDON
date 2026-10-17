#!/usr/bin/env bash
#
# Build and verify one RadioGeddon .fap: one edition for one firmware family.
#
#   scripts/build_target.sh <target> [OUT_DIR]
#
#   target             edition   firmware SDK
#   catalog-official   Catalog   Official (ufbt SDK zip)
#   full-momentum      Full      Momentum (ufbt SDK zip)
#   full-unleashed     Full      Unleashed (ufbt SDK zip)
#   full-roguemaster   Full      RogueMaster (its own source tree and fbt)
#   full-roguemaster-device
#                      Full      RogueMaster at ROGUEMASTER_DEVICE_REF/_API
#                                (the device-specific release build)
#
# This is the exact build path used by CI and by the release workflow, so a
# local build reproduces a release artifact. Steps:
#   1. Write the edition's copy of the source (scripts/stage_edition.py).
#   2. Fetch the SDK pinned in scripts/firmware_pins.sh and verify its SHA-256
#      (Official, Momentum, Unleashed) or check out the pinned RogueMaster commit.
#   3. Assert the SDK's API version equals the pinned API version.
#   4. Build the .fap (ufbt, or RogueMaster's own fbt).
#   5. Verify the .fap: manifest (API, target, name, version) and that every
#      symbol it imports is exported by that SDK (verify_fap.py --symbols).
#   6. Write OUT_DIR/radiogeddon-<target>.fap and a .build-info record.
#
# Requirements: bash, git, curl, sha256sum, python3, ufbt (pip install
# "ufbt==$UFBT_VERSION"). The RogueMaster build downloads its firmware tree
# (~1-2 GB with submodules) and an ARM toolchain on first use.
#
# Environment overrides: RG_UFBT_HOME (SDK home), RM_DIR (RogueMaster tree),
# FBT_TOOLCHAIN_PATH (toolchain home for the RogueMaster build).
#
# RogueMaster ships often and its API minor changes with almost every build,
# so a device rarely runs exactly the pinned commit. To build for the commit
# a device actually runs (Settings > About > Firmware Info shows its short
# hash and API, e.g. "4f2e0620 [88.4]"):
#
#   RM_REF=<full or short commit> RM_API=<api on the device> \
#       scripts/build_target.sh full-roguemaster
#
# Both are required together, the API is still asserted against that tree,
# and the build record names the commit. The artifact is named after them,
# radiogeddon-full-roguemaster-<first 9 hex of the commit>-api<api>.fap, so it
# never replaces the release build radiogeddon-full-roguemaster.fap. The
# release ships one such build, pinned in scripts/firmware_pins.sh
# (target full-roguemaster-device).

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/firmware_pins.sh
source "${PROJECT_DIR}/scripts/firmware_pins.sh"

target="${1:-}"
out_dir="${2:-${PROJECT_DIR}/dist/release}"
case "${target}" in
    catalog-official) edition=catalog firmware=official ;;
    full-momentum) edition=full firmware=momentum ;;
    full-unleashed) edition=full firmware=unleashed ;;
    full-roguemaster) edition=full firmware=roguemaster ;;
    full-roguemaster-device)
        edition=full firmware=roguemaster
        RM_REF="${RM_REF:-${ROGUEMASTER_DEVICE_REF}}" RM_API="${RM_API:-${ROGUEMASTER_DEVICE_API}}"
        ;;
    *)
        echo "usage: $0 <catalog-official|full-roguemaster|full-momentum|full-unleashed|full-roguemaster-device> [OUT_DIR]" >&2
        exit 64
        ;;
esac

log() { echo "==> [${target}] $*"; }
die() {
    echo "!! [${target}] $*" >&2
    exit 1
}

cd "${PROJECT_DIR}"
mkdir -p "${out_dir}"
out_dir="$(cd "${out_dir}" && pwd)"

app_version="$(sed -n 's/^[[:space:]]*fap_version="\([0-9.]*\)".*/\1/p' application.fam)"
[ -n "${app_version}" ] || die "could not read fap_version from application.fam"
release_version="$(sed -n 's/^#define RADIOGEDDON_VERSION "\(.*\)"$/\1/p' radiogeddon_version.h)"
[ -n "${release_version}" ] || die "could not read RADIOGEDDON_VERSION from radiogeddon_version.h"

if [ "${edition}" = full ]; then
    appid=radiogeddon_full app_name="RadioGeddon Full"
    # The Full edition's optional tools, packed in the .fap (modules/modules.fam).
    modules="$(sed -n 's/^    appid="\(radiogeddon_[a-z]*\)",$/plugins\/\1.fal/p' modules/modules.fam | paste -sd, -)"
    [ -n "${modules}" ] || die "no modules listed in modules/modules.fam"
else
    appid=radiogeddon app_name="RadioGeddon"
    modules=""
fi

source_rev="$(git rev-parse HEAD 2>/dev/null || echo unknown)"
if [ -n "$(git status --porcelain --untracked-files=no 2>/dev/null)" ]; then
    source_rev="${source_rev}-dirty"
fi

upper="$(echo "${firmware}" | tr '[:lower:]' '[:upper:]')"
pinned_api_var="${upper}_API"
pinned_api="${!pinned_api_var}"
fw_label_var="${upper}_FW_LABEL"
fw_label="${!fw_label_var}"
if [ -n "${RM_REF:-}${RM_API:-}" ]; then
    [ "${firmware}" = roguemaster ] || die "RM_REF/RM_API apply to full-roguemaster only"
    [ -n "${RM_REF:-}" ] && [ -n "${RM_API:-}" ] || die "set both RM_REF and RM_API"
    [[ "${RM_REF}" =~ ^[0-9a-f]{7,40}$ ]] || die "RM_REF must be a commit hash (7-40 hex digits)"
    [[ "${RM_API}" =~ ^[0-9]+\.[0-9]+$ ]] || die "RM_API must be MAJOR.MINOR (e.g. 88.4)"
    ROGUEMASTER_REF="${RM_REF}"
    pinned_api="${RM_API}"
    fw_label="RogueMaster @ ${RM_REF:0:9} (device-specific build, API ${RM_API})"
    # Named after the device's firmware, never radiogeddon-full-roguemaster.fap.
    target="full-roguemaster-${RM_REF:0:9}-api${RM_API}"
fi
artifact="${out_dir}/radiogeddon-${target}.fap"
rm -f "${artifact}"

sdk_api_from_csv() {
    grep -m1 '^Version,' "$1" | cut -d, -f3
}

build_with_ufbt() {
    local url_var="${upper}_SDK_URL" sha_var="${upper}_SDK_SHA256"
    local url="${!url_var}" sha="${!sha_var}"
    local cache="${PROJECT_DIR}/.ufbt-cache"
    local zip stage
    zip="${cache}/$(basename "${url}")"
    export UFBT_HOME="${RG_UFBT_HOME:-${PROJECT_DIR}/.ufbt-${firmware}}"

    command -v ufbt > /dev/null || die "ufbt not found (pip install \"ufbt==${UFBT_VERSION}\")"
    mkdir -p "${cache}" "${UFBT_HOME}"

    if ! echo "${sha}  ${zip}" | sha256sum -c --status 2> /dev/null; then
        log "downloading SDK ${url}"
        curl -fsSL --retry 3 --retry-delay 5 -o "${zip}.part" "${url}"
        mv "${zip}.part" "${zip}"
    fi
    echo "${sha}  ${zip}" | sha256sum -c --quiet || die "SDK checksum mismatch for ${zip}"
    log "SDK checksum OK (${sha})"

    log "deploying SDK into ${UFBT_HOME}"
    ufbt update --hw-target f7 --local "${zip}" > /dev/null

    symbols_csv="${UFBT_HOME}/current/sdk_headers/f7_sdk/targets/f7/api_symbols.csv"
    local api
    api="$(sdk_api_from_csv "${symbols_csv}")"
    [ "${api}" = "${pinned_api}" ] || die "SDK API ${api} != pinned ${pinned_api}"
    log "SDK API ${api}"

    # The edition's own copy, in a dot-directory the app's source globs skip.
    stage="${PROJECT_DIR}/.stage/${target}"
    python3 "${PROJECT_DIR}/scripts/stage_edition.py" "${edition}" "${stage}"

    log "building"
    (cd "${stage}" && ufbt)
    [ -f "${stage}/dist/${appid}.fap" ] || die "ufbt produced no dist/${appid}.fap"
    cp "${stage}/dist/${appid}.fap" "${artifact}"
    sdk_ref="${url} sha256=${sha}"
}

build_with_roguemaster() {
    local rm_dir="${RM_DIR:-${PROJECT_DIR}/.ufbt-rmfw}"
    # fbt needs an ARM toolchain; reuse an existing ufbt home if present,
    # otherwise fbt downloads its own into this directory.
    if [ -z "${FBT_TOOLCHAIN_PATH:-}" ]; then
        local home
        FBT_TOOLCHAIN_PATH="${PROJECT_DIR}/.ufbt-toolchain"
        for home in official momentum unleashed; do
            if [ -d "${PROJECT_DIR}/.ufbt-${home}/toolchain" ]; then
                FBT_TOOLCHAIN_PATH="${PROJECT_DIR}/.ufbt-${home}"
                break
            fi
        done
    fi
    export FBT_TOOLCHAIN_PATH
    export FBT_NO_SYNC=1
    export GIT_LFS_SKIP_SMUDGE=1
    mkdir -p "${FBT_TOOLCHAIN_PATH}"

    local head
    head="$(git -C "${rm_dir}" rev-parse HEAD 2> /dev/null || true)"
    if [ -z "${head}" ] || [ "${head#"${ROGUEMASTER_REF}"}" = "${head}" ]; then
        log "fetching RogueMaster ${ROGUEMASTER_REF}"
        rm -rf "${rm_dir}"
        git init -q "${rm_dir}"
        git -C "${rm_dir}" remote add origin "${ROGUEMASTER_REPO}"
        if ! git -C "${rm_dir}" fetch -q --depth 1 origin "${ROGUEMASTER_REF}" 2> /dev/null; then
            # A short hash cannot be fetched by itself: look it up on the
            # default branch's recent history.
            git -C "${rm_dir}" fetch -q --filter=blob:none --shallow-since=2025-01-01 origin 420
            ROGUEMASTER_REF="$(git -C "${rm_dir}" rev-parse --verify "${ROGUEMASTER_REF}^{commit}")" ||
                die "RogueMaster commit ${RM_REF:-} not found"
        fi
        git -C "${rm_dir}" checkout -q "${ROGUEMASTER_REF}" 2> /dev/null || git -C "${rm_dir}" checkout -q FETCH_HEAD
        git -C "${rm_dir}" submodule update -q --init --depth 1 --jobs 4
    fi
    head="$(git -C "${rm_dir}" rev-parse HEAD)"
    [ "${head#"${ROGUEMASTER_REF}"}" != "${head}" ] || die "RogueMaster tree is not at ${ROGUEMASTER_REF}"
    ROGUEMASTER_REF="${head}"

    symbols_csv="${rm_dir}/targets/f7/api_symbols.csv"
    local api
    api="$(sdk_api_from_csv "${symbols_csv}")"
    [ "${api}" = "${pinned_api}" ] || die "RogueMaster API ${api} != pinned ${pinned_api}"
    log "RogueMaster API ${api}"

    # The edition's copy of every tracked file (no build caches), staged as a
    # user app of the firmware tree.
    rm -rf "${rm_dir}/applications_user/radiogeddon"
    python3 "${PROJECT_DIR}/scripts/stage_edition.py" "${edition}" "${rm_dir}/applications_user/radiogeddon"

    # Remove any artifact from a previous run so a stale file can't be picked up.
    if [ -d "${rm_dir}/build" ]; then
        find "${rm_dir}/build" -path "*/.extapps/${appid}.fap" -delete
    fi

    log "building with RogueMaster fbt"
    (cd "${rm_dir}" && ./fbt "fap_${appid}")

    local built
    built="$(find "${rm_dir}/build" -path "*/.extapps/${appid}.fap" -print -quit)"
    [ -n "${built}" ] || die "RogueMaster build produced no ${appid}.fap"
    cp "${built}" "${artifact}"
    sdk_ref="${ROGUEMASTER_REPO}@${ROGUEMASTER_REF}"
}

case "${firmware}" in
    official | momentum | unleashed) build_with_ufbt ;;
    roguemaster) build_with_roguemaster ;;
esac

log "verifying manifest and imported symbols"
python3 "${PROJECT_DIR}/scripts/verify_fap.py" "${artifact}" \
    --api "${pinned_api}" --name "${app_name}" --version "${app_version}" \
    --symbols "${symbols_csv}" ${modules:+--modules "${modules}"}

fap_sha="$(sha256sum "${artifact}" | cut -d' ' -f1)"
cat > "${out_dir}/radiogeddon-${target}.build-info" << EOF
artifact: radiogeddon-${target}.fap
edition: ${edition}
appid: ${appid}
firmware: ${fw_label}
api: ${pinned_api}
hardware_target: f7
version: ${release_version}
manifest_version: ${app_version}
sdk: ${sdk_ref}
ufbt: ${UFBT_VERSION}
source: ${source_rev}
sha256: ${fap_sha}
modules: ${modules:-none}
EOF
log "done: ${artifact} (API ${pinned_api}, sha256 ${fap_sha})"
