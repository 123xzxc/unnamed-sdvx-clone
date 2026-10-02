#!/usr/bin/env python3
"""Write a vcpkg manifest without the cpr dependency.

cpr/libcurl is the fragile part of the iOS dependency set and is only needed for
Internet Ranking and skin downloads. Dropping it lets the iOS build fall back to
the API-compatible cpr stub (see platform/ios/stubs/cpr) and keeps CI green when
libcurl fails to cross-compile for arm64-ios.

Usage: make_nohttp_manifest.py <source vcpkg.json> <target vcpkg.json>
"""

import json
import pathlib
import sys

DROP = "cpr"


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2

    source, target = map(pathlib.Path, sys.argv[1:3])
    manifest = json.loads(source.read_text())

    kept = []
    for entry in manifest["dependencies"]:
        name = entry if isinstance(entry, str) else entry.get("name")
        if name == DROP:
            continue
        kept.append(entry)
    manifest["dependencies"] = kept

    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(manifest, indent=4) + "\n")
    print("vcpkg dependencies:", [e if isinstance(e, str) else e.get("name") for e in kept])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())