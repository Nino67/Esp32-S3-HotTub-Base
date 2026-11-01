#!/usr/bin/env python3
"""Stage the web assets for the LittleFS images.

HTML, JS and CSS files are gzip-compressed (as "<name>.gz", with a fixed timestamp so builds are
reproducible); everything else is copied unchanged. The web server prefers "<path>.gz" when it exists.

Usage: stage_www.py <source dir> <staging dir> <stamp file>
"""
import gzip
import shutil
import sys
from pathlib import Path

COMPRESSED_SUFFIXES = {".html", ".js", ".css"}


def stage(source: Path, staging: Path) -> None:
    if staging.exists():
        shutil.rmtree(staging)

    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        target = staging / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        if path.suffix in COMPRESSED_SUFFIXES:
            compressed = target.with_name(path.name + ".gz")
            with open(compressed, "wb") as raw:
                with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0) as archive:
                    archive.write(path.read_bytes())
        else:
            shutil.copy2(path, target)


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2

    source, staging, stamp = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    if not source.is_dir():
        print(f"Source directory not found: {source}", file=sys.stderr)
        return 1

    stage(source, staging)
    stamp.write_text("staged\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
