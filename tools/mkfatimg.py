#!/usr/bin/env python3
"""mkfatimg -- build a FAT16 image containing the given files (root dir only).

Self-contained (no mkfs/mtools). Written for two uses:
  * pre-load card decks into the firmware's FAT flash region:
        mkfatimg.py -o decks.img -s 3145728 decks/*.cap
        picotool load decks.img -o 0x10100000     # XIP base + FAT_FLASH_OFFSET
  * build the fixture image for test/host/test_fat.c.

Layout: 512-byte sectors, 1 sector/cluster (image sizes here give >4085
clusters => FAT16), 1 reserved sector, 2 FATs, 512 root entries. Long
filenames are emitted as VFAT LFN entries; 8.3 aliases use the NAME~N form.
Files are allocated contiguously.
"""

import argparse
import struct
import sys
from pathlib import Path

BS = 512
ROOT_ENTRIES = 512


def short_alias(name, used):
    stem, dot, ext = name.rpartition(".")
    if not dot:
        stem, ext = name, ""
    def clean(s, n):
        out = ""
        for c in s.upper():
            if c.isalnum() or c in "_-":
                out += c
            if len(out) == n:
                break
        return out
    base, ext = clean(stem, 8) or "X", clean(ext, 3)
    cand = (base + " " * 8)[:8] + (ext + " " * 3)[:3]
    n = 1
    while cand in used:
        tail = f"~{n}"
        cand = (base[:8 - len(tail)] + tail + " " * 8)[:8] + (ext + " " * 3)[:3]
        n += 1
    used.add(cand)
    return cand


def lfn_checksum(short11):
    s = 0
    for c in short11.encode("ascii"):
        s = (((s >> 1) | ((s & 1) << 7)) + c) & 0xFF
    return s


def lfn_entries(name, short11):
    """VFAT long-name entries, in on-disk order (last part first)."""
    csum = lfn_checksum(short11)
    codes = [ord(c) for c in name] + [0x0000]
    while len(codes) % 13:
        codes.append(0xFFFF)
    parts = [codes[i:i + 13] for i in range(0, len(codes), 13)]
    ents = []
    for i, part in enumerate(parts, 1):
        e = bytearray(32)
        e[0] = i | (0x40 if i == len(parts) else 0)
        e[11] = 0x0F
        e[13] = csum
        offs = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]
        for off, code in zip(offs, part):
            struct.pack_into("<H", e, off, code)
        ents.append(bytes(e))
    return list(reversed(ents))


def build(files, size, label="GE120 DECKS"):
    total = size // BS
    spc = 1
    reserved, nfats = 1, 2
    root_secs = ROOT_ENTRIES * 32 // BS
    # fatsz from a conservative cluster estimate, then fixed-point settle
    fatsz = (total * 2 + BS - 1) // BS
    for _ in range(8):
        data = total - reserved - nfats * fatsz - root_secs
        clusters = data // spc
        need = ((clusters + 2) * 2 + BS - 1) // BS
        if need == fatsz:
            break
        fatsz = need
    if clusters < 4085:
        sys.exit(f"image too small for FAT16 ({clusters} clusters); "
                 f"use a size >= ~2.2 MB")

    boot = bytearray(BS)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"MSDOS5.0"
    struct.pack_into("<H", boot, 11, BS)
    boot[13] = spc
    struct.pack_into("<H", boot, 14, reserved)
    boot[16] = nfats
    struct.pack_into("<H", boot, 17, ROOT_ENTRIES)
    if total < 0x10000:
        struct.pack_into("<H", boot, 19, total)
    else:
        struct.pack_into("<I", boot, 32, total)
    boot[21] = 0xF8
    struct.pack_into("<H", boot, 22, fatsz)
    struct.pack_into("<H", boot, 24, 63)      # geometry: don't-care
    struct.pack_into("<H", boot, 26, 255)
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0x47453132)
    boot[43:54] = (label + " " * 11)[:11].encode("ascii")
    boot[54:62] = b"FAT16   "
    boot[510:512] = b"\x55\xAA"

    fat = bytearray(fatsz * BS)
    fat[0:4] = b"\xF8\xFF\xFF\xFF"            # media + EOC for clusters 0,1

    root = bytearray(root_secs * BS)
    vol = bytearray(32)
    vol[0:11] = (label + " " * 11)[:11].encode("ascii")
    vol[11] = 0x08
    root[0:32] = vol
    root_off = 32

    data_img = bytearray()
    next_clus = 2
    used_aliases = set()

    for path in files:
        blob = path.read_bytes()
        nclus = max(1, (len(blob) + spc * BS - 1) // (spc * BS))
        first = next_clus
        for i in range(nclus):
            c = first + i
            nxt = 0xFFFF if i == nclus - 1 else c + 1
            struct.pack_into("<H", fat, c * 2, nxt)
        next_clus += nclus
        if next_clus - 2 > clusters:
            sys.exit(f"{path.name}: image full")

        name = path.name
        short = short_alias(name, used_aliases)
        for e in lfn_entries(name, short):
            root[root_off:root_off + 32] = e
            root_off += 32
        e = bytearray(32)
        e[0:11] = short.encode("ascii")
        e[11] = 0x20                          # archive
        struct.pack_into("<H", e, 26, first)
        struct.pack_into("<I", e, 28, len(blob))
        root[root_off:root_off + 32] = e
        root_off += 32
        if root_off > len(root):
            sys.exit("root directory full")

        data_img += blob + b"\x00" * (nclus * spc * BS - len(blob))
        print(f"  {name}: {len(blob)} B, clusters {first}..{next_clus - 1}")

    img = bytearray(size)
    img[0:BS] = boot
    off = reserved * BS
    for _ in range(nfats):
        img[off:off + len(fat)] = fat
        off += len(fat)
    img[off:off + len(root)] = root
    off += len(root)
    img[off:off + len(data_img)] = data_img
    return bytes(img)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("files", nargs="+", type=Path)
    ap.add_argument("-o", "--out", type=Path, required=True)
    ap.add_argument("-s", "--size", type=int, default=3 * 1024 * 1024,
                    help="image size in bytes (default 3 MiB = FAT region)")
    args = ap.parse_args()
    img = build(args.files, args.size)
    args.out.write_bytes(img)
    print(f"{args.out}: {len(img)} bytes")


if __name__ == "__main__":
    main()
