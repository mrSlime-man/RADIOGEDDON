#!/usr/bin/env bash
#
# Build the full RadioGeddon release set locally: one verified .fap per
# edition and firmware family, plus SHA256SUMS and BUILD_INFO.txt.
#
#   scripts/build_release.sh [OUT_DIR] [TARGET...]
#
#   OUT_DIR  default: dist/release
#   TARGET   any of: catalog-official full-roguemaster full-momentum
#            full-unleashed (default: all four)
#
# Each target is built by scripts/build_target.sh against the SDK pinned in
# scripts/firmware_pins.sh — the same path CI and the release workflow use.
# Official releases are produced by .github/workflows/release.yml from a tag;
# this script exists so anyone can reproduce and check those artifacts.

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="${1:-${PROJECT_DIR}/dist/release}"
shift || true
targets=("$@")
[ "${#targets[@]}" -gt 0 ] || targets=(catalog-official full-roguemaster full-momentum full-unleashed)

mkdir -p "${out_dir}"
for t in "${targets[@]}"; do
    "${PROJECT_DIR}/scripts/build_target.sh" "${t}" "${out_dir}"
done

"${PROJECT_DIR}/scripts/package_release.sh" "${out_dir}"
