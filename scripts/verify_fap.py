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

Modules (plugins packed into the .fap's ``.fapassets`` section, the Full
edition's optional tools) are unpacked and checked the same way: present,
same API version and target, and every import exported by the SDK.

Usage:
    verify_fap.py FAP --api 87.1 [--target 7] [--name RadioGeddon]
                      [--version 1.0] [--symbols api_symbols.csv]
                      [--modules plugins/a.fal,plugins/b.fal] [--json]

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


def section_headers(data: bytes):
    """(name, type, offset, size, link) of every ELF32 section."""
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
        # name, type, flags, addr, offset, size, link
        return struct.unpack_from("<IIIIIII", data, off)

    strtab = header(e_shstrndx)
    str_off, str_size = strtab[4], strtab[5]
    names = data[str_off : str_off + str_size]
    result = []
    for i in range(e_shnum):
        sh_name, sh_type, _f, _a, sh_offset, sh_size, sh_link = header(i)
        end = names.index(b"\0", sh_name)
        name = names[sh_name:end].decode("ascii", "replace")
        result.append((name, sh_type, sh_offset, sh_size, sh_link))
    return result


def read_section(data: bytes, wanted: str) -> bytes:
    for name, _type, offset, size, _link in section_headers(data):
        if name == wanted:
            return data[offset : offset + size]
    raise FapError(f"section {wanted} not found")


SHT_SYMTAB = 2
ELF32_SYM = struct.Struct("<IIIBBH")


def imported_symbols(data: bytes) -> set:
    """Names of the undefined (imported) symbols of an ELF32 object."""
    sections = section_headers(data)
    symtabs = [s for s in sections if s[1] == SHT_SYMTAB]
    if not symtabs:
        raise FapError("no symbol table")
    result = set()
    for _name, _type, offset, size, link in symtabs:
        str_off, str_size = sections[link][2], sections[link][3]
        strings = data[str_off : str_off + str_size]
        for pos in range(offset, offset + size - ELF32_SYM.size + 1, ELF32_SYM.size):
            st_name, _value, _size, _info, _other, st_shndx = ELF32_SYM.unpack_from(data, pos)
            if st_shndx != 0 or st_name == 0:
                continue
            end = strings.index(b"\0", st_name)
            result.add(strings[st_name:end].decode("ascii", "replace"))
    return result


def exported_symbols(csv_path: Path) -> set:
    """Functions and variables an SDK's api_symbols.csv exports ('+')."""
    result = set()
    for line in csv_path.read_text(encoding="utf-8").splitlines():
        parts = line.split(",")
        if len(parts) >= 3 and parts[0] in ("Function", "Variable") and parts[1] == "+":
            result.add(parts[2])
    if not result:
        raise FapError(f"{csv_path}: no exported symbols")
    return result


ASSETS_MAGIC = 0x4F4C5A44
ASSETS_VERSION = 1


def embedded_assets(data: bytes) -> dict:
    """Files packed in the ``.fapassets`` section ({path: bytes}); {} if none.

    Layout (lib/flipper_application/application_assets.c): header (magic,
    version, dirs, files: u32 each); signature (u32 length + bytes); each
    directory (u32 length + name); each file (u32 length + name, u32 size,
    contents).
    """
    try:
        blob = read_section(data, ".fapassets")
    except FapError:
        return {}
    magic, version, dirs, files = struct.unpack_from("<IIII", blob, 0)
    if magic != ASSETS_MAGIC or version != ASSETS_VERSION:
        raise FapError(f"bad assets header 0x{magic:08X} v{version}")
    pos = 16

    def chunk():
        nonlocal pos
        (length,) = struct.unpack_from("<I", blob, pos)
        start = pos + 4
        pos = start + length
        if pos > len(blob):
            raise FapError("assets section truncated")
        return blob[start:pos]

    chunk()  # signature
    for _ in range(dirs):
        chunk()
    result = {}
    for _ in range(files):
        name = chunk().split(b"\0", 1)[0].decode("utf-8", "replace")
        (size,) = struct.unpack_from("<I", blob, pos)
        pos += 4
        if pos + size > len(blob):
            raise FapError(f"asset {name} truncated")
        result[name] = blob[pos : pos + size]
        pos += size
    return result


def parse_manifest(path: Path) -> dict:
    data = path.read_bytes()
    return parse_manifest_bytes(data, str(path))


def parse_manifest_bytes(data: bytes, label: str) -> dict:
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
        "file": label,
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
    ap.add_argument("--symbols", type=Path, default=None, help="SDK api_symbols.csv every import must be in")
    ap.add_argument(
        "--modules",
        default=None,
        help="comma-separated asset paths of modules (.fal) that must be packed in the .fap",
    )
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
    if args.symbols is not None:
        try:
            imports = imported_symbols(args.fap.read_bytes())
            missing = sorted(imports - exported_symbols(args.symbols))
        except (OSError, FapError, struct.error, ValueError) as exc:
            problems.append(f"symbol check: {exc}")
        else:
            m["imports"] = len(imports)
            for name in missing:
                problems.append(f"imports {name}, which {args.symbols} does not export")

    if args.modules:
        try:
            assets = embedded_assets(args.fap.read_bytes())
        except (OSError, FapError, struct.error, ValueError) as exc:
            problems.append(f"assets: {exc}")
            assets = {}
        exported = exported_symbols(args.symbols) if args.symbols is not None else None
        m["modules"] = []
        for name in [n for n in args.modules.split(",") if n]:
            blob = assets.get(name)
            if blob is None:
                problems.append(f"module {name} is not packed in the .fap")
                continue
            try:
                mm = parse_manifest_bytes(blob, name)
                if mm["api"] != args.api:
                    problems.append(f"module {name}: API {mm['api']} != expected {args.api}")
                if mm["hardware_target"] != args.target:
                    problems.append(f"module {name}: hardware target {mm['hardware_target']}")
                if exported is not None:
                    imports = imported_symbols(blob)
                    for sym in sorted(imports - exported):
                        problems.append(f"module {name} imports {sym}, which the SDK does not export")
                    mm["imports"] = len(imports)
                m["modules"].append(mm)
            except (FapError, struct.error, ValueError) as exc:
                problems.append(f"module {name}: {exc}")

    if args.json:
        print(json.dumps(m, indent=2))
    if problems:
        for p in problems:
            print(f"FAIL {args.fap}: {p}", file=sys.stderr)
        return 1
    print(
        f"OK   {args.fap}: {m['name']} v{m['app_version']} "
        f"api={m['api']} target=f{m['hardware_target']} ({m['size']} bytes)"
        + (f", {m['imports']} imports resolved" if "imports" in m else "")
        + (f", {len(m['modules'])} modules verified" if m.get("modules") else "")
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
