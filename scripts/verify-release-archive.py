#!/usr/bin/env python3
"""Verify that a packaged release archive carries the built-in component library.

The release job packages the binaries by hand, outside CMake's install rules, so
this check opens the finished archive instead of trusting the source tree. It
requires the component_data/library directory, parses every *.json definition,
and requires each declared data file to be present. The part numbers are
printed, so the release log shows what a fresh download lists.

Exit 0 when the archive passes, 1 otherwise. Standard library only.

Usage:
  python3 scripts/verify-release-archive.py ARCHIVE --library-root ROOT [--min-parts N]
"""

from __future__ import annotations

import argparse
import json
import posixpath
import sys
import tarfile
import zipfile
from pathlib import Path, PurePosixPath


def normalize_name(name: str) -> str:
    """Drop the leading "./" that tar adds when archiving a directory."""
    while name.startswith("./"):
        name = name[2:]
    return name


class Archive:
    """Read file names and bytes from a .tar.gz, .tar, or .zip archive."""

    def __init__(self, path: Path) -> None:
        self._zip: zipfile.ZipFile | None = None
        self._tar: tarfile.TarFile | None = None
        self._tar_members: dict[str, tarfile.TarInfo] = {}
        self._names: list[str] = []
        if zipfile.is_zipfile(path):
            self._zip = zipfile.ZipFile(path)
            self._names = [
                normalize_name(info.filename)
                for info in self._zip.infolist()
                if not info.is_dir()
            ]
        else:
            self._tar = tarfile.open(path, "r:*")
            for member in self._tar.getmembers():
                if member.isfile():
                    name = normalize_name(member.name)
                    self._names.append(name)
                    self._tar_members[name] = member

    def __enter__(self) -> "Archive":
        return self

    def __exit__(self, *_exc: object) -> None:
        if self._zip is not None:
            self._zip.close()
        if self._tar is not None:
            self._tar.close()

    def names(self) -> list[str]:
        return self._names

    def read(self, name: str) -> bytes:
        if self._zip is not None:
            return self._zip.read(name)
        if self._tar is None:
            raise OSError(f"{name} is not in a readable archive")
        stream = self._tar.extractfile(self._tar_members[name])
        if stream is None:
            raise OSError(f"{name} has no readable data")
        return stream.read()


def definition_errors(name: str, document: object, archive_names: set[str],
                      library_prefix: str) -> tuple[list[str], tuple[str, str, str] | None]:
    """Validate one definition and return (errors, (part, type, manufacturer))."""
    if not isinstance(document, dict):
        return [f"{name} is not a JSON object"], None
    part_number = document.get("part_number")
    part_type = document.get("type")
    manufacturer = document.get("manufacturer", "")
    if not isinstance(part_number, str) or not part_number.strip():
        return [f"{name} has no part_number"], None
    if not isinstance(part_type, str) or not part_type.strip():
        return [f"{name} has no type"], None
    if not isinstance(manufacturer, str):
        manufacturer = ""

    errors: list[str] = []
    data_files = document.get("data_files", [])
    if not isinstance(data_files, list):
        errors.append(f"{name} has a non-list data_files entry")
        data_files = []
    for entry in data_files:
        if not isinstance(entry, dict):
            errors.append(f"{name} has a malformed data_files entry")
            continue
        relative = entry.get("path")
        if not isinstance(relative, str) or not relative:
            errors.append(f"{name} has a data_files entry without a path")
            continue
        relative = relative.replace("\\", "/")
        parts = PurePosixPath(relative).parts
        if PurePosixPath(relative).is_absolute() or any(part in (".", "..") for part in parts):
            errors.append(f"{name} names an unsafe data file path {relative}")
            continue
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), relative))
        if not resolved.startswith(library_prefix):
            errors.append(f"{name} names a data file outside the library: {relative}")
        elif resolved not in archive_names:
            errors.append(f"{name} names missing data file {relative}")

    return errors, (part_number.strip(), part_type.strip(), manufacturer.strip())


def verify(archive_path: Path, library_root: str, min_parts: int) -> list[str]:
    root = library_root.strip("/")
    prefix = root + "/"
    errors: list[str] = []
    parts: list[tuple[str, str, str]] = []

    try:
        with Archive(archive_path) as archive:
            names = archive.names()
            archive_names = set(names)
            json_names = sorted(
                name for name in names if name.startswith(prefix) and name.endswith(".json")
            )
            if not json_names:
                return [f"{archive_path} has no {root} definitions"]

            for name in json_names:
                try:
                    document = json.loads(archive.read(name))
                except ValueError:
                    errors.append(f"{name} is not valid JSON")
                    continue
                file_errors, part = definition_errors(name, document, archive_names, prefix)
                errors.extend(file_errors)
                if part is not None:
                    parts.append(part)
    except (OSError, tarfile.TarError, zipfile.BadZipFile) as error:
        return [f"{archive_path} cannot be read: {error}"]

    if len(json_names) < min_parts:
        errors.append(
            f"{archive_path} lists {len(json_names)} built-in definitions; "
            f"expected at least {min_parts}"
        )
    if errors:
        return errors

    for part_number, part_type, manufacturer in sorted(parts):
        suffix = f", {manufacturer}" if manufacturer else ""
        print(f"  {part_number} ({part_type}{suffix})")
    print(f"Verified {archive_path}: {len(json_names)} built-in component definitions")
    return []


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify that a packaged release archive carries its component library."
    )
    parser.add_argument("archive", type=Path, help="release .tar.gz, .tar, or .zip archive")
    parser.add_argument(
        "--library-root",
        required=True,
        help="archive-relative library directory, for example component_data/library",
    )
    parser.add_argument(
        "--min-parts",
        type=int,
        default=1,
        help="minimum number of definition files (default: 1)",
    )
    arguments = parser.parse_args()

    errors = verify(arguments.archive, arguments.library_root, arguments.min_parts)
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
