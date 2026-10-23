#!/usr/bin/env python3
"""Make a prebuilt /MT static library linkable into a /MD (UWP) binary.

Usage: patch_static_crt_lib.py <lib> [<lib> ...]

MSVC objects carry linker directives in their .drectve sections: a /FAILIFMISMATCH on the
runtime library and /DEFAULTLIB entries for the static CRT. They are plain text, so they are
blanked out in place (same length, spaces), leaving the code to bind to the dynamic CRT that
the rest of the binary uses. Used for the prebuilt Mesa NIR library, which is only shipped /MT.
"""

import re
import sys

PATTERNS = [
    rb'[/-]FAILIFMISMATCH:"?RuntimeLibrary=MT_StaticRelease"?',
    rb'[/-]DEFAULTLIB:"?(?:LIBCMT|libcmt|libcpmt|LIBCPMT|libucrt|libvcruntime)(?:\.lib)?"?',
]


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    for path in sys.argv[1:]:
        data = open(path, "rb").read()
        total = 0
        for pattern in PATTERNS:
            data, count = re.subn(pattern, lambda m: b" " * len(m.group(0)), data)
            total += count
        open(path, "wb").write(data)
        print(f"{path}: blanked {total} directives")
    return 0


if __name__ == "__main__":
    sys.exit(main())
