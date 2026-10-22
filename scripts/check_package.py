#!/usr/bin/env python3
"""Checks that a source folder or ZIP of the project contains no game data.

Usage: python3 scripts/check_package.py <folder|file.zip>
Exits with code 1 if anything suspicious is found.
"""
import hashlib
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
ALLOWED_BINARY = set()
PROJECT_IMAGES = {
    "packaging/icons/nrengine_headless.png": "a9756c2f0f8d8a9a767e41ae8c1cc70214d0edc7045bca39659acb72110a787b",
    "packaging/icons/nrlauncher.png": "804165717b8f43e5cc8612a5e22e7d888f2328c722af3dbd2ea5a068f2c50cfe",
    "packaging/icons/nrengine_headless.ico": "a4c3f10c09c8c0ad5a99ed1d771747aa0a577663e0db92794ffc17b71dcf179e",
    "packaging/icons/nrlauncher.ico": "f8db84b036987a7785cbb66344d4d0355faa1b2543a7c2fd0a7197fdc1e46b0f",
    "docs/images/launcher-general.png": "4c38ba6faa20664bf84ed394568bd2811e923dc9874c26d8e19734cc0d931ba8",
    "docs/images/launcher-graphics.png": "0a45be49c2ecf6ebb0ebfbb604c5bdd59044ea1c39b7e30178207361445522e7",
    "docs/images/launcher-effects.png": "b33ba3cb1048a8b96d44798990240f1ffe1147ac3cebfe43488b5f6cc7eb1619",
}
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
    for path, digest in PROJECT_IMAGES.items():
        if low == path or low.endswith("/" + path):
            if hashlib.sha256(data).hexdigest() == digest:
                return
            problems.append(f"{name}: project image changed; review it and update its SHA-256 in check_package.py")
            return
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
