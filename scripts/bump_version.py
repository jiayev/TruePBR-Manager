#!/usr/bin/env python3
"""Bump the project version in CMakeLists.txt and vcpkg.json simultaneously."""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

CMAKE_FILE = ROOT / "CMakeLists.txt"
VCPKG_FILE = ROOT / "vcpkg.json"

VERSION_RE = re.compile(
    r"(project\(TruePBR-Manager\s+VERSION\s+)\d+\.\d+\.\d+"
)


def bump(new_version: str) -> None:
    # Validate format
    if not re.fullmatch(r"\d+\.\d+\.\d+", new_version):
        print(f"Error: '{new_version}' is not a valid semver (X.Y.Z)", file=sys.stderr)
        sys.exit(1)

    # --- CMakeLists.txt ---
    cmake_text = CMAKE_FILE.read_text(encoding="utf-8")
    cmake_text_new, n = VERSION_RE.subn(rf"\g<1>{new_version}", cmake_text)
    if n != 1:
        print("Error: could not find version in CMakeLists.txt", file=sys.stderr)
        sys.exit(1)
    CMAKE_FILE.write_text(cmake_text_new, encoding="utf-8")
    print(f"CMakeLists.txt -> {new_version}")

    # --- vcpkg.json ---
    vcpkg = json.loads(VCPKG_FILE.read_text(encoding="utf-8"))
    vcpkg["version-string"] = new_version
    VCPKG_FILE.write_text(
        json.dumps(vcpkg, indent=4, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(f"vcpkg.json     -> {new_version}")

    print(f"\nDone. Commit and push to master to auto-create tag v{new_version}.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Bump project version")
    parser.add_argument("version", help="New version in X.Y.Z format")
    args = parser.parse_args()
    bump(args.version)
