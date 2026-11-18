#!/usr/bin/env python3
"""Broken-link gate for the plain-Markdown user documentation.

Checks every Markdown file under the docs root (default: docs/):

  - relative link and image targets resolve to an existing file,
  - "#anchor" fragments on Markdown targets match a heading in that file,
    using GitHub's heading slug rules,
  - every page directly under the docs root is linked from README.md.

External URLs (http, https, mailto) are not checked. The script has no
dependencies beyond the Python standard library. Exits 0 when every check
passes and 1 otherwise, printing one line per problem.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from urllib.parse import unquote

# Inline link or image: [text](target "title") / ![alt](target "title")
LINK_RE = re.compile(r"\[[^\]\n]*\]\(\s*([^)\s]+)(?:\s+\"[^\"]*\")?\s*\)")
HEADING_RE = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
FENCE_RE = re.compile(r"^(```|~~~)")
EXTERNAL_RE = re.compile(r"^[a-zA-Z][a-zA-Z0-9+.-]*:")

CODE_SPAN_RE = re.compile(r"`([^`]*)`")
LINK_TEXT_RE = re.compile(r"\[([^\]]*)\]\([^)]*\)")
HTML_TAG_RE = re.compile(r"<[^>]+>")


def github_slug(heading: str) -> str:
    """Slug a heading the way GitHub does.

    Lowercase, drop everything that is not a letter, digit, underscore,
    hyphen, or space, then turn spaces into hyphens. Code spans, link
    syntax, and HTML tags keep only their text.
    """
    text = CODE_SPAN_RE.sub(r"\1", heading)
    text = LINK_TEXT_RE.sub(r"\1", text)
    text = HTML_TAG_RE.sub("", text)
    slug = text.strip().lower()
    slug = "".join(c for c in slug if c.isalnum() or c in "_- ")
    return slug.replace(" ", "-")


def heading_slugs(lines: list[str]) -> set[str]:
    """Collect all valid anchors for a file's headings, duplicates included."""
    slugs: set[str] = set()
    seen: dict[str, int] = {}
    in_fence = False
    for line in lines:
        if FENCE_RE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        match = HEADING_RE.match(line)
        if not match:
            continue
        slug = github_slug(match.group(2))
        if not slug:
            continue
        count = seen.get(slug, 0)
        seen[slug] = count + 1
        slugs.add(slug if count == 0 else f"{slug}-{count}")
    return slugs


def strip_fragment(target: str) -> tuple[str, str]:
    if "#" in target:
        path, fragment = target.split("#", 1)
        return path, unquote(fragment)
    return target, ""


def check(root: Path) -> list[str]:
    errors: list[str] = []
    md_files = sorted(root.rglob("*.md"))
    if not md_files:
        return [f"{root}: no Markdown files found"]

    readmes = [p for p in md_files if p.parent == root and p.name == "README.md"]
    if not readmes:
        errors.append(f"{root}: README.md index is missing")
    readme_links: set[Path] = set()

    for path in md_files:
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeDecodeError) as exc:
            errors.append(f"{path}: cannot read ({exc})")
            continue
        slugs = heading_slugs(lines)

        in_fence = False
        for lineno, line in enumerate(lines, 1):
            if FENCE_RE.match(line):
                in_fence = not in_fence
                continue
            if in_fence:
                continue
            for match in LINK_RE.finditer(line):
                raw = match.group(1)
                if EXTERNAL_RE.match(raw):
                    continue
                link_path, fragment = strip_fragment(raw)
                # A bare "#anchor" points at the current file.
                target = path.parent / unquote(link_path) if link_path else path
                if path.name == "README.md" and path.parent == root:
                    readme_links.add(target.resolve())
                if link_path:
                    resolved = (path.parent / unquote(link_path)).resolve()
                    if not resolved.exists():
                        errors.append(
                            f"{path.relative_to(root.parent)}:{lineno}: "
                            f"link target '{link_path}' not found"
                        )
                        continue
                    if resolved.is_dir():
                        if fragment:
                            errors.append(
                                f"{path.relative_to(root.parent)}:{lineno}: "
                                f"anchor '#{fragment}' on directory target '{link_path}'"
                            )
                        continue
                # Fragments are anchors only on Markdown targets; a bare
                # "#anchor" points at the current file.
                if fragment and target.suffix == ".md":
                    if not target.exists():
                        continue
                    try:
                        target_lines = target.read_text(encoding="utf-8").splitlines()
                    except (OSError, UnicodeDecodeError):
                        continue
                    if fragment not in heading_slugs(target_lines):
                        errors.append(
                            f"{path.relative_to(root.parent)}:{lineno}: "
                            f"anchor '#{fragment}' not found in "
                            f"{target.relative_to(root.parent)}"
                        )

    # AGENTS.md is the DOX contract for the folder, not a user page.
    for page in sorted(root.glob("*.md")):
        if page.name in ("README.md", "AGENTS.md"):
            continue
        if page.resolve() not in readme_links:
            errors.append(
                f"{page.relative_to(root.parent)}: "
                "page is not linked from README.md"
            )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", default="docs", type=Path)
    args = parser.parse_args()

    root = args.root
    if not root.is_dir():
        print(f"{root}: docs root is not a directory", file=sys.stderr)
        return 1

    errors = check(root)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        print(f"docs link check: FAILED ({len(errors)} problem(s))", file=sys.stderr)
        return 1
    pages = len(list(root.rglob("*.md")))
    print(f"docs link check: OK ({pages} Markdown files, all links resolve)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
