## Summary

<!-- What does this change do, and why? Link related issues (e.g. "Closes #12"). -->

## Type of change

- [ ] Bug fix
- [ ] New feature
- [ ] Analysis engine / heuristics
- [ ] Build, CI or release tooling
- [ ] Documentation only

## How it was tested

- [ ] `make -C test check` passes (host unit tests and fuzz corpus, ASan/UBSan)
- [ ] `scripts/build_target.sh official` builds and verifies the .fap
- [ ] `python3 scripts/check_links.py` reports no broken links
- [ ] Tested on a physical Flipper Zero — firmware family/version: <!-- e.g. Official 1.4.3 -->
- [ ] Not tested on hardware (say so in the summary; CI cannot exercise the radio)

## Checklist

- [ ] Documentation updated where behaviour or UI text changed (`docs/`, `README.md`)
- [ ] `CHANGELOG.md` updated under "Unreleased"
- [ ] New analysis output is labelled `[CONFIRMED]`, `[OBSERVED]`, `[HEURISTIC]` or `[HYPOTHESIS]` honestly — nothing heuristic is presented as a verified decode
- [ ] No key recovery, rolling-code bypass, jamming, or weakening of the transmit safeguards
- [ ] No secrets, personal captures, or build output committed
