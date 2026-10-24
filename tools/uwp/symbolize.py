#!/usr/bin/env python3
"""Names the socket.exe+0x... addresses in a crash_trace.log, using the engine's linker map.

Usage: python3 -I tools/uwp/symbolize.py <socket.map> <crash_trace.log>

The map comes with each CI engine build (artifact uwp-engine-map). Every "socket.exe+0xOFFSET" in the
log gets the function it falls in appended; other modules are left as they are.
"""

import bisect
import re
import sys


def demangle(name):
    # ?func@Class@ns@@... -> ns::Class::func (good enough to read a stack).
    if not name.startswith("?"):
        return name
    m = re.match(r"\?([^@]+)@([^@]*(?:@[^@]+)*?)@@", name)
    if not m:
        return name
    scopes = [s for s in m.group(2).split("@") if s and not s.startswith("?")]
    return "::".join(reversed(scopes)) + "::" + m.group(1) if scopes else m.group(1)


def load_map(path):
    base = 0x140000000
    symbols = []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"\s*Preferred load address is ([0-9a-fA-F]+)", line)
        if m:
            base = int(m.group(1), 16)
            continue
        m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s+f?\s", line)
        if m:
            address = int(m.group(2), 16)
            if address >= base:
                symbols.append((address - base, m.group(1)))
    symbols.sort()
    return [s[0] for s in symbols], [s[1] for s in symbols]


def main():
    offsets, names = load_map(sys.argv[1])

    def name_of(match):
        offset = int(match.group(1), 16)
        i = bisect.bisect_right(offsets, offset) - 1
        if i < 0:
            return match.group(0)
        return "%s  [%s+0x%x]" % (match.group(0), demangle(names[i]), offset - offsets[i])

    for line in open(sys.argv[2], encoding="utf-8", errors="replace"):
        print(re.sub(r"socket\.exe\+0x([0-9a-fA-F]+)", name_of, line.rstrip("\r\n")))


if __name__ == "__main__":
    main()
