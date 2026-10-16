#!/usr/bin/env python3
"""Checks that a source folder or ZIP of the project contains no game data.

Usage: python3 scripts/check_package.py <folder|file.zip>
Exits with code 1 if anything suspicious is found.
"""
import os
import sys
import zipfile

FORBIDDEN_EXT = {
    ".bin", ".cue", ".iso", ".img", ".chd", ".pbp", ".ecm", ".ccd", ".sub", ".mdf", ".mds",
    ".trk", ".lsc", ".wve", ".av", ".psx", ".68", ".cnf", ".dat", ".exe_psx",
    ".mcd", ".mcr", ".srm", ".sav",
    ".png", ".bmp", ".jpg", ".jpeg", ".wav", ".mp4", ".avi", ".raw", ".vag",
}
FORBIDDEN_NAMES = {"slus_010.68", "system.cnf", "glbldata.psx", "dummy.dat"}
ALLOWED_BINARY = set()          # no binary file is expected in the source tree
MAX_TEXT = 512 * 1024


def is_text(data):
    if b"\0" in data:
        return False
    try:
        data.decode("utf-8")
        return True
    except UnicodeDecodeError:
        return False


def check(name, data, problems):
    low = name.lower().replace("\\", "/")
    base = low.rsplit("/", 1)[-1]
    ext = os.path.splitext(base)[1]
    if base in FORBIDDEN_NAMES:
        problems.append(f"{name}: file name of the game disc")
    if ext in FORBIDDEN_EXT:
        problems.append(f"{name}: forbidden extension ({ext})")
    if data[:8] == b"PS-X EXE":
        problems.append(f"{name}: PlayStation executable")
    if b"CD001" in data[:0x9400] and len(data) > 0x8000 and data[0x8001:0x8006] == b"CD001":
        problems.append(f"{name}: ISO9660 image")
    if data[:12] == bytes([0] + [0xFF] * 10 + [0]):
        problems.append(f"{name}: raw CD sector")
    if not is_text(data) and base not in ALLOWED_BINARY:
        problems.append(f"{name}: binary file ({len(data)} bytes)")
    elif len(data) > MAX_TEXT:
        problems.append(f"{name}: text file too large ({len(data)} bytes)")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    target = sys.argv[1]
    problems = []
    count = 0
    if zipfile.is_zipfile(target):
        with zipfile.ZipFile(target) as z:
            for info in z.infolist():
                if info.is_dir():
                    continue
                count += 1
                check(info.filename, z.read(info), problems)
    else:
        for root, dirs, files in os.walk(target):
            dirs[:] = [d for d in dirs if d not in (".git", "build", "build-win", "build-deb", "dist")]
            for f in files:
                path = os.path.join(root, f)
                count += 1
                with open(path, "rb") as fh:
                    check(os.path.relpath(path, target), fh.read(), problems)
    if problems:
        print("PROBLEMS FOUND:")
        for p in problems:
            print("  " + p)
        return 1
    print(f"OK: {count} files checked, no game data or binary file found.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
