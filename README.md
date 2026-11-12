# PaperLagoon (working title)

A generative environmental-music firmware for the M-VAVE FM-1, after 1980s Japanese *kankyō ongaku*
(and, at most, Susumu Yokota's hazy pulse). It plays by itself; you steer it from outside.

Status: a sketch. FM voices through AMB-1's tape echo and plate, played by a generator of short motifs that a
hidden state leans. Tested in the host simulator; runs on an FM-1 (installed with base/tools/fm1_install.py, 2026-11-11).

## Controls

| Control | Does |
|---|---|
| SELECT | turns through the pages: 1 the lagoon, 2..4 the kit's voices, 5 the effects, 6 the environment, 7 the cycles, 8 the memory, 9 global (as HYP-1: on the rule over the knobs, the page's name and a short bar a page, the one shown lit; the header keeps the world) |
| HOME | the lagoon (lit there) |
| EDIT | the voices (lit there); again: the next voice; held: the voice as the kit has it |
| FX | the effects: K1..K4 Time (the tape's loop), Feedback, Size (the plate), Wear (the tape's age) |
| ENV | the environment: the world, its root, beat and colour; K1..K4 Drift (how far the cycles and offerings sway the music), Offer, Tempo, Transpose |
| LFO | the cycles (breath, tide, long turn) as orbits: K1..K3 their depths, K4 Pace |
| GLO | global: K1..K4 Tune, Level, Lights (the buttons' glow), Sleep (minutes; then it rests) |
| ARP | a pulse of broken chords under the music, on or off (soft, short; no drums) |
| OCT- / OCT+ | the music's register, two octaves either way |
| REC | held: the keys play (the music waits); let go: the phrase is taken in as the music's own (the motif, the drone's chord, the loops' notes). OCT- / OCT+ meanwhile move the keys' octave |
| SEL | held: K1..K4 are the lagoon's four from any page (on the lagoon itself, in finer steps) |
| SEQ | the memory: what the music holds (the motif and the ones remembered, the chord, the loops); K1..K4 Repeat, Change, Length, Memory. Held: hold it (no new motif, chord or colour), held again: let go |
| PRESETS | the kit: six sets of three voices (lead, answer, low); edits are kept per kit |
| ALGORITHM | the world: five places that exist nowhere, each its own way of making music (never the knobs nor the kit) |
| K1..K4 | the lagoon: Density, Bright, Space, Echo. A voice: Color (the FM ratio), Bright, Decay, Soft (the attack) |
| PLAY | plays or rests |
| SAVE | keeps all of it (lit when something is not saved) |

Every button glows dim; the one whose page shows is lit, and so are PLAY when playing, SAVE when unsaved, SEQ when held, ARP when on, OCT when moved.

| Keys | play no notes (but under REC): a press *offers* something to a hidden state (held: a little more, again and again) |

Kits: PAPER LAGOON, WOODEN RAIN, GLASS ORCHARD, SLOW BREATH, IRON SNOW, HAZE ARCHIVE.

Worlds, each its own way of making music (heard at once; the screen changes with it):
SHALLOWS OF MEMORY (short motifs, repeated and changed), BEHIND THE HORIZON (a drone: chords that swell and fall
back, a low tone under them, now and then one note of a melody), THE UNREAD LETTER (single points, far apart: a few
pitches, the letter's words, each a leap from the last, at uneven distances; a long silence after each line; in the
margin some stay unread), SILENT FOUNTAIN (one figure that rises and falls back, round and round in soft eighths,
changing a little now and then), WHERE LIGHT BECOMES MOSS (four loops of unequal length, drifting in and out of step).

SEQ's knobs, world by world: Repeat (a motif's passes; a line read again; the fountain's rounds before it may change),
Change (a note moved; a word exchanged; the figure's height, foot, length or start), Length (notes to a motif, a line,
a figure; the drone's swells; the loops' lengths), Memory (how often an older motif, line or figure comes back).

The music leans on four hidden quantities, named for paper (light, bleed, fold, margin; never shown), which three slow cycles of unequal length
(their lengths wander, about 50–100, 120–230 and 300–600 s) keep turning. An offering seeps into them over some seconds and fades over minutes; what it
changes (the scale, the register, the rests, the motifs' length, how often the answer and the low voice come, an
older motif folding back, a motif built on the offered pitch) is never one key to one thing. The screen answers at once: a drop falls to the water, rings spread.
The water's line is the state: higher in light, rougher when folded.

## Build

```sh
git submodule update --init --recursive
./build.sh --host                     # host simulator (a C compiler is enough)
./test.sh                             # every script in app/tests, judged by exit code (or: ./test.sh rec)
./build.sh --web                      # browser emulator (Emscripten)
./serve.sh [port]                     # serve it on http://localhost:8000/
./build.sh                            # device package (JieLi toolchain + AC79 SDK)
./install.sh [--build]                # install it on an FM-1 over USB (needs mido, python-rtmidi)
```

## Licence

GPL-3.0-only (see LICENSE). Built on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0) and
felucca-bsp. The plate and the echo come from AMB-1 (MPL-2.0); see LICENSING.md.
