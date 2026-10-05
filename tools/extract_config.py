#!/usr/bin/env python3
"""Extract the ChicagoHS sensor config template from your own Windows Goodix driver (gfspi.dll).

The template is Goodix data and is not shipped with this project. The Windows driver that came with
the laptop contains it; this script finds it by structure and writes the 224-byte blob the libfprint
gxfp driver loads (default /var/lib/fprintd/gxfp/chicagohs_cfg.bin).

usage: extract-chicagohs-config.py <gfspi.dll | mounted Windows partition> [-o out.bin]
"""
import argparse
import glob
import hashlib
import os
import struct
import sys

CFG_LEN = 0xE0
# Template used and tested (gfspi.dll 1.1.18.25, 2024-02-27). Other driver versions may differ.
KNOWN_SHA256 = "1ef7beb5a7fb9d051289d5ee7f70eceaa831bab20b0759855cdf5c7e043df77d"
DAC_REGS = (0x220, 0x236, 0x238, 0x23A)


def find_dll(path):
    if os.path.isfile(path):
        return path
    hits = glob.glob(os.path.join(path, "Windows/System32/DriverStore/FileRepository/gfspi.inf_amd64_*/gfspi.dll"))
    if not hits:
        sys.exit(f"no gfspi.dll under {path} (expected Windows/System32/DriverStore/FileRepository/gfspi.inf_amd64_*/)")
    return sorted(hits, key=os.path.getmtime)[-1]


def config_regs(blob):
    """Registers listed in a Goodix 0xe0 config: 8 sections of (offset, length) at bytes 1..16, then reg/value pairs."""
    sections = [(blob[1 + i * 2], blob[2 + i * 2]) for i in range(8)]
    offsets = [o for o, _ in sections]
    if offsets[0] != 0x11 or offsets != sorted(offsets):
        return None
    regs = []
    for off, length in sections:
        if length % 4 or off + length > CFG_LEN:
            return None
        regs += [struct.unpack_from("<H", blob, off + step)[0] for step in range(0, length, 4)]
    return regs


def candidates(data):
    for i in range(len(data) - CFG_LEN):
        if data[i + 1] != 0x11:
            continue
        blob = data[i:i + CFG_LEN]
        regs = config_regs(blob)
        if regs and all(r in regs for r in DAC_REGS):
            yield i, blob


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="gfspi.dll, or the mount point of the Windows partition")
    ap.add_argument("-o", "--out", default="chicagohs_cfg.bin")
    args = ap.parse_args()

    dll = find_dll(args.source)
    data = open(dll, "rb").read()
    found = list(candidates(data))
    # Chicago-family templates carry all four DAC registers; the ChicagoHS one starts with 0x70.
    chosen = [(off, b) for off, b in found if b[0] == 0x70]
    print(f"{dll}: {len(found)} Chicago-family template(s) at " + ", ".join(hex(o) for o, _ in found))
    if len(chosen) != 1:
        sys.exit(f"expected exactly one ChicagoHS template (first byte 0x70), found {len(chosen)}; not writing")

    off, blob = chosen[0]
    digest = hashlib.sha256(blob).hexdigest()
    with open(args.out, "wb") as f:
        f.write(blob)
    print(f"wrote {args.out} (offset {off:#x}, sha256 {digest})")
    if digest != KNOWN_SHA256:
        print("NOTE: differs from the tested template (gfspi.dll 1.1.18.25); it may still work, but it is untested")


if __name__ == "__main__":
    main()
