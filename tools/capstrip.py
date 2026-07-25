#!/usr/bin/env python3
"""capstrip -- reduce a Burroughs-reader .cap capture to its hex section.

A .cap capture (see gemu/docs/punchcards.md section 2) contains two dumps of
the same deck: a hex section ("Card n. K" + 80 four-hex-digit column values)
and a visual hole-art section ("Card n. K" + 12 rows of '*'/'_'), wrapped in
console chatter (boot banner, FEED ON/OFF, per-card feed counters, "Total
cards: N" trailers).

This tool keeps exactly what the loaders consume -- the hex section -- and
drops everything else, matching gemu cap.c semantics: a card is a "Card n."
header followed by strict 4-hex-digit whitespace-separated tokens; cards with
no hex tokens (the visual section's) are discarded.

Output format, parseable by gemu's cap_load() and by the Pico firmware:

    Card n. 1
    XXXX XXXX ... (80 tokens)
    ...
    Total cards: N

Usage:
    capstrip.py deck.cap [deck2.cap ...] [-o OUTDIR] [--suffix SUF] [--check]

With --check the tool also re-parses its own output and verifies the column
values match the input's hex section exactly (count and content).
"""

import argparse
import re
import sys
from pathlib import Path

CARD_RE = re.compile(r"\s*Card n\.\s*(\d+)\s*$")
HEX_TOKEN_RE = re.compile(r"^[0-9A-Fa-f]{4}$")


def parse_cap(text):
    """Return list of (card_number, [column values]) for cards with hex data.

    Mirrors gemu cap.c cap_load(): every 'Card n.' header opens a card; only
    strict 4-hex-digit tokens are collected; other lines are ignored. Cards
    that end up with zero columns (the visual-section duplicates) are dropped.
    """
    cards = []
    current = None  # (number, [cols])
    for line in text.splitlines():
        m = CARD_RE.match(line)
        if m:
            if current and current[1]:
                cards.append(current)
            current = (int(m.group(1)), [])
            continue
        if current is None:
            continue
        for tok in line.split():
            if HEX_TOKEN_RE.match(tok):
                current[1].append(int(tok, 16) & 0x1FFF)
            else:
                # a non-hex token ends nothing in cap.c; it is just skipped
                pass
    if current and current[1]:
        cards.append(current)
    return cards


def emit(cards):
    out = []
    for num, cols in cards:
        out.append(f"Card n. {num}")
        out.append("".join(f"{v:04X} " for v in cols))
    out.append(f"Total cards: {len(cards)}")
    return "\n".join(out) + "\n"


def strip_file(path, outdir, suffix, check):
    src = path.read_text(errors="replace")
    cards = parse_cap(src)
    if not cards:
        print(f"{path}: no hex cards found", file=sys.stderr)
        return 1

    short = [(n, len(c)) for n, c in cards if len(c) != 80]
    reduced = emit(cards)

    out_path = (outdir or path.parent) / (path.stem + suffix + ".cap")
    out_path.write_text(reduced)

    ratio = 100.0 * len(reduced) / max(1, len(src))
    print(f"{path.name}: {len(cards)} cards -> {out_path.name} "
          f"({len(src)} -> {len(reduced)} bytes, {ratio:.1f}%)")
    if short:
        for n, ncols in short:
            print(f"  warning: card {n} has {ncols} columns (expected 80)",
                  file=sys.stderr)

    if check:
        recards = parse_cap(reduced)
        if [(n, c) for n, c in recards] != [(n, c) for n, c in cards]:
            print(f"{path}: CHECK FAILED -- reduced file does not round-trip",
                  file=sys.stderr)
            return 1
        print(f"  check ok: {len(recards)} cards, "
              f"{sum(len(c) for _, c in recards)} columns round-trip")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("caps", nargs="+", type=Path, help=".cap input files")
    ap.add_argument("-o", "--outdir", type=Path, default=None,
                    help="output directory (default: alongside input)")
    ap.add_argument("--suffix", default=".hex",
                    help="suffix inserted before .cap (default: .hex)")
    ap.add_argument("--check", action="store_true",
                    help="re-parse output and verify against input")
    args = ap.parse_args()

    if args.outdir:
        args.outdir.mkdir(parents=True, exist_ok=True)
    rc = 0
    for p in args.caps:
        rc |= strip_file(p, args.outdir, args.suffix, args.check)
    return rc


if __name__ == "__main__":
    sys.exit(main())
