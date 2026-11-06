#!/usr/bin/env python3
"""Extract the string tables from Comet Crash's language*.utf files.

Format (from language1.utf, the English table): plain text. Each string
is a block of one or more lines preceded by a separator line containing
exactly "****". The game's local_text.cpp loads these by index, so the
index of each string is part of the format — the extractor preserves it.

Usage: extract_language.py <USRDIR/data dir> <out dir>
Writes language<N>.txt for every language file found, one string per
line as: <index>\\t<string with newlines escaped as \\n>.
"""

import glob
import os
import sys


def parse(path: str) -> list[str]:
    raw = open(path, "rb").read()
    # English is plain ASCII; other tables may be UTF-8. Fall back to
    # latin-1 so no byte is ever lost silently (it round-trips 1:1).
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        text = raw.decode("latin-1")
    strings: list[str] = []
    current: list[str] = []
    for line in text.split("\n"):
        if line.strip() == "****":
            strings.append("\n".join(current).strip("\n"))
            current = []
        else:
            current.append(line)
    strings.append("\n".join(current).strip("\n"))
    # The file starts with a separator, so drop the leading empty block.
    if strings and strings[0] == "":
        strings = strings[1:]
    return strings


def main() -> int:
    data_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    for path in sorted(glob.glob(os.path.join(data_dir, "language*.utf"))):
        name = os.path.splitext(os.path.basename(path))[0]
        strings = parse(path)
        with open(os.path.join(out_dir, name + ".txt"), "w", encoding="utf-8") as f:
            for i, s in enumerate(strings):
                f.write(f"{i}\t{s.replace(chr(10), chr(92) + 'n')}\n")
        print(f"{name}: {len(strings)} strings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
