#!/usr/bin/env bash
#
# Fetch the u8g2 fonts the Flipper canvas draws text with, for the host UI
# harness (make -C test ui): the firmware's lib/u8g2/u8g2_fonts.c at the
# pinned Official release (OFFICIAL_SOURCE_COMMIT), checked against its
# SHA-256, then the three fonts canvas_set_font() uses (FontPrimary =
# helvB08_tr, FontSecondary = haxrcorp4089_tr, FontKeyboard = profont11_mr,
# see applications/services/gui/canvas.c) extracted into a small header.
#
#   test/ui/fetch_fonts.sh DEST_FILE
#
# The font data is used only to draw synthetic development previews and to
# measure text on the host; it is not committed or shipped. u8g2 and its
# fonts are under their own licenses (https://github.com/olikraus/u8g2).

set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/firmware_pins.sh
source "${here}/../../scripts/firmware_pins.sh"
dest="${1:?usage: $0 DEST_FILE}"
sha="31420cf3992a15ab9764a98e953a80cbb32ba66c9eb54d081202b53afffbac2e"
src="$(dirname "${dest}")/u8g2_fonts.c.txt"
mkdir -p "$(dirname "${dest}")"
if ! echo "${sha}  ${src}" | sha256sum --quiet -c - > /dev/null 2>&1; then
    curl -sSfL --retry 3 -o "${src}.part" \
        "https://raw.githubusercontent.com/flipperdevices/flipperzero-firmware/${OFFICIAL_SOURCE_COMMIT}/lib/u8g2/u8g2_fonts.c"
    mv "${src}.part" "${src}"
    echo "${sha}  ${src}" | sha256sum --quiet --strict -c -
fi
python3 "${here}/extract_fonts.py" "${src}" "${dest}" \
    u8g2_font_helvB08_tr u8g2_font_haxrcorp4089_tr u8g2_font_profont11_mr
