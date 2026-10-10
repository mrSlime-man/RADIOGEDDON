# shellcheck shell=bash
# shellcheck disable=SC2034  # constants consumed by the scripts that source this file
#
# Single source of truth for the firmware SDKs RadioGeddon is built against.
# Sourced by scripts/build_target.sh (local builds) and by CI/release workflows,
# so a release is always built against exactly these, integrity-checked SDKs.
#
# A .fap embeds the API version of the SDK it was compiled with. Firmware
# refuses an app whose API *major* differs from its own, and an app built with
# a newer API *minor* may need functions an older firmware lacks. Each artifact
# therefore targets one firmware family; the *_API values below are asserted
# against both the SDK and the built .fap.
#
# To move to a newer firmware: update URL + SHA256 (+ API) here, rebuild, run
# the hardware checklist, and note the change in CHANGELOG.md.

# uFBT version used to deploy SDKs and build (pip install "ufbt==${UFBT_VERSION}").
UFBT_VERSION="0.2.6"

# Official Flipper Zero firmware 1.4.3 (release channel).
OFFICIAL_SDK_URL="https://update.flipperzero.one/builds/firmware/1.4.3/flipper-z-f7-sdk-1.4.3.zip"
OFFICIAL_SDK_SHA256="2e89e70c6b5770440cbf02f2ca01a2f8804e05ddb77f66afdd23ed2584740c7f"
OFFICIAL_FW_LABEL="Official 1.4.3"
OFFICIAL_API="87.1"
# The firmware source commit of that release (tag 1.4.3). Host tests build the
# firmware's own FlipperFormat code and read its Sub-GHz test files from it
# (test/firmware/fetch.sh); they are downloaded for testing, never shipped.
OFFICIAL_SOURCE_COMMIT="8622f1a2b83d8f4918dd5fa3f43de963f6d6f819"
# The firmware's lib/mlib submodule (M*LIB, BSD-2-Clause), which its Sub-GHz
# receiver and keystore use: V0.6.0, whose m-core.h and m-array.h are the ones
# the 1.4.3 SDK ships. Fetched for the host decoder tests only.
OFFICIAL_MLIB_COMMIT="62c8ac3e5d4a7a4f8757328e7a80286fde2686b6"

# Unleashed firmware unlshd-093 (release channel).
UNLEASHED_SDK_URL="https://unleashedflip.com/fw/unlshd-093/flipper-z-f7-sdk-unlshd-093.zip"
UNLEASHED_SDK_SHA256="1eebdcf8cb1ffad4266bc084f872d2b79b0c26b98bc86162865e89aa1b375e66"
UNLEASHED_FW_LABEL="Unleashed unlshd-093"
UNLEASHED_API="88.9"

# RogueMaster publishes no standalone SDK zip, so the app is compiled inside
# the RogueMaster source tree with its own fbt, pinned to an exact commit.
ROGUEMASTER_REPO="https://github.com/RogueMaster/flipperzero-firmware-wPlugins"
ROGUEMASTER_REF="38d7ae9ae7eb2d25b31cea9b9fcf88fd11c1f3d3"
ROGUEMASTER_FW_LABEL="RogueMaster @ 38d7ae9"
ROGUEMASTER_API="88.16"
