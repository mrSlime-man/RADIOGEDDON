#!/usr/bin/env bash
#
# Build the RogueMaster .fap by compiling against the RogueMaster firmware
# source with its own fbt. RogueMaster is NOT just Unleashed: it ships its own
# SDK/API (e.g. API 88.16 vs Unleashed 88.9) and extra presets, so we build
# against the RM tree rather than assuming the Unleashed build will load.
#
# We never edit the embedded API metadata to force a mismatched load.
#
# Usage: scripts/build_roguemaster.sh [REF]
#   REF  optional RogueMaster git ref/branch (default: repository default)
#
# Output: dist/release/roguemaster/radiogeddon-roguemaster.fap (+ BUILD_INFO)

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RM_URL="https://github.com/RogueMaster/flipperzero-firmware-wplugins"
RM_DIR="${RM_FW_DIR:-$HOME/roguemaster/flipperzero-firmware-wplugins}"
REF="${1:-}"
OUT="${PROJECT_DIR}/dist/release/roguemaster"

# A toolchain is required. Reuse ufbt's if present (saves a second download).
export FBT_TOOLCHAIN_PATH="${FBT_TOOLCHAIN_PATH:-$HOME/.ufbt}"
export FBT_NO_SYNC="${FBT_NO_SYNC:-1}" # don't re-sync submodules on every run

if [ ! -d "${RM_DIR}/.git" ]; then
    echo "==> cloning RogueMaster firmware into ${RM_DIR} (large; one-time)"
    mkdir -p "$(dirname "${RM_DIR}")"
    GIT_LFS_SKIP_SMUDGE=1 git clone "${RM_URL}" "${RM_DIR}"
    git -C "${RM_DIR}" submodule update --init --depth 1
fi
if [ -n "${REF}" ]; then
    git -C "${RM_DIR}" checkout "${REF}"
fi

echo "==> staging app into RogueMaster tree"
rm -rf "${RM_DIR}/applications_user/radiogeddon"
mkdir -p "${RM_DIR}/applications_user/radiogeddon"
git -C "${PROJECT_DIR}" archive HEAD | tar -x -C "${RM_DIR}/applications_user/radiogeddon"

echo "==> building fap_radiogeddon with RogueMaster fbt"
( cd "${RM_DIR}" && ./fbt fap_radiogeddon )

FAP="${RM_DIR}/build/f7-firmware-C/.extapps/radiogeddon.fap"
[ -f "${FAP}" ] || { echo "!! RogueMaster build produced no .fap" >&2; exit 1; }

mkdir -p "${OUT}"
cp "${FAP}" "${OUT}/radiogeddon-roguemaster.fap"
api="$(grep -m1 '^Version,' "${RM_DIR}/targets/f7/api_symbols.csv" | cut -d, -f3 || true)"
rm_ref="$(git -C "${RM_DIR}" describe --tags --always 2>/dev/null || git -C "${RM_DIR}" rev-parse --short HEAD)"
echo "radiogeddon-roguemaster.fap  target=f7  api=${api}  rm_ref=${rm_ref}" > "${OUT}/BUILD_INFO.txt"
echo "==> done: ${OUT}/radiogeddon-roguemaster.fap (API ${api}, ${rm_ref})"
