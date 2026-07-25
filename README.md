# rpi-pico-card-reader — GE-120 punched-card reader simulator

Copyright (C) 2026 Verde Binario

This program is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, version 3. This program is distributed in the hope
that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
[COPYING](COPYING) for the full text.

Raspberry Pi Pico 2 W firmware that stands in for the GE-120's card reader
(LS 600 / GIS 450 controller). It plugs into the CPU backplane at the three
connector-2 COCA slots (I1 / L1 / M1), holds the Site Acceptance Test decks
(reduced `.cap` files) on a USB-accessible flash drive, and answers the
machine's own `CLEAR → LOAD1 → LOAD → START` bootstrap so real test programs
load into core memory.

Status: **step 2 of 6** (see ARCHITECTURE.md §11). Working now: USB composite
device (CDC-ACM console + MSC FAT drive, writable while disarmed), read-only
FAT12/16 driver with long filenames, `.cap`/batch → RAM deck path, tunable
config persisted in flash, event-ring tracing. Still stubbed: the wire engine
(PIO presenter feed, per-command presentation — steps 3-4) and the on-machine
bring-up (steps 5-6).

## Documents

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — signals, LOAD sequence,
  command–response matrix, presentation timing, dual-core/PIO/IRQ design,
  storage, console, OPEN items, bring-up plan.
- [docs/PINOUT.md](docs/PINOUT.md) — GP ↔ level-shifter ↔ COCA slot·pin
  tables (atlas-verified), straps, corrections to older drafts.

## Layout

```
CMakeLists.txt           pico-sdk build (PICO_BOARD=pico2_w)
pio/                     PIO programs: presenter (LU bus + strobes),
                         RE-byte capture on TU00N
include/, src/           firmware (core0 = USB/console, core1 = wire)
tools/capstrip.py        reduce a .cap capture to its hex section (~29% size)
tools/mkfatimg.py        build a FAT16 image of decks (preload w/o MSC)
test/host/               host-built unit tests (no Pico needed)
```

## Using it (step-2 functionality)

Copy decks onto the USB drive ("GE-120 decks", writable only while
disarmed), or preload them at flash time:

```sh
python3 tools/capstrip.py ../gemu/Site_Acceptance_Test/*.cap -o decks --check
python3 tools/mkfatimg.py -o decks.img decks/*.cap        # 3 MiB image
picotool load decks.img -o 0x10100000                     # FAT region
```

Then on the console (`/dev/ttyACM0`, any baud):

```
ls                          decks on the drive
batches                     built-in SAT batch recipes (deck surgery)
arm funktionalcpu.cap       parse + surgery -> RAM, wait for the GE
arm ls600-controller-sat    or arm a batch; add --raw for box order
status | trace on           watch the wire (RE bytes, TU strobes; also a
                            PINS line with every input's standing level
                            each 500 ms -- active-high logic)
set w 35 | save             tune LU08N width etc. (100 ns ticks), persist
```

The output shifters are enabled by the hardware JP-OE jumper, not from the
console: jumper open = tri-stated/passive; close it to drive the backplane
(DANGER -- only after passive capture).

## Quick start (host side, today)

```sh
# Reduce the SAT decks for the flash drive:
python3 tools/capstrip.py ../gemu/Site_Acceptance_Test/*.cap -o decks --check

# Host unit tests:
make -C test/host

# Firmware (pico-sdk lives at ~/src/pico-sdk on this machine; builds a
# flashable .uf2 -- USB enumerates CDC+MSC, wire engine still stubbed):
cmake -B build -DPICO_SDK_PATH=$HOME/src/pico-sdk \
      -Dpioasm_DIR=$PWD/build-tools/pioasm-install/lib/cmake/pioasm
cmake --build build -j8      # -> build/ge120_cardreader.uf2
```

### pioasm vs GCC 15

pico-sdk 2.1.1's bundled `pioasm` fails to build with host GCC 15
(`pio_types.h` uses `uint8_t` without `<cstdint>`). Workaround, done once —
build pioasm standalone with the include injected, then pass `-Dpioasm_DIR`
as above:

```sh
cmake -S ~/src/pico-sdk/tools/pioasm -B build-tools/pioasm-build \
      -DCMAKE_CXX_FLAGS="-include cstdint" \
      -DCMAKE_INSTALL_PREFIX=$PWD/build-tools/pioasm-install
cmake --build build-tools/pioasm-build -j8 --target install
```

## Safety rules (real machine)

- The board is **USB-powered only** — never draw from the backplane +5.2 V.
- Single ground reference: ZERO1 (pin 05 of each COCA slot).
- First session on the machine is **passive**: output shifters held in
  tri-state (JP-OE jumper open, OE# pulled up), only logging RE / TU00N /
  TU03N during a LOAD attempt. Drive nothing until the captured command
  stream confirms the protocol (OPEN items 1 and 3 in ARCHITECTURE.md).
- Level-shifter part code TBD (unidirectional 74xx, one direction per IC);
  confirm the GE side's open-collector-vs-push-pull question (OPEN 5) before
  final adapter assembly.

## Protocol ground truth

Everything wire-level is derived from the gemu emulator sources
(`../gemu/cardreader.c`, `reader.c`, `msl-states.c`, `tests/initial-load.c`,
`transcode.c`, `sat_batches.c`) and the atlas backplane transcriptions
(`../atlas/row_{I,L,M}_pinout_verified.csv`). When in doubt, those win.
