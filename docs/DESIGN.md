# Vector37: design

## What the Vector is

The Beetlecrab Vector is a polyphonic desktop synth built around one idea, the Orbiter: a square space with a sound
in each corner, and a point moving through it that decides the mix. A note plays all four corners at once; where the
point is, that corner is loud. The point is a small moon (green) circling a sun (blue) that follows a path; the path
can be warped, sized, turned, synced to the tempo, or follow an envelope. Each corner makes its sound from two
"overtone generators" (fixed sets of harmonics with + and - signs, like organ registers) that are either added or
made to modulate each other, then filtered. Effects, an arpeggiator and a randomizer sit around that.

## What matters for the sound (ranked)

1. **The orbit as the mixer.** Smooth, click-free corner weights from a moving point; the suborbit adds a second,
   faster motion. Paths: Lissajous 1:1, 1:2, 3:2, an N-gon (star-able), a pulse (hopping between two corners), and
   an ADSR path walked once per note. Per-voice phase with retrigger, or all voices on one shared orbit.
2. **The overtone patterns.** Six sign patterns over the harmonics at 1/n: A..C even ones, D..F odd ones (the
   table in the manual: A -2 -10 .., B -2 -6 -10 .., C +2 +4 +6 .., D +3 +5 +7 .., E -3 -7 -11 .., F -3 -11 ..), the
   fundamental always in. Because of the signs, adding two patterns can cancel harmonics: C + D is a sawtooth,
   C + B removes 2, 6, 10. Plus N: pink noise high-passed at the pitch.
3. **The blend.** Centre: generator 1 alone; left: generator 2's overtones added; right: modulation (AM, PM,
   PM+AM, phase distortion + AM, and its mirror). This is where most of the timbre range is.
4. **Per-corner filters** (LP12, LP24, BP, HP, a two-formant vowel filter), cutoff and resonance interpolated by
   the orbit, key-tracked or fixed.
5. **Voice modes**: POLY, DUAL (each note plus a follower at an interval, the other side of the stereo), MONO (a
   detuned stack with glide).
6. **Envelopes**: amp ADSR; the orbit ADSR for the ADSR path.
7. **Effects**: drive, chorus, delay (ping-pong; at full feedback a loop), reverb.
8. **Arpeggiator**, 9. **randomizer** with undo.

## What makes the orbit audible

Reviews describe the Vector as crystalline, glassy, airy, close to classic FM / D-50 territory, with big stereo
effects and a strong chorus; vector synthesis in general (Prophet VS, Wavestation) lives from crossfading sources
that are very unlike each other. The first Vector37 sounds broke that rule: four corners too alike, paths that never
reached a corner, a bilinear mix that kept every corner in. So:

- **Corners that contrast:** per sound one dark and round corner (a low-passed sine-like pattern), one bright and
  thin (a saw or odd buzz, high- or band-passed), one metallic (PM, a generator an octave or a twelfth below), one
  noisy or vocal (N, the vowel filter); different filter types and cutoffs, different blend modes.
- **A sharper mix:** the bilinear weights are raised to a power (CONTRAST, 1 .. 8, per sound, on the orbit knobs) and
  normalized: near a planet you hear that corner, at the centre still all four alike.
- **Paths of their own, big but whole:** a circle, the pulse's hop from planet to planet, the ADSR walk, a figure
  eight, a slow 3:2 knot, a triangle and a four-point star with their tips at planets, a five-point star. SIZE goes to
  x 1.41, but orbit_fit keeps each figure whole inside the square (it stops growing; it is never squashed flat against
  an edge): every sound's path reaches 0.97 to 0.98 of the way to the edge (tests/host.c checks it at any size and
  rotation). Rates of half a beat to eight beats a round, the moon's suborbit visible on each.

## Paths you can morph (firmware/src/orbit.c)

Every path is one formula with continuous parameters, in five families: LISSAJOUS (x = cos(a t + phase),
y = sin(b t)), STAR (a radius r(theta) between a rose, 1 - d (1 - cos n theta) / 2, and a straight-edged polygon,
cos(pi / n) / cos(the angle from an edge's middle), with points pulled in by the depth d), SPIRO (an epicycle,
(1 - k) e^(i t) + k e^(-i m t + phase)), PULSE and ADSR. Where a formula needs whole numbers (a, b, n, m) the two
neighbours are blended, so each in-between figure is a closed curve and a turning knob morphs it; whole numbers sit
every 25 knob steps. FAMILY too is a position: two neighbouring families blended. The parameters glide (a one-pole,
~25 ms, every control tick), and the fit to the square follows (at most every 8 ticks: it samples the path 128
times, a star's tips exactly; the fitted scale glides as well, so the size never steps). The sun cannot jump: a knob turning slowly moves it at most 0.004 of the square a tick (tests/host.c).
The screen draws the path from the same gliding parameters: sampled finer where it bends or runs fast (no two
points more than ~1.5 pixels apart, up to 1024), each point spread over its four pixels (a soft, anti-aliased line),
recomputed ~11 times a second while it moves, only the 8 x 8 pixel tiles under the old and the new path drawn
again. Playing: ~60 field frames a second (the selected planet turning at ~8 frames a second, the moon's trail, the
planets drawn whole over the square's edge, included); while a fader morphs the whole figure ~25-30; with SPIN
turning the path while playing, ~55 (its path redrawn as often as the SPI can spare). See "The frame rate" below.

Measured (tests/host.c, one note held, effects off; numbers of this version):

| Sound | Centroid with the orbit (p10..p90) | Largest octave-band swing | Corners alone: centroids | Corners apart (mean per band) | Corner levels within |
|---|---|---|---|---|---|
| NEBULA PAD (circle) | 132..1740 Hz | 57 dB | 132 / 1755 / 497 / 797 Hz | 41 dB | 4 dB |
| ORBIT BASS (pulse) | 120..642 Hz | 59 dB | 131 / 2436 / 260 / 659 Hz | 36 dB | 6 dB |
| GLASS BELLS (ADSR) | 372..4058 Hz | 19 dB | 2460 / 404 / 155 / 3986 Hz | 25 dB | 6 dB |
| TALKING (figure eight) | 558..918 Hz | 18 dB | 557 / 497 / 881 / 920 Hz (vowels: the formants move, not the centroid) | 11 dB | 4 dB |
| DEEP DRONE (3:2 knot) | 76..3939 Hz | 52 dB | 90 / 113 / 4094 / 1811 Hz | 17 dB | 3 dB |
| PLUCK STEP (triangle) | 22..1562 Hz | 39 dB | 368 / 1757 / 136 / 798 Hz | 20 dB | 6 dB |
| SYNC LEAD (five-point star) | 259..2225 Hz | 24 dB | 551 / 908 / 2407 / 140 Hz | 40 dB | 3 dB |
| SPACE STATION (four-point star) | 374..6758 Hz | 25 dB | 7881 / 688 / 173 / 6823 Hz | 29 dB | 5 dB |

The first sounds, for comparison: the centroid moved 191..251 Hz (GLASS ORGAN), 623..821 (VOX DRIFT), 177..270
(DUAL STRINGS), 131..273 (PULSE BASS); corners 0.3 to 0.9 octaves apart.

## Built now (0.1) and later

Now, play first: everything above in a compact form, on one screen. Sound: 1-7 complete as listed; the arpeggiator
as up / down / up-down / random over the held keys, two octaves, sixteenths; randomize with one undo. Thirty-two factory
sounds in two banks (FACTORY 1..16, FACTORY 2 17..32: the basics), sixteen USER slots (the sound list, PATCH); < >
steps through the bank of the sound now.

FACTORY 2 keeps the rule (corners apart, a path that reaches them) for the sounds people expect first: strings, a PWM
pad, a choir, an organ (the drawbar wavetable), an electric piano, harp, marimba, music box (the struck ones walk the
ADSR path from their strike to their body), sub, FM and sequenced bass, an arpeggio, brass, a flute lead, wind, drums.
tests/host.c renders each as it is meant (chords, a bass line, a melody, held keys for the arpeggios) to
demo_NN_NAME.wav and measures the loudest 0.4 s, K-weighted (ITU-R BS.1770): FACTORY 2 within -2.1 .. +1.9 dB of
FACTORY 1's median (-17.7 LUFS; FACTORY 1 itself spans -9.6 .. +5.1 dB), peaks -4.6 dBFS or lower, the master's
limiter in gain under a quarter of the time, none silent; and each passes the orbit tests (paths whole, the spectrum
moving 12 dB or more, the corners 6 dB a band apart, their levels within 12 dB).
The store numbers the sounds as before: SETUP's START SOUND 0..15 FACTORY, 16..31 USER, 32..47 FACTORY 2 (an older
setting means what it meant); a USER slot's name is its factory sound 0..31.

Later: the full Corner Matrix (filters placed anywhere in the chain, the prime slot, the corner limiter), the arp's
8-step octave grid with chains and walks, routing (MIDI to parameters), randomizer scope, vibrato and tremolo,
tempo sync for the suborbit, chorus and delay options (dotted, band-pass in the loop, clear), stored user sounds and
global settings, MIDI clock.

## The engine (fixed point, firmware/src/engine.c, orbit.c, fx.c, mix.c)

- **Generators:** band-limited wavetables built by tools/gen_tables.py: per pattern ten octave bands (increments up
  to 2^(20+b)), 64..1024 samples, at least four samples per highest overtone, overtones up to 25 kHz at the band's
  top pitch (aliases land above 19 kHz). 50.8 KB, copied to RAM at start. Linear interpolation. Measured: the
  patterns' overtones within 0.03 of 1/n, C + D a sawtooth to 0.001, off-harmonic energy of a C6 / C7 saw -49 / -46 dB.
- **Wavetables (W1..W8):** 8 banks x 32 frames from tools/gen_tables.py recipes (additive spectra: a resonance
  scan, a pulse width, vowel formants, organ drawbars; waveforms in time whose harmonics a DFT takes: a hard-synced
  saw, 2-op FM, a stepped sine, phase distortion): original, nothing sampled. Each frame in six levels by octave
  band (32 harmonics in 128 samples for the notes up to 689 Hz, its top half faded; 16 in 128, 8 in 64, 4 in 32,
  2 in 32, 1 in 16 above), normalized (RMS 8000, peak at most 28000): 200 KB in the flash, read in place only as
  frames: a cache per voice, corner and generator in RAM (64 x 2 frames x 129 samples, 33.5 KB in .bss) holds the
  two frames around the position, loaded when the position crosses into another frame or the pitch into another
  band (a sweep through a whole bank: 62 loads in 4 s); per sample both frames are read (linear, as the overtone
  tables) and morphed by the position's fraction, ramped over the block. The position: WT POS + WT ORBIT x the moon's
  nearness to the planet (the plain bilinear weight), every control tick, gliding (1/4 a tick). Measured (tests/host.c):
  every bank, every frame, C4..C7: the interpolation's mirror images at least 31 dB below (the overtone patterns:
  46 dB); a knob sweeping the position: the centroid 163 .. 1421 Hz (SCAN), no clicks; the moon moving the
  position over 25 frames. A corner with a wavetable renders in corner_render_wt (the classic corners as before,
  bit for bit).
- **One phase per voice** running at 1/60 of the note: the generators' ratios 1, 1/2 .. 1/5 are exact multiples
  (60, 30, 20, 15, 12), so all generators of a voice stay phase-locked. That is what makes added patterns cancel.
- **Blend:** added: G1 + a (G2 - its fundamental); AM: G1 crossfaded to G1 x G2; PM: G1 read at its phase + a x G2
  (up to 3/4 of a cycle); PM+AM; PD+AM: generator 2's phase bent at a knee moving with the amount, then AM; rPD the
  mirror knee.
- **Filters:** TPT state-variable filters (A. Simper's form), 64-bit products, bounded states. All corners of a voice
  share the orbit-weighted cutoff and resonance, and the filters are linear, so corners of the same type are summed
  first and filtered once: exact, and typical sounds run one or two filters per voice instead of four. LP24 = two
  SVFs, VOX = two band-passes on the U E O I A formants, resonance = how much.
- **Control rate:** every 32 samples (0.73 ms): envelopes, pitch (glide, bend, POLY drift), the orbit (path, place,
  suborbit, focus), the weights (bilinear in the moon's place, squared and normalized; level, mute) and the filter coefficients.
  Weights, amplitude and pan ramp per sample; cutoff glides. Silent corners are skipped.
- **Stereo (WIDTH):** a voice's place = its spread (STEREO: the stack, the follower, POLY's scatter) + WIDTH x the
  orbit's side, (the heard planets' sides weighted by their weights + the moon's x) / 2, every control tick; constant
  power (a quarter sine), the gains ramped over the block. One pan per voice, not per corner: the corners share
  their filters, a pan per corner would filter twice. WIDTH 0 gives the bits of before. tests/host.c: WIDTH 0 L == R;
  full: the balance follows the orbit (correlation 1.00), |L + R| never below 0.99 of |L| + |R|. The effects take
  the stereo bus as it is (drive per side; chorus, delay and plate fed L + R, their returns stereo).
- **Voices:** 8 rendered at most. A CPU guard in the audio interrupt lowers the limit when a half buffer takes over
  85 % of its time and raises it again after 2.3 s under 55 %.
- **After the voices:** drive (band-pass into tanh beside the dry), chorus (four moving taps, stereo), delay
  (ping-pong, tempo divisions 1/16 .. 3/4 up to 0.74 s, darkening repeats; full feedback: a loop), the plate reverb
  (Dattorro, half rate), volume, then the master from Melodee37 (DC blocker, peak limiter, soft clip). The volume
  is one 64-bit product on the full bus, rounded, and the limiter's gain one 64-bit product too (0.3.2; before, the
  bus was shifted right by 5 first: soft notes and reverb tails sat on an ~11-bit grid, a released note left a
  truncation residue). ~10 instructions more a stereo sample, ~0.4 M a second.
- **Pad light** (SETUP K3, PAD LIGHT 25 / 50 / 75 / 100 %, at first 75 %): the pads' levels (ui.c PK_*) at 100 %
  are 0.3.1's PAD LIGHT 8; a step scales a colour's brightest component v by s + (1 - s) * K / (v + K) (K = PL_KNEE,
  an unselected corner's level), so the bright states follow the setting and the dim ones keep most of their light:
  at 25 % the dimmest pad (an empty USER slot) keeps ~75 % of its light (9 of 255), a dim one stays ~1.5 x it and a
  full one ~2.8 x a dim one (tests/screen.c checks every step). 0.3.1's 1..8 are read as the step above (kept as 2,
  4, 6, 8: 0.3.1 reads the same record). The console's padtest sets the LEDs as given (a hardware check).

## The screen (firmware/src/draw.c)

By meaning, top to bottom (the path drawing: see "Paths you can morph"): the top line says what a pad, fader or
button did ("PAD FILTER 3: LP24", "F2 SIZE 80", "FX: MIXER KNOBS"), else the sound and the level. The orbit
leads: a 176 x 176 square in a night sky that fills the screen between the top and the bottom line (twinkling
stars, a dotted path, the four planets glowing with their weights, the selected one zoomed up in a red ring, the sun
with its trace (the path itself behind it, fading, smooth at any speed), the moon on its ring, every other voice a
smaller pair). The path, the suns and the moons stay in the square; the planets, their bubbles and the central box
reach over its edge into the sky, drawn whole. A planet's
values live in its speech bubble (its tail round the planet, its colour): picked or turned, the overview of its eight
knobs (pictures: the patterns' waves, the ratio, a BLEND dial, the modulated wave, the filter's curve, a level bar;
the turned one large); a MODE, FILTER or MUTE pad shows its list. Everything global lives in the central box (square,
blue-white, a small sun's mark): an open knob set, a fader's value, the families as a list that scrolls with
fader 1, and with no planet selected the orbit's knob turned. At the bottom a strip with the path and the moon in
small pictures (family, size circle, speed arrow, the figure now, warp, the moon on its ring). KNOB
BANK opens the guide: drawings of the orbit, the panel, the pad groups, a corner, the families, and the buttons; K1
scrolls it. Only what moved is drawn again, in 8 x 8 tiles rendered from the scene, and of those only the tiles that
came out different are sent: about 60 field frames a second while four voices play (tests/fps.c times it).

### The frame rate

One core runs it all (core 1 is not started): the audio interrupt (ALNK0, above the main loop; the TIMER5 scan above
it) and the main loop, which reads the panel and draws. The LCD is an ST7789 on SPI1 at 12 MHz (lcd.c LCD_BAUD 4, the
stock firmware's rate; `LCD_BAUD=2 sh build.sh` builds 20 MHz, untested on the SMK-37): 0.67 us a byte, a whole screen
77 ms. So the screen sends only what changed, and draws while it sends:

- **Dirty tiles.** Whatever moved marks the 8 x 8 tiles it touched, where it was and where it is (draw.c rq_add); a
  run of marked tiles is carried down the rows that have it too, one rectangle each (fewer, taller rectangles: each
  one renders the scene once, clipped).
- **Only changed tiles go out.** Each tile on the screen has a hash of its pixels (field_th, 3 KB); a drawn strip
  sends, per row of tiles, the span from the first to the last tile that differs, the spans moved together in the
  canvas so that rows with the same span go in one transfer. While playing about half of the drawn tiles are sent.
- **Draw while the SPI sends.** The strips are drawn into the two halves of the canvas in turn (gfx.c cv_begin2):
  lcd.c waits only when a transfer still reads the half about to be drawn (lcd_sync_buf), so the next strip is drawn
  while the DMA sends this one. The pad LEDs share SPI1 (another pin group): they go out between frames, after the
  last transfer (padled_flush).
- **Only the canvas' part is visited.** Glows, discs, a planet's body, the bubbles' triangles and text visit only the
  rows and columns the strip holds; the path skips 32 points at a time whose box misses the strip, the moon's trail each piece whose box misses it;
  the stars' places are a table; the glow's falloff is two multiplications instead of two divisions a pixel (exact:
  the reciprocal rounded up is exact while the dividend times the divisor stays under 2^32). The screens are the same
  bit for bit: built with the old pace (`-DUI_FIELD_MS=28u -DUI_LOOP_MS=15u`), tests/screen.c's 76 screens match
  tests/golden.txt (e2ceaa0's).
- **The pace.** The main loop runs at most every 16 ms, the field at most every 16 ms (UI_LOOP_MS, UI_FIELD_MS in
  draw.c; before: 15 and 28, a field frame every 30 ms); a frame that takes longer is followed at once. What moved by
  the frame now moves by the time (a planet's zoom, the stars' twinkle, the sun's tail and the moon's trail: a place
  every ~30 ms), so the screen looks the same, only smoother.
- **The moon's trail follows its curve.** A place every ~30 ms; where two places lie more than 3 pixels apart (a
  fast moon: it goes round the sun, the sun along the path, between them) the way between is filled in once, when
  the piece is complete, from the path and the moon's ring (the phases kept with each place, the speeds, SPIN turned
  back), a point every 2 pixels at most; the piece to the moon anew each frame. So it is one line along the way the
  moon went, never straight chords across the orbit (0.3 drew those, and left pieces behind as they aged out: the
  oldest piece's tiles were not marked). A very fast moon's filled-in way is kept to about 480 pixels.
  tests/screen.c checks it at MOON SPEED 127 (no piece over 3 pixels, nothing of it left on the screen).

tests/fps.c runs the firmware's ui.c and draw.c on the simulated panel with the main loop timed as the device would
run it: the drawing's host time x K (K: the device's time for the host's on a fixed piece of the earlier drawing, its
instructions counted in build/vector37.dis, one a cycle at 320 MHz, as the budget below), stretched by the audio's
share of the core; the SPI at 12 MHz, one transfer at a time, the drawing waiting where lcd.c waits. Model numbers,
not measured on the device (the console's `cpu` gives the audio's share there):

| | before: fps | now: fps | drawing a frame (ms) before / now | SPI a frame (ms) before / now | core 0: audio + UI, before / now |
|---|---|---|---|---|---|
| idle orbit | 29 | 62 | 10.4 / 4.8 | 9.0 / 3.6 | 9 + 30 % / 9 + 30 % |
| four notes | 29 | 62 | 10.8 / 4.5 | 8.0 / 3.4 | 30 + 31 % / 30 + 28 % |
| a planet selected, playing | 23 | 57 | 18.9 / 7.8 | 15.0 / 4.4 | 30 + 43 % / 30 + 44 % |
| a planet's knob turning | 6 | 36 | 83.0 / 15.7 | 52.0 / 7.8 | 30 + 50 % / 30 + 57 % |
| a fader morphing the path | 12 | 26-31 | 31.4 / 15.7-19 | 41.7 / 17-18 | 30 + 38 % / 30 + 49 % |
| SPIN, playing | 20 | 57 | 22.6 / 8.0 | 14.4 / 4.4 | 30 + 44 % / 30 + 45 % |

The UI's share is what the loop works, not idles (it polls the panel in between); at twice the frames it costs
about the same as before. The audio interrupt outranks the loop: a slow frame delays the screen, never the sound.

## Controls

docs/CONTROLS.md: one table, control -> action -> what the screen shows. The pads and all eight knobs are the planets:
four pad groups of 2 x 2 (SELECT, MODE, FILTER, MUTE), each laid out like the screen's corners, steady colours; one
press, one step; the knobs edit the selected planets from source to level (GEN 1, GEN 2, TUNE 2, BLEND, MODE, CUTOFF,
RESO, LEVEL), several at once each from its own value (their differences kept; one bubble with a tail to each, a dot
per planet on each cell's track); a selected planet's pad tapped again takes it out, and with none selected (as after
switching on and after a new sound) the knobs are the orbit and the moon (WARP, WARP Y, ROTATE, CONTRAST, MOON DIST,
MOON SPEED, RESTART, BPM; the path or the moon's ring lights up as they change); MUTE shows no bubble, the planet flashes and collapses to an outline, or ignites. The four faders are
the path: FAMILY (Lissajous, spiro, star, pulse and ADSR, with detents: a plateau of 70 % per family where the figure
is exact, short blended morphs between), SIZE, SPEED, MORPH. The mod wheel is the moon's speed (its place is the
value, the moon's ring lights up while it moves; K6 with no planet selected is the same value). A new sound brings its own path values; the first deliberate move of a
fader makes its value glide to the fader (~0.4 s, the path morphs on screen), then it follows; a one-step wobble at
rest is ignored and never starts a glide (tests/screen.c checks both, and the host test that the sun never jumps
while FAMILY moves slowly). The bottom strip always shows the path and the moon: family, size, speed, morph,
warp, the moon (its ring as wide as its distance, the bar its speed); values as bars, the faders and the wheel as
ticks. SEQ: FREEZE (the orbit stands where it is). The mixer is the FX knob set. A set's button tapped leaves its set open (its light lit) until tapped again; held
while a knob turns, the set is lent only while held.

Editing a planet is visual and audible: the selected planet zooms up (the others shrink and dim), turns slowly, and
draws its eight settings (stripes = GEN 1, a lower band = GEN 2, turning speed = TUNE 2, a ring = BLEND, solid when
added, dashed by MODE when modulated, glow = CUTOFF, rim sparkles = RESO, brightness = LEVEL), with eight satellites in
the knobs' colours (the knob labels below share them) whose size is each value; the turned knob's satellite swells.
Values and pad lists appear in a speech bubble whose tail wraps the planet, in its colour. AUDITION: with no key
held, a changed planet sounds alone (its corner soloed, C3), held while it changes, let go 0.5 s after; on / off on
the MORE knob set (FADER BANK, K8).

## Budget (estimates: the CPU load is not measured on the device yet)

| | |
|---|---|
| App image | 171 KB (slot 605 KB) |
| RAM .data + .bss | 43.8 KB of 96 KB |
| RAM pool | 310 KB of 344 KB: overtone tables 50.8, delay 128, plate 83, screen canvas 58 |
| Wavetable banks | 200 KB in the flash (app image 384 KB of the 605 KB slot); their frame caches 33.5 KB in .bss (.data + .bss 82 KB of 96) |

CPU, from the instruction counts of the hot loops in the firmware's own pi32v2 code (build/vector37.dis): a corner
16-25 (generator 2) + 10-39 (blend) + 10 (its weighted sum) per sample; a filter type in use 45 (LP24 90, VOX 108);
the voice's stereo out ~20. A typical voice (two filter types) ~380 instructions a sample, the worst (PD / PM blends,
four different filter types) ~650; the effects and the master ~620. Eight voices: ~170 M instructions a second
typical, ~260 M worst. At an assumed 320 MHz (the JieLi SDK's usual system clock; our package does not set one) and
about one instruction a cycle: ~53 % typical, ~80 % worst; at 240 MHz the typical case is ~70 %. The CPU guard keeps
the worst case playable by dropping voices. Host: eight voices + effects cost 1.2 % of real time on a desktop CPU.
0.3.2's volume and limiter (64-bit, rounded): the output loop ~10 instructions more a stereo sample (~0.4 M a second,
~0.1 % at 320 MHz); host 1.3 % against 1.4 % for 0.3.1 (best of three, the same machine): no difference measurable.
A wavetable generator per sample: two table reads and the morph, ~40-54 instructions in the firmware's loops
(build/vector37.dis) against ~16-27 for an overtone pattern: a sound with a wavetable in every corner's GEN 1 costs
~100 instructions a sample more per voice (8 voices: ~35 M a second more); every generator a wavetable (8 voices) the
CPU guard would trim. Host: x 1.0-1.3 the classic patterns (tests/host.c checks it stays under x 1.6).
**Measure first on the device:** the console's `cpu` while holding eight notes of each sound.
