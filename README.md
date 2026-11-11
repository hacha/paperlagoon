# PaperLagoon (working title)

A generative environmental-music firmware for the M-VAVE FM-1, after 1980s Japanese *kankyō ongaku*
(and, at most, Susumu Yokota's hazy pulse). It plays by itself; you steer it from outside.

Status: a sketch. FM voices through AMB-1's tape echo and plate, played by a generator of short motifs that a
hidden state leans. Tested in the host simulator; runs on an FM-1 (installed with base/tools/fm1_install.py, 2026-11-11).

## Controls

| Control | Does |
|---|---|
| SELECT | turns through the pages: 1 the lagoon, 2..4 the kit's voices, 5 the effects, 6 the memory (the header shows n/6) |
| HOME | the lagoon (lit there) |
| EDIT | the voices (lit there); again: the next voice; held: the voice as the kit has it |
| FX | the effects: K1..K4 Time (the tape's loop), Feedback, Size (the plate), Wear (the tape's age) |
| SEQ | the memory: what the music holds (the motif and the ones remembered, the chord, the loops); K1..K4 Repeat, Change, Length, Memory. Held: hold it (no new motif, chord or colour), held again: let go |
| PRESETS | the kit: six sets of three voices (lead, answer, low); edits are kept per kit |
| ALGORITHM | the world: five places that exist nowhere, each its own way of making music (never the knobs nor the kit) |
| K1..K4 | the lagoon: Density, Bright, Space, Echo. A voice: Color (the FM ratio), Bright, Decay, Soft (the attack) |
| PLAY | plays or rests |
| SAVE | keeps all of it (lit when something is not saved) |

Buttons that do something glow dim; the one whose page shows is lit.

| Keys | play no notes: a press *offers* something to a hidden state (held: a little more, again and again) |

Kits: PAPER LAGOON, WOODEN RAIN, GLASS ORCHARD, SLOW BREATH, IRON SNOW, HAZE ARCHIVE.

Worlds, each its own way of making music (heard at once; the screen changes with it):
SHALLOWS OF MEMORY (short motifs, repeated and changed), BEHIND THE HORIZON (a drone: chords that swell and fall
back, a low tone under them, now and then one note of a melody), THE UNREAD LETTER (motifs, sparse; older ones come
back), SILENT FOUNTAIN (motifs, bright and quick), WHERE LIGHT BECOMES MOSS (four loops of unequal length, drifting
in and out of step). THE UNREAD LETTER and SILENT FOUNTAIN are to get their own ways too.

The music leans on four hidden quantities, named for paper (light, bleed, fold, margin; never shown), which three slow cycles of unequal length
(their lengths wander, about 50–100, 120–230 and 300–600 s) keep turning. An offering seeps into them over some seconds and fades over minutes; what it
changes (the scale, the register, the rests, the motifs' length, how often the answer and the low voice come, an
older motif folding back, a motif built on the offered pitch) is never one key to one thing. The screen answers at once: a drop falls to the water, rings spread.
The water's line is the state: higher in light, rougher when folded.

## Build

```sh
git submodule update --init --recursive
./build.sh --host                     # host simulator (a C compiler is enough)
app/build/host/paperlagoon_host app/tests/basic.txt app/build/host   # and tones, offerings, worlds, pages
./build.sh --web                      # browser emulator (Emscripten)
./serve.sh [port]                     # serve it on http://localhost:8000/
./build.sh                            # device package (JieLi toolchain + AC79 SDK)
./install.sh [--build]                # install it on an FM-1 over USB (needs mido, python-rtmidi)
```

## Licence

GPL-3.0-only (see LICENSE). Built on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0) and
felucca-bsp. The plate and the echo come from AMB-1 (MPL-2.0); see LICENSING.md.
