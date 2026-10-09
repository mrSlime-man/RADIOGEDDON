#!/usr/bin/env python3
"""Verify the embedded manifest of a Flipper Zero .fap file.

A .fap is an ELF32/ARM object whose ``.fapmeta`` section holds a packed
``FlipperApplicationManifestV1`` (see lib/flipper_application/
application_manifest.h in the firmware SDK). The firmware refuses to load an
app whose API *major* version differs from its own, and an app built with a
newer API *minor* may import functions an older firmware does not export, so
every release artifact must carry exactly the API version of the SDK it was
built against. This script checks that, plus the
manifest magic, hardware target, application name and application version.

Standard library only, so it runs on any CI runner without extra installs.

Usage:
    verify_fap.py FAP --api 87.1 [--target 7] [--name RadioGeddon]
                      [--version 1.0] [--json]

Exit status is non-zero if the file is malformed or any expectation fails.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

FAP_MANIFEST_MAGIC = 0x52474448
FAP_MANIFEST_SUPPORTED_VERSION = 1
EM_ARM = 40
# FlipperApplicationManifestV1, #pragma pack(1):
#   u32 magic, u32 manifest_version, u16 api_minor, u16 api_major,
#   u16 hardware_target_id, u16 stack_size, u32 app_version,
#   char name[32], char has_icon, char icon[32]
MANIFEST_V1 = struct.Struct("<IIHHHHI32sc32s")


class FapError(Exception):
    pass


def read_section(data: bytes, wanted: str) -> bytes:
    if data[:4] != b"\x7fELF":
        raise FapError("not an ELF file")
    if data[4] != 1:
        raise FapError("not ELF32")
    if data[5] != 1:
        raise FapError("not little-endian")
    (e_machine,) = struct.unpack_from("<H", data, 18)
    if e_machine != EM_ARM:
        raise FapError(f"unexpected machine {e_machine} (want ARM={EM_ARM})")
    e_shoff, = struct.unpack_from("<I", data, 32)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 46)
    if e_shoff == 0 or e_shnum == 0:
        raise FapError("no section headers")

    def header(i: int):
        off = e_shoff + i * e_shentsize
        # name, type, flags, addr, offset, size
        return struct.unpack_from("<IIIIII", data, off)

    strtab = header(e_shstrndx)
    str_off, str_size = strtab[4], strtab[5]
    names = data[str_off : str_off + str_size]
    for i in range(e_shnum):
        sh_name, _t, _f, _a, sh_offset, sh_size = header(i)
        end = names.index(b"\0", sh_name)
        if names[sh_name:end].decode("ascii", "replace") == wanted:
            return data[sh_offset : sh_offset + sh_size]
    raise FapError(f"section {wanted} not found")


def parse_manifest(path: Path) -> dict:
    data = path.read_bytes()
    meta = read_section(data, ".fapmeta")
    if len(meta) < MANIFEST_V1.size:
        raise FapError(f".fapmeta too small ({len(meta)} < {MANIFEST_V1.size})")
    (
        magic,
        manifest_version,
        api_minor,
        api_major,
        target,
        stack_size,
        app_version,
        name,
        has_icon,
        _icon,
    ) = MANIFEST_V1.unpack_from(meta)
    return {
        "file": str(path),
        "size": len(data),
        "magic": magic,
        "manifest_version": manifest_version,
        "api": f"{api_major}.{api_minor}",
        "hardware_target": target,
        "stack_size": stack_size,
        "app_version": f"{app_version >> 16}.{app_version & 0xFFFF}",
        "name": name.split(b"\0", 1)[0].decode("utf-8", "replace"),
        "has_icon": has_icon != b"\0",
    }


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("fap", type=Path)
    ap.add_argument("--api", required=True, help="expected API version, e.g. 87.1")
    ap.add_argument("--target", type=int, default=7, help="hardware target id (f7=7)")
    ap.add_argument("--name", default=None, help="expected application name")
    ap.add_argument("--version", default=None, help="expected app version, e.g. 1.0")
    ap.add_argument("--json", action="store_true", help="print the manifest as JSON")
    args = ap.parse_args(argv)

    try:
        m = parse_manifest(args.fap)
    except (OSError, FapError, struct.error, ValueError) as exc:
        print(f"FAIL {args.fap}: {exc}", file=sys.stderr)
        return 2

    problems = []
    if m["magic"] != FAP_MANIFEST_MAGIC:
        problems.append(f"bad manifest magic 0x{m['magic']:08X}")
    if m["manifest_version"] != FAP_MANIFEST_SUPPORTED_VERSION:
        problems.append(f"unsupported manifest version {m['manifest_version']}")
    if m["api"] != args.api:
        problems.append(f"API {m['api']} != expected {args.api}")
    if m["hardware_target"] != args.target:
        problems.append(f"hardware target {m['hardware_target']} != {args.target}")
    if args.name is not None and m["name"] != args.name:
        problems.append(f"name {m['name']!r} != {args.name!r}")
    if args.version is not None and m["app_version"] != args.version:
        problems.append(f"app version {m['app_version']} != {args.version}")
    if not m["has_icon"]:
        problems.append("manifest has no launcher icon")

    if args.json:
        print(json.dumps(m, indent=2))
    if problems:
        for p in problems:
            print(f"FAIL {args.fap}: {p}", file=sys.stderr)
        return 1
    print(
        f"OK   {args.fap}: {m['name']} v{m['app_version']} "
        f"api={m['api']} target=f{m['hardware_target']} ({m['size']} bytes)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
