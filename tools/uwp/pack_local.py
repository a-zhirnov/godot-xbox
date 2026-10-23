#!/usr/bin/env python3
"""Build and sign an MSIX package on macOS/Linux (no Windows SDK needed).

Usage:
  pack_local.py --out Socket.msix --manifest AppxManifest.xml --exe socket.exe [--exe other.exe ...]
                [--pck game.pck] --cert cert.pem --key key.pem [--version 0.3.2.7]

The manifest is a template with @PACKAGE_NAME@, @PUBLISHER@, @VERSION@, @DISPLAY_NAME@ and
@PUBLISHER_DISPLAY_NAME@ placeholders. Files are stored uncompressed, which keeps the block map trivial.
Signing is done with osslsigncode (brew install osslsigncode).
"""

import argparse
import base64
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
from xml.sax.saxutils import quoteattr

BLOCK = 65536
SHA256 = "http://www.w3.org/2001/04/xmlenc#sha256"


def png(width: int, height: int, rgba=(16, 17, 20, 255)) -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    row = b"\x00" + bytes(rgba) * width
    raw = row * height
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


ASSETS = {
    "Assets/StoreLogo.png": (50, 50),
    "Assets/Square44x44Logo.png": (44, 44),
    "Assets/Square150x150Logo.png": (150, 150),
    "Assets/Wide310x150Logo.png": (310, 150),
    "Assets/SplashScreen.png": (620, 300),
}


def content_types(names):
    exts = sorted({os.path.splitext(n)[1][1:].lower() for n in names if os.path.splitext(n)[1]})
    mime = {"png": "image/png", "exe": "application/x-msdownload", "dll": "application/x-msdownload", "pck": "application/octet-stream", "xml": "application/xml"}
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">']
    for e in exts:
        lines.append(f'<Default Extension="{e}" ContentType="{mime.get(e, "application/octet-stream")}"/>')
    lines.append('<Override PartName="/AppxManifest.xml" ContentType="application/vnd.ms-appx.manifest+xml"/>')
    lines.append('<Override PartName="/AppxBlockMap.xml" ContentType="application/vnd.ms-appx.blockmap+xml"/>')
    lines.append('<Override PartName="/AppxSignature.p7x" ContentType="application/vnd.ms-appx.signature"/>')
    lines.append("</Types>")
    return "\n".join(lines).encode()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--exe", action="append", default=[])
    ap.add_argument("--pck")
    ap.add_argument("--cert", required=True)
    ap.add_argument("--key", required=True)
    ap.add_argument("--version", default="0.3.2.1")
    ap.add_argument("--name", default="Zhirnov.Socket")
    ap.add_argument("--publisher", default="CN=Zhirnov Studio")
    args = ap.parse_args()

    manifest = open(args.manifest, encoding="utf-8").read()
    for key, value in {
        "@PACKAGE_NAME@": args.name,
        "@PUBLISHER@": args.publisher,
        "@VERSION@": args.version,
        "@DISPLAY_NAME@": "Socket",
        "@PUBLISHER_DISPLAY_NAME@": "Zhirnov Studio",
    }.items():
        manifest = manifest.replace(key, value)

    files = {"AppxManifest.xml": manifest.encode("utf-8")}
    for name, (w, h) in ASSETS.items():
        files[name] = png(w, h)
    for exe in args.exe:
        files[os.path.basename(exe)] = open(exe, "rb").read()
    if args.pck:
        files["game.pck"] = open(args.pck, "rb").read()

    names = list(files) + ["AppxBlockMap.xml", "[Content_Types].xml"]
    ctypes = content_types(names)

    # Write the payload first so the local header sizes are known for the block map.
    unsigned = args.out + ".unsigned"
    with zipfile.ZipFile(unsigned, "w", zipfile.ZIP_STORED) as z:
        for name, data in files.items():
            z.writestr(zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0)), data)
    lfh = {}
    with zipfile.ZipFile(unsigned) as z, open(unsigned, "rb") as raw:
        for info in z.infolist():
            raw.seek(info.header_offset)
            header = raw.read(30)
            name_len, extra_len = struct.unpack("<HH", header[26:30])
            lfh[info.filename] = 30 + name_len + extra_len

    blockmap = ['<?xml version="1.0" encoding="UTF-8"?>', f'<BlockMap xmlns="http://schemas.microsoft.com/appx/2010/blockmap" HashMethod="{SHA256}">']
    for name, data in files.items():
        blockmap.append(f"<File Name={quoteattr(name.replace('/', chr(92)))} Size=\"{len(data)}\" LfhSize=\"{lfh[name]}\">")
        for i in range(0, max(len(data), 1), BLOCK):
            digest = base64.b64encode(hashlib.sha256(data[i : i + BLOCK]).digest()).decode()
            blockmap.append(f'<Block Hash="{digest}"/>')
        blockmap.append("</File>")
    blockmap.append("</BlockMap>")

    with zipfile.ZipFile(unsigned, "a", zipfile.ZIP_STORED) as z:
        z.writestr(zipfile.ZipInfo("AppxBlockMap.xml", date_time=(2026, 1, 1, 0, 0, 0)), "\n".join(blockmap).encode())
        z.writestr(zipfile.ZipInfo("[Content_Types].xml", date_time=(2026, 1, 1, 0, 0, 0)), ctypes)

    subprocess.run(
        ["osslsigncode", "sign", "-certs", args.cert, "-key", args.key, "-h", "sha256", "-in", unsigned, "-out", args.out],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    os.remove(unsigned)
    print(f"built {args.out} ({os.path.getsize(args.out) / 1e6:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
