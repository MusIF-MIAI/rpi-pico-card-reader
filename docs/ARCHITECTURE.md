# GE-120 Card Reader Simulator — Firmware Architecture

Raspberry Pi Pico 2 W firmware that replaces the GE-120's punched-card reader
(LS 600 / GIS 450 controller). It plugs into the CPU backplane at the three
connector-2 COCA slots (I1 / L1 / M1), carries reduced `.cap` test decks in
flash, and lets the machine's own bootstrap (`CLEAR → LOAD1 → LOAD → START`)
pull a deck into core memory.

Ground truth for every protocol statement below is the gemu emulator
(`gemu/cardreader.c`, `reader.c`, `msl-states.c`, `tests/initial-load.c`,
`cap.c`, `transcode.c`, `sat_batches.c`) and the machine documentation it
cites. File:line references are into the gemu repo.

---

## 1. System overview

```
   PC (USB host)                      Pico 2 W                        GE-120 backplane
  ┌──────────────┐        ┌──────────────────────────────┐        ┌─────────────────────┐
  │ /dev/ttyACM0 │◄──CDC──┤ core0: console, deck mgmt,   │        │  COCA slots         │
  │  console     │        │        FatFs, TinyUSB        │        │  I1: RE bus, TU00B  │
  │              │        ├──────────────────────────────┤ 4× 74x │  L1: status lines   │
  │ FAT drive    │◄──MSC──┤ flash FAT region (.cap files)│◄─level─►  M1: LU bus, LU08B, │
  │  (.cap files)│        ├──────────────────────────────┤ shift  │      TU03B, POM0B   │
  └──────────────┘        │ core1: feeder FSM, PIO,      │        └─────────────────────┘
                          │        GPIO IRQs (realtime)  │
                          └──────────────────────────────┘
```

The Pico **is** the COCA replacement. Signals that were internal to the real
COCA controller board — PICON, BI20, the mode decodes N001/N002/DEBI/MI01/MI02,
COCON, REGEN — never appear on any wire here; their logic lives in firmware.

Design decisions fixed with the user:

- attach at COCA slots I1/L1/M1 (pull the three connector-2 coupler cards);
- sense the RE command bus from day one (no scripted command guessing);
- deck storage: USB MSC FAT region in flash + CDC-ACM console to pick decks;
- level shifters: unidirectional 74xx-family, all ports of one IC in the same
  direction (exact part TBD; see PINOUT.md for the banking).

## 2. Electrical & pin map summary

Full tables with atlas citations live in [PINOUT.md](PINOUT.md).

- GE logic: +5.2 V rail (ALI 150), 1968-71 discrete logic. Mnemonic suffix
  `N`/`B` = active-low wire. All Pico pins use
  `gpio_set_outover/inover(pin, GPIO_OVERRIDE_INVERT)` so firmware logic is
  active-high throughout.
- Outputs (Pico → GE): LU00N-LU07N data, LU08N char strobe, FININ end-of-card,
  FIDEN end-of-sequence, LUPOR ready, POM01 binary-mode, LUREN error.
- Inputs (GE → Pico): TU00N command strobe (~1.2 µs one-shot, CE10),
  TU03N card-feed strobe (CE09), RE00N-RE07N command byte.
- Straps on the adapter (no GPIO): LESAB = active (reader present),
  LUSEN and LENON = inactive.
- Board is USB-powered, never backplane-powered; single ground reference to
  the backplane ZERO1 pins.

## 3. The LOAD operation (what the machine does)

Operator: `CLEAR → LOAD1 → LOAD (arms AINI) → START`. Microstate walk
(`tests/initial-load.c:22-137`):

```
80 → c8 → d8..dc → cc  rRE ← 0x40 (read forward)      TU00N fires (CE10)
   → ca → a8/a9        rL1 ← 0x0080 (≤129 words)
   → aa/ab             rV1 ← 0x0000, RASI set          TU00N (input-transfer arm)
   → b8 ⇄ b9/b1        reader strobes nibbles, channel packs 2 per byte
   → b8 WAIT           FININ ends the transfer (RIG1 latch chain)
   → ea/eb             TU03N card-feed strobe (CE09)
   → e3                execute the loaded block at address 0
```

The loaded block is the **loader card** (4-hex-nibble-per-column card,
`loader/loader.s`, `software/loader.txt`). It then loops per program card:
PER set-by-pass · PER read → 0x0036 · PER exam · MVC relocate · JU 0 —
each PER arriving on the wire as an RE byte + TU00N strobe.

## 4. Command–response matrix (core1 dispatch table)

Every CPU command = RE00-07 valid + TU00N strobe, **including exam and
reset-error** (CE10 at state ab for transfers, `msl-states.c:1735`; at state
ca for Z-bit-7 set/exam commands, `msl-states.c:1654`). Status is **never a
data transfer**: for exam the CPU samples the standing status wires itself
(RG decodes → RO at state cc); the reader's only exam duty is keeping
LUPOR/LUREN/FIDEN correct at all times.

| RE byte | Command (`reader.c:6-20`, `software/loader.txt`) | Transfer? | Firmware action |
|---|---|---|---|
| `0x40` | Read unchanged (IPL + loader's per-card read) | yes | present current card in *current* mode; mode not touched |
| `0x21`/`0x01` | Read normal i/ii | yes | latch mode NORMAL, present |
| `0x24`/`0x04` | Read mixed i/ii | yes | latch mode NORMAL-equivalent, present |
| `0x20` | Read binary / by-pass | yes | latch mode COLBIN, assert POM01, present |
| `0xa0` | Put binary (output direction) | no | latch COLBIN; log; we never accept output data |
| `0x44` | Exam of conditions (EPER Z=0xC0) | no | nothing active — status wires already valid; log |
| `0x47` | Reset error (SPER Z=0x80) | no | clear LUREN latch and pin |
| `0x48` | Card reject | no | skip current card (advance pointer, no presentation) |
| `0x0c` | No function | no | log only |
| other | unknown | no | log + counter; never crash the feed |

**OPEN #1 — the set-by-pass byte.** The reconstructed loader
(`loader/loader.s:16-18`) decodes its "Set by-Pass" order block to Z=0x80,
cmd=**0x40**, while `reader.c:56` latches by-pass on **0x20**. gemu sidesteps
this: after the loader card, program cards are forced to COLBIN regardless
(`cardreader.c:325-348`, `post_loader_pack`). The firmware does the same by
default (policy `post_loader_colbin = on`), latches modes per the table when
they do arrive, and **logs every RE byte with a timestamp** so the passive
bring-up run resolves the truth.

## 5. Presentation cadence (what one "character" looks like)

Verified semantics (`tests/initial-load.c:85-131`, `cardreader.c:362-405`):

- The channel-1 input microcode packs **two presented values per memory
  byte, keeping the LOW nibble of each** (present 0xAB then 0xCD → mem 0xBD).
  Only LU00-03 ever carry information; LU04-07 are held inactive.
- **TC_HEX** (loader card): one strobe per column; nibble = sum of punched
  digit-row indices (`transcode.c:574-588`). 2 columns → 1 byte.
- **TC_COLBIN** (by-pass program cards): column → byte via
  B2R = {9,8,7,6,3,2,1,0} (`transcode.c:590-607`), presented as **hi-nibble
  strobe then lo-nibble strobe** (`cardreader.c:369-373`).
- **FININ rides the low nibble of the last column** (`cardreader.c:365-372`);
  it feeds the RIG1/PEC1 end-latch chain (commit at TO50, RASI clear TO70).
- **LUPOR must be low whenever LU08N is asserted** — the PELEA invariant
  `PELEA = !(LU081 · LUPO1)` (`signals.h:933`, `reader.c:66-73`). Enforced in
  hardware here: LU08N and LUPOR are one PIO side-set pair, always written as
  complements.

Per-nibble timeline generated by PIO0 SM0 (all parameters runtime-tunable):

```
            |<——————————— period P = S + W + G ————————————>|
LU00-03  ———X════════ nibble valid ════════════════X———————     FININ (OUT bit 8)
FININ    ———X════════ (only on final nibble) ══════X———————     rides the same word
LU08N       ┌──────────┐
 (logical)──┘   W      └───────────────────────────────────
LUPOR    ═══┐          ┌═══════════════════════════════════     side-set complement
            └──────────┘
         S→|←—— W ——→|←———————— G ————————→|
```

| Param | Default | Meaning | Constraint |
|---|---|---|---|
| `S` | 0.5 µs | data setup before strobe | ≥ 1 PIO tick |
| `W` | 3.5 µs | LU08N width | analytic safe window ~3-4 µs: too short misses the TO00 request latch; too long re-raises the char request after the b1-cycle CE18 reset → double-read (`signals.h:1073-1076`, `msl-commands.c:689-707`). **OPEN #2** |
| `G` | 11.5 µs | gap to next strobe | real BI20 spacing was ~15 µs/nibble (`docs/signals.md:56`) |
| `D` | 20 µs | command → first strobe delay | stands in for the invisible RASI arm |

PIO tick = 100 ns (clkdiv 150 MHz → 10 MHz). At the defaults a full 80-column
binary card ≈ 160 nibbles × 15.5 µs ≈ 2.5 ms — hundreds of times faster than
the real transport, still leisurely for the 2 µs machine.

## 6. Card and deck flow (feeder FSM, core1)

Ported from `cardreader.c:53-60` with wall-clock pacing replacing the
emulator's RASI-gated cycles:

```
DISARMED ──arm──► ARMED_WAIT ──read cmd──► PRESENTING ──last nibble──► CARD_DONE
   ▲                  ▲                        │  ▲                       │
   │                  │ deck exhausted: FIDEN  │  └─── next read cmd ─────┤
   └── disarm ────────┴────────── DONE ◄───────┘       or TU03N: advance ─┘
                              (LUREN set ──► ERROR, cleared by cmd 0x47)
```

- **End of card**: stop strobing after the FININ nibble. Deassert FININ on
  (a) TU03N, (b) the next TU00N command, or (c) timeout (default 1 ms) —
  FININ must not leak into the next card (`reader.c:98-107`). **OPEN #4.**
- **Advance**: any TU03N pulse = feed next card. gemu confines TU03 to the
  loader card (`cardreader.c:405-414`) but that is a model shortcut — the
  CE09 gating (`msl-states.c:1742,1761,1826`) allows it at every end-of-card
  b8. Fallback: auto-advance when a fresh read command arrives with the
  pointer parked at end-of-card. A stats counter records which path fired.
  **OPEN #3.**
- **Deck exhausted**: assert FIDEN, stay ready (`cardreader.c:251-254`).
- **No general clear reaches these slots.** REGEN travels on reader-harness
  connector 1253-5, not the CPU-side backplane (CAGU7 lands on none of
  I1/L1/M1). Replacements: console `arm`/`rewind`/`disarm`, plus an optional
  idle auto-rewind (armed + mid-deck + no wire activity for N s + fresh
  `0x40` ⇒ rewind first; default on, N = 5).

### Deck surgery (port of `sat_batches.c:88-194`)

- Loader-card detect: first card among indices 0-3 with `cols[2] == 0x0100`
  (row-8 punch in column 3), else card 0 (`sat_batches.c:140-151`); the
  TC_HEX-decoded card must open with `9E 80 00 .. 9E 80`
  (`cardreader.c:113-148`).
- `SERIAL_LOADER_PLUS_BODY`: that loader card + cards 5..N-2.
- `TRIM_TITLE_SUMMARY`: cards 1..N-2.
- Seven named SAT batches (cpu-functional, card-reader-a, printer-mechanical,
  control-program-cr, ls600-controller/transcoder/doe-sat) combining 1-2
  source decks each.

## 7. Dual-core and interrupt architecture

### core0 — console, storage, housekeeping (never touches the wire)

| Module | Role |
|---|---|
| `usb_composite.c`, `usb_descriptors.c` | TinyUSB composite CDC-ACM + MSC |
| `console.c` | command shell on CDC (see §9) |
| `storage.c` | MSC block device ↔ flash FAT region; FatFs mount for firmware reads |
| `deck.c` | `.cap` streaming parser (buffer-based `cap.c` rewrite) → RAM deck image |
| `surgery.c` | SAT batch recipes on deck images |
| `config.c` | tunable parameters (S/W/G/D, policies), persisted in a flash config sector |
| `monitor.c` | event-ring pretty printer, `trace` mode, stats |

### core1 — the wire (hard realtime)

All core1 code is SRAM-resident (`__not_in_flash_func` / `.time_critical`
section) and the active deck image lives in RAM: **MSC flash writes stall
XIP**, and core1 must keep running through them.

| Module | Role |
|---|---|
| `core1_main.c` | init, IRQ claim, main dispatch loop |
| `feeder.c` | FSM of §6 + command matrix of §4 |
| `wire_tx.c` | PIO0 SM0 presenter: FIFO feed, abort/drain, parameter reload |
| `wire_rx.c` | PIO1 SM0 RE capture RX FIFO + TU03N GPIO IRQ |
| `status_pins.c` | FIDEN / POM01 / LUREN software GPIOs |

### PIO usage

- **PIO0 SM0 — presenter** (`pio/presenter.pio`): one 32-bit TX word per
  nibble, `[FININ:1 | data:8]` in the low 9 bits; `out pins, 9` drives
  LU00-07+FININ; 2-bit side-set drives {LU08N, LUPOR} as complements; W and G
  delay loops count preloaded values. TX-not-full IRQ keeps the FIFO fed.
- **PIO1 SM0 — RE capture** (`pio/re_capture.pio`): wait for the TU00N active
  edge, delay ~300 ns to mid-pulse, `in pins, 8` (RE00-07, contiguous
  GP15-22), `push` → RX FIFO → IRQ to core1. Immune to IRQ-latency jitter.
- TU03N: plain GPIO IRQ (edge) on core1 — pulse is a full machine-cycle
  event and needs no sub-µs capture.

### IRQ routing (all on core1's NVIC)

| IRQ | Source | Priority |
|---|---|---|
| `PIO1_IRQ_0` | RE byte captured | highest |
| `IO_IRQ_BANK0` (proc1 routing) | TU03N edge on GP26 | high |
| `PIO0_IRQ_0` | presenter FIFO wants data | normal |
| `SIO_IRQ_FIFO` | core0 command mailbox | low |

### Inter-core protocol (`include/ipc.h`)

- core0 → core1: `multicore_fifo` opcodes `ARM(slot)`, `DISARM`, `REWIND`,
  `SET_PARAM(id,val)`, `INJECT_ERROR`, `EJECT`.
- core1 → core0: nothing blocking — core1 writes a `volatile` status struct
  (state, card/col/half, mode, counters) and appends to a lock-free event
  ring `{timestamp_us, kind, byte}` recording every RE byte, TU00N/TU03N,
  strobe batch, and anomaly. core0 polls and pretty-prints.

## 8. Storage design

Flash map (Pico 2 W: 4 MB QSPI):

```
0x000000 ─ firmware (XIP)            ~ up to 960 KB
0x0F0000 ─ config sector             4 KB (tunables, last-armed deck)
0x100000 ─ FAT16 region → USB MSC    3 MB (.cap files, ~772 KB for all 15 SAT decks)
0x3FFFFF
```

- The MSC LUN maps 1:1 onto the FAT region; the PC formats/populates it like
  a thumb drive. Device-side, FatFs mounts it read-only to enumerate and
  stream `.cap` files.
- **Write policy**: flash writes (MSC or config) run under `pico_flash`
  safe-execute with core1 lockout **only while DISARMED**. While a session is
  armed the MSC reports write-protected (CSW fail) — feeding only ever
  touches RAM.
- RAM deck image (`include/deckimg.h`): u16 columns, per-card index; the
  largest SAT deck (isolationcpu02, 292 cards) ≈ 47 KB — comfortable in
  520 KB SRAM, with a second buffer slot for surgery output.
- `.cap` files may be full captures or `tools/capstrip.py`-reduced ones; the
  parser (same semantics as gemu `cap_load`) skips non-hex lines either way.

## 9. Console (CDC-ACM)

ASCII, newline-terminated, `OK`/`ERR <reason>` replies, human-friendly.

```
ls                       list .cap files on the FAT region
batches                  list built-in SAT batch recipes
arm <file|batch> [--raw] parse (+ surgery) → RAM, enter ARMED_WAIT
disarm | rewind | eject  session control
status                   FSM state, card/col/half, mode, counters, tunables
set <S|W|G|D|policy> <v> tunables (µs, 0.1 µs granularity); `save` persists
trace on|off             live event-ring dump (every RE byte, TU strobe)
inject-error [luren]     assert LUREN until the machine sends 0x47
version | help
```

## 10. OPEN items → measurements that resolve them

| # | Unknown | Resolved by |
|---|---|---|
| 1 | RE byte of the loader's "set by-pass" (0x20 vs 0x40-under-Z=0x80) | passive capture at I1 during a real LOAD (bring-up step 5) |
| 2 | LU08N min/max width & double-read boundary | scope LU08B (M1·15) vs behavior; tune W/G; symptoms: doubled/missing nibbles in the loaded image |
| 3 | TU03N cadence on non-loader cards | passive capture during multi-card load |
| 4 | FININ hold window vs RIG1 latch | start with hold-until-next-command; scope L1·06 if load-end misbehaves |
| 5 | GE-side drivers open-collector or push-pull (`reference_cardreader_interface.md`) | ohmmeter/scope at I1 pins; adapter carries series-R + pull-up footprints either way |
| 6 | EPER status-bit mapping beyond RO1/RO2/RO6 (`signals.h:1038`) | only needed to fake specific abnormal conditions; defer |

## 11. Implementation order

1. **Host-buildable core**: cap parser, transcode, surgery, feeder FSM with a
   mock wire — unit tests in `test/host/` against gemu-derived golden traces
   (`tests/initial-load.c` cadence).
2. **USB composite + FatFs + console**; deck → RAM path end-to-end on the bench.
3. **PIO presenter + RE capture** verified with a second Pico / logic
   analyzer: W/G/S timing, FININ ride, LUPOR complement.
4. **core1 integration**: IRQ routing, SRAM residency, MSC write lockout.
5. **Passive bring-up on the real machine — inputs only, outputs
   tri-stated**: log RE/TU00N/TU03N during a console LOAD attempt
   (resolves OPEN 1/3). GP27 emits a scope trigger per captured command.
6. **Active load**: loader card alone, then full decks; tune W/G (OPEN 2/4);
   then SAT batches, auto-rewind polish, error injection.

Note: the machine currently memfaults on RAM access (see
`project_restoration_status.md`); step 6 needs that fixed, but steps 1-5 do not.

## 12. Corrections vs. earlier board-design drafts

Relative to `board-design/project_card_reader_emulation_board_rp2350.md` /
`..._f7_reference.md`:

- **TU00N is an input** (GE → reader command strobe); the old doc had CPU1
  "generating TU00N pulses". Nothing here generates TU00N or TU03N.
- Pico 2 (W) has **4 MB** flash, not 16 MiB; RP2350 USB is **FS 12 Mbps**,
  not 480; there are no LD1/LD2/LD3 LEDs (that was Nucleo residue) — and on
  the **W** variant the on-board LED is behind the CYW43, not GP25.
- TXB0104 bidirectional shifters are dropped for unidirectional 74xx banks
  (user's parts; also TXB drive strength was marginal for this use).
- Several backplane pin numbers in the F7 doc are wrong; PINOUT.md carries
  the atlas-verified set (FININ L1·06 not ·05, FIDEN L1·03 not ·08, LENON
  L1·13, LESAB L1·15, etc.).
- PICON/BI20 are not wired at all (COCA-internal — we are the COCA).
