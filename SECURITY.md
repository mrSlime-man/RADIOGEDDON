# Security Policy

## Supported versions

RadioGeddon is in public beta. Security fixes are made on `main` and shipped in
the next release; only the latest release receives fixes.

| Version | Supported |
|---------|-----------|
| 1.0.0-beta.x (latest) | ✅ |
| Anything older | ❌ |

## Reporting a vulnerability

Please **do not open a public issue** for a vulnerability.

Report it privately through GitHub:
**[Security → Report a vulnerability](https://github.com/mrSlime-man/RADIOGEDDON/security/advisories/new)**.

If private reporting is unavailable for any reason, open an issue titled
"Security contact request" with **no technical details**, and the maintainer
will arrange a private channel.

Please include the affected version and firmware, what an attacker can achieve,
and steps or a sample file to reproduce. You can expect an acknowledgement
within 7 days. We will agree a disclosure timeline with you and credit you in
the release notes unless you prefer otherwise.

## In scope

- Memory-safety problems (crashes, out-of-bounds reads/writes, hangs) triggered
  by crafted `.sub` files placed on the SD card or by received radio data.
- Ways to make RadioGeddon transmit when it should refuse: outside the
  firmware's regional limits, a dynamic/rolling-code protocol, with a
  modulation preset it does not recognise, or through a custom preset that
  writes radio commands instead of settings.
- Analysis output that presents a guess as a confirmed decode in a way that
  could mislead a user about a device's security.
- Supply-chain problems in the build and release pipeline (for example a way to
  publish an artifact that was not built from the tagged source).

## Out of scope

- Weaknesses of the radio devices you analyse. A fixed-code remote being
  replayable is a property of that remote, not a RadioGeddon vulnerability.
- Vulnerabilities in Flipper Zero firmware (Official, Unleashed, RogueMaster) —
  please report those to the respective firmware project.
- Requests to add key recovery, rolling-code bypass, jamming or similar
  capabilities. These are deliberately excluded.

## Verifying release artifacts

Every release lists SHA-256 checksums and is built by GitHub Actions from the
tagged commit, with signed build-provenance attestations:

```bash
sha256sum -c SHA256SUMS
gh attestation verify radiogeddon-official.fap --repo mrSlime-man/RADIOGEDDON
```

## Responsible use

RadioGeddon is for education, research, and testing devices you own or are
explicitly authorized to test. Receiving and transmitting radio signals is
regulated; you are responsible for complying with the laws in your
jurisdiction. RadioGeddon itself does not decrypt payloads, recover keys, or
defeat rolling codes (it does display the firmware's decoder output, which may
use the SD-card keystore to identify KeeLoq-family signals), and its transmit
path keeps the firmware's regional restrictions in force and refuses decoded
rolling-code protocols.
