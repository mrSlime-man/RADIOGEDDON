#pragma once

/**
 * RadioGeddon release version (semantic version, may carry a pre-release
 * suffix). Shown on the About screen.
 *
 * Must equal the release tag without its leading "v" (e.g. tag v1.0.0-beta.1).
 * application.fam's fap_version can only hold MAJOR.MINOR (a Flipper manifest
 * limit) and must match the first two components. Both rules are enforced by
 * .github/workflows/release.yml before anything is built or published.
 */
#define RADIOGEDDON_VERSION "1.0.0-beta.1"
