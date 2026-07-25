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
| GP14 | TU00N (command strobe, ~1.2 µs) | I1·15 | `TU00B` | PIO1 SM0 trigger (`wait gpio`) + GPIO IRQ (stats) |
| GP15 | RE00N | I1·01 | `RE00B` | PIO1 SM0 `in pins` bit 0 |
| GP16 | RE01N | I1·02 | `RE018` | `in` bit 1 |
| GP17 | RE02N | I1·03 | `RE02B` | `in` bit 2 |
| GP18 | RE03N | I1·04 | `RE03B` | `in` bit 3 |
| GP19 | RE04N | I1·06 | `RE04B` | `in` bit 4 |
| GP20 | RE05N | I1·07 | `RE05B` | `in` bit 5 |
| GP21 | RE06N | I1·09 | `RE06B` | `in` bit 6 |
| GP22 | RE07N | I1·10 | `RE07B` | `in` bit 7 |
| GP26 | TU03N (card feed) | M1·13 | `TU038` | GPIO IRQ on core1 (proc1 routing) |

Bank C (8 lines): GP15-22 (RE bus — contiguous, required for PIO `in pins`).
Bank D (2 lines): GP14, GP26, 6 spare positions (future: RE08N parity I1·12,
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

## 4. Level-shifter banking

User's parts: unidirectional 74xx-family (exact code TBD), **all ports of one
IC in the same direction**. Suggested types if compatible with the parts on
hand — outputs: 74AHCT541-class (VCC = GE +5.2 V, 3.3 V-CMOS-compatible
inputs, tri-state `OE` for the passive bring-up phase); inputs:
74LVC541-class (VCC = 3.3 V, 5 V-tolerant inputs).

| IC | Direction | Lines | Pico pins | GE pins |
|---|---|---|---|---|
| A | 3.3 → 5.2 V | LU00N-LU07N | GP0-7 | M1·01-04,06,07,09,10 |
| B | 3.3 → 5.2 V | FININ, LU08N, LUPOR, FIDEN, POM01, LUREN (+2 spare) | GP8-13 | L1·06, M1·15, L1·07, L1·03, M1·12, L1·09 |
| C | 5.2 → 3.3 V | RE00N-RE07N | GP15-22 | I1·01-04,06,07,09,10 |
| D | 5.2 → 3.3 V | TU00N, TU03N (+6 spare) | GP14, GP26 | I1·15, M1·13 |

Wire **both output banks' `OE#` (or equivalent) to a Pico GPIO** (suggest
GP28) so the passive bring-up phase can tri-state every GE-facing driver in
hardware, not just by GPIO direction.

GP27 = scope-trigger output (Pico-side only, no shifter). GP23/24/25/29 are
CYW43-reserved on the Pico 2 **W** — never assign them.

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
