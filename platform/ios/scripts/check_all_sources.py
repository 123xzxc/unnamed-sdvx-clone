#!/usr/bin/env python3
"""
Syntax-checks every translation unit of an iOS build.

The Xcode build stops at the first translation unit that fails, which makes
fixing a port one CI run per error. CMake can export the exact command line used
for each source file (`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`); this script replays
all of them with `-fsyntax-only`, so every compile error of the whole project is
reported in a single pass.

    cmake -S . -B build-ios/diagnose -G "Unix Makefiles" \
          -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ...
    platform/ios/scripts/check_all_sources.py \
          build-ios/diagnose/compile_commands.json "$PWD"

The first argument is compile_commands.json, the second the repository root used
to shorten the reported paths (optional, defaults to the current directory).
Exits with 1 when any translation unit failed to compile.
"""

import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ERROR_LINE = re.compile(r"^(?P<file>[^:]+):(?P<line>\d+):(?:\d+:)? error: (?P<msg>.*)$")


def syntax_check(entry):
    """Replays a compile command with -fsyntax-only and returns its stderr."""
    args = shlex.split(entry["command"])
    command = []
    skip_next = False
    for arg in args:
        if skip_next:
            skip_next = False
        elif arg == "-c":
            continue
        elif arg == "-o":
            skip_next = True
        else:
            command.append(arg)
    command.append("-fsyntax-only")

    result = subprocess.run(command, cwd=entry["directory"], capture_output=True, text=True)
    return entry["file"], result.stderr


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    database = sys.argv[1]
    repo = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.getcwd()

    with open(database) as handle:
        entries = json.load(handle)

    workers = min(8, (os.cpu_count() or 2) * 2)
    with ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(syntax_check, entries))

    errors, failed_files = set(), set()
    for source, output in results:
        for line in output.splitlines():
            match = ERROR_LINE.match(line)
            if not match:
                continue
            path = os.path.relpath(match.group("file"), repo)
            errors.add(f"{path}:{match.group('line')}: {match.group('msg')}")
            failed_files.add(path)

    print(f"checked {len(entries)} translation units, {len(failed_files)} failed")
    print("=== unique compile errors ===")
    for error in sorted(errors):
        print(error)

    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
