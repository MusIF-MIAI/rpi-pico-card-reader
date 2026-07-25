# Pinout — Pico 2 W ↔ level shifters ↔ GE-120 COCA slots I1/L1/M1

The Pico replaces the three connector-2 COCA coupler cards. Every backplane
pin below is verified against `atlas/row_{I,L,M}_pinout_verified.csv`
(dwg 14026 136 transcription); the atlas **overrides** the older
`board-design/*_f7_reference.md` tables, which carry known-wrong pin numbers
(see §5).

Card edge connectors are 17-pin ("PIEDINI DEL CONNETTORE 01-17", cp08);
standard cards use 01-07 and 09-16, pins 08/17 blank. Pin 05 is `ZERO1`
(logic ground) on all three slots — use it as the single ground reference.

Polarity: `N`/`B` suffix = active-low wire on the +5.2 V GE rail. All Pico
GPIOs are configured with `GPIO_OVERRIDE_INVERT` (both directions) so the
firmware reasons in active-high logic exclusively.

## 1. Outputs — Pico → GE (3.3 V → 5.2 V, shifter banks A and B)

| Pico GP | Signal | COCA slot·pin | Atlas cell (raw OCR) | Driven by | Idle wire level |
|---|---|---|---|---|---|
| GP0 | LU00N (data 0) | M1·01 | `LUOGO` | PIO0 SM0 `out` bit 0 | high (inactive) |
| GP1 | LU01N | M1·02 | `LU01S` | PIO0 `out` bit 1 | high |
| GP2 | LU02N | M1·03 | `LU023` | PIO0 `out` bit 2 | high |
| GP3 | LU03N | M1·04 | `LU03B` | PIO0 `out` bit 3 | high |
| GP4 | LU04N | M1·06 | `LUO4G` | PIO0 `out` bit 4 (always inactive: channel packs nibbles, only LU00-03 carry data) | high |
| GP5 | LU05N | M1·07 | `LU05B` | PIO0 `out` bit 5 (inactive) | high |
| GP6 | LU06N | M1·09 | `LU06B` | PIO0 `out` bit 6 (inactive) | high |
| GP7 | LU07N | M1·10 | `LU07B` | PIO0 `out` bit 7 (inactive) | high |
| GP8 | FININ (end-of-card) | L1·06 | `F1N1B` | PIO0 `out` bit 8 — rides the last-nibble data word | high |
| GP9 | LU08N (char strobe) | M1·15 | `LU0BB` | PIO0 side-set bit 0 | high |
| GP10 | LUPOR (ready) | L1·07 | `LUPDB` | PIO0 side-set bit 1 — hardware complement of LU08N (PELEA invariant) | low (= ready asserted when armed) |
| GP11 | FIDEN (end-of-sequence) | L1·03 | `F1DEB` | software GPIO (core1) | high |
| GP12 | POM01 (binary-mode ind.) | M1·12 | `PDH0B` | software GPIO (core1) | high |
| GP13 | LUREN (error) | L1·09 | `LUREB` | software GPIO (core1) | high |

Bank A (8 lines): GP0-7. Bank B (6 lines): GP8-13, 2 spare positions.

## 2. Inputs — GE → Pico (5.2 V → 3.3 V, shifter banks C and D)

| Pico GP | Signal | COCA slot·pin | Atlas cell | Consumed by |
|---|---|---|---|---|
| GP16 | RE01N | I1·02 | `RE018` | PIO1 SM0 `in pins` bit 0 |
| GP17 | RE02N | I1·03 | `RE02B` | `in` bit 1 |
| GP18 | RE03N | I1·04 | `RE03B` | `in` bit 2 |
| GP19 | RE04N | I1·06 | `RE04B` | `in` bit 3 |
| GP20 | RE05N | I1·07 | `RE05B` | `in` bit 4 |
| GP21 | RE06N | I1·09 | `RE06B` | `in` bit 5 |
| GP22 | RE07N | I1·10 | `RE07B` | `in` bit 6 |
| GP26 | TU03N (card feed) | M1·13 | `TU038` | GPIO IRQ on core1 (proc1 routing) |
| GP27 | TU00N (command strobe, ~1.2 µs) | I1·15 | `TU00B` | PIO1 SM0 trigger (`wait gpio`) + GPIO IRQ (stats) |
| GP28 | RE00N | I1·01 | `RE00B` | PIO1 SM0 `in pins` bit 12 |

**GP14 and GP15 are reserved for later** (unconnected, headers 19/20); TU00N
and RE00N, formerly there, now sit on GP27 and GP28. The RE bus is therefore
no longer contiguous: PIO1 SM0 samples the 13-pin window GP16-28 in a single
`in pins, 13` (bits 0-6 = RE01-07, bit 12 = RE00) and firmware reassembles
the byte; bits 7-11 (GP23-25 CYW43, GP26, GP27) are masked out.

Bank C (8 lines): RE bus — RE01-07 → GP16-22, RE00 → GP28.
Bank D (2 lines): GP27, GP26, 6 spare positions (future: RE08N parity I1·12,
LU20B/LU21B if they turn out to matter).

RE08N (odd parity, I1·12) is deliberately not sensed in phase 1.

## 3. Straps on the adapter (no GPIO)

| Signal | COCA slot·pin | Atlas cell | Strap to | Meaning |
|---|---|---|---|---|
| LESAB (reader present) | L1·15 | `LESAB` | active level | "a card reader is attached" |
| LUSEN (out of service) | L1·04 (†) | `LOSES` | inactive level | never out of service |
| LENON (not operable) | L1·13 | `LENOB` | inactive level | always operable (LENON=active would suppress TU03N feeds, `reader.c:113-118`) |

(†) `LOSES` is almost certainly OCR for `LUSEB` — the connector-2 form of
LUSEN — as L1 is the connector-2 status card and every other status line sits
there. Verify continuity from L1·04 to M15·01/N8·06 (known LUSEN nodes)
before strapping. If it is not LUSEN, strap those backplane nodes instead.

Whether "active/inactive level" means drive-to-rail or open (release to a
backplane pull) depends on OPEN #5 (open-collector vs push-pull) — the
adapter should carry both a pull-up footprint and a direct-tie option per
strap.

Unresolved L1 pins, left unconnected: L1·01 `CASUS`, L1·10 `LU20B`,
L1·12 `LU21B`.

## 4. Level shifters — 4× LM54LVC245AN (confirmed 2026-07-25)

LM54LVC245AN = mil-temp DIP-20 74LVC245A octal transceiver: VCC ≤ 3.6 V,
**5 V-tolerant I/O**, one DIR pin per chip (all 8 bits one direction), OE#
tri-state. Pinout: 1=DIR, 2-9=A1-A8, 10=GND, 11-18=B8-B1 (mirrored),
19=OE#, 20=VCC.

**Power all four chips from the Pico 3V3 rail — never from the GE +5.2 V**
(LVC abs-max VCC 3.6 V). 100 nF decoupling per chip. GE 5.2 V signals into
the B ports are within the 5.5 V I/O tolerance.

⚠️ Toward the GE, a driven high is **3.3 V, not 5.2 V**. Active-low lows are
solid 0 V, but if the GE input threshold were above 3.3 V, *inactive* status
lines (e.g. LUREN) would falsely read asserted. The passive first session
must also measure what voltage a genuine backplane high sits at before
enabling the output banks. Hedge: 33-100 Ω series resistors on every B-side
line + unpopulated pull-up footprints (interacts with OPEN #5).

### IC1 — LU data bus (out, Pico→GE) · DIR=3V3 (A→B), OE#=JP-OE jumper

| Pico header | GP | A pin | B pin | COCA pin | Signal |
|---|---|---|---|---|---|
| 1 | GP0 | A1 (2) | B1 (18) | M1·01 | LU00N |
| 2 | GP1 | A2 (3) | B2 (17) | M1·02 | LU01N |
| 4 | GP2 | A3 (4) | B3 (16) | M1·03 | LU02N |
| 5 | GP3 | A4 (5) | B4 (15) | M1·04 | LU03N |
| 6 | GP4 | A5 (6) | B5 (14) | M1·06 | LU04N |
| 7 | GP5 | A6 (7) | B6 (13) | M1·07 | LU05N |
| 9 | GP6 | A7 (8) | B7 (12) | M1·09 | LU06N |
| 10 | GP7 | A8 (9) | B8 (11) | M1·10 | LU07N |

### IC2 — strobes + status (out, Pico→GE) · DIR=3V3, OE#=JP-OE jumper

| Pico header | GP | A pin | B pin | COCA pin | Signal |
|---|---|---|---|---|---|
| 11 | GP8 | A1 (2) | B1 (18) | L1·06 | FININ |
| 12 | GP9 | A2 (3) | B2 (17) | M1·15 | LU08N |
| 14 | GP10 | A3 (4) | B3 (16) | L1·07 | LUPOR |
| 15 | GP11 | A4 (5) | B4 (15) | L1·03 | FIDEN |
| 16 | GP12 | A5 (6) | B5 (14) | M1·12 | POM01 |
| 17 | GP13 | A6 (7) | B6 (13) | L1·09 | LUREN |
| — | — | A7,A8 → GND | B7,B8 n/c | — | spare (LVC inputs must not float) |

### IC3 — RE command bus (in, GE→Pico) · DIR=GND (B→A), OE#=GND

| COCA pin | Signal | B pin | A pin | GP | Pico header |
|---|---|---|---|---|---|
| I1·01 | RE00N | B1 (18) | A1 (2) | GP28 | 34 |
| I1·02 | RE01N | B2 (17) | A2 (3) | GP16 | 21 |
| I1·03 | RE02N | B3 (16) | A3 (4) | GP17 | 22 |
| I1·04 | RE03N | B4 (15) | A4 (5) | GP18 | 24 |
| I1·06 | RE04N | B5 (14) | A5 (6) | GP19 | 25 |
| I1·07 | RE05N | B6 (13) | A6 (7) | GP20 | 26 |
| I1·09 | RE06N | B7 (12) | A7 (8) | GP21 | 27 |
| I1·10 | RE07N | B8 (11) | A8 (9) | GP22 | 29 |

### IC4 — GE strobes (in, GE→Pico) · DIR=GND, OE#=GND

| COCA pin | Signal | B pin | A pin | GP | Pico header |
|---|---|---|---|---|---|
| I1·15 | TU00N | B1 (18) | A1 (2) | GP27 | 32 |
| M1·13 | TU03N | B2 (17) | A2 (3) | GP26 | 31 |
| — | spare: B3-B8 → GND | | | (future RE08N parity, I1·12) | |

### Not through any shifter

| Connection | From | To |
|---|---|---|
| Output enable | JP-OE jumper | IC1+IC2 OE# (pin 19); **pull-up to 3V3, so jumper open = tri-stated/passive**; close to GND to drive the backplane. No firmware control. |
| Reserved | GP14 (header 19), GP15 (header 20) | unconnected, reserved for later |
| VCC | Pico 3V3 OUT (header 36) | pin 20 × 4 |
| Ground | Pico GND (3/8/…/38) | pin 10 × 4 + ZERO1 (pin 05 of I1/L1/M1), star-joined |
| Straps | LESAB→L1·15 active, LUSEN→L1·04 inactive, LENON→L1·13 inactive | jumpered, liftable for passive mode; tie form per OPEN #5 |

GP23/24/25/29 are CYW43-reserved on the Pico 2 **W** — never assign them.

## 5. Corrections vs. `board-design/*_f7_reference.md`

Trust the atlas; the F7-era doc mislocates:

| Signal | F7 doc said | Atlas (correct) |
|---|---|---|
| FININ | L1·05 | **L1·06** |
| FIDEN | L1·08 | **L1·03** |
| LENON | L1·16 / M16·02 | **L1·13** / M16·01 |
| LUSEN | M15·02 / N8·12 | M15·01 / N8·06 (COCA entry likely **L1·04**) |
| LUREN | N8·14 | **L1·09** / M13·03 / N8·09 |
| LESAB | H7·15 / N8·16 | **L1·15** / H7·09 / N8·11 |

Also: `docs/signals.md` marks FININ/FIDEN "not located" — they are (above).

## 6. Physical form

Three 17-pin edge-card breakouts (or wire-wrap tails onto the I1/L1/M1
backplane pins) → ribbon/discrete wires → shifter board → Pico 2 W.
Board is USB-powered; the only backplane connections are signal pins plus
one ZERO1 ground pin per slot (star-joined at the shifter board). Never
draw power from the +5.2 V rail. Measure the edge-connector pitch on a
pulled COCA card before ordering PCBs — the drawing set does not state it.
