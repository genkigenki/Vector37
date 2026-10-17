// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 M. Geisler (makkuro)
// The simulator's wasm in Node, as the worklet runs it: boots silent, a chord sounds (audio, the orbit on the screen,
// the pads follow the corners), a corner knob and pad do their job, a held button lends the knobs its set, < > the
// sounds; cost against real time; the screen to OUT.ppm; then the sound list (PATCH: the banks, a FACTORY 2 sound, a
// pad held: SAVE), BT's SETUP, and the store given back after a restart (as the page keeps it).
//   node web/sim/sim_test.mjs build/sim/sim.wasm [OUT.ppm]
import fs from "fs";
const bytes = fs.readFileSync(process.argv[2] || "build/sim/sim.wasm");
const { instance } = await WebAssembly.instantiate(bytes, {});
const ex = instance.exports;
if (ex._initialize) ex._initialize();
ex.sim_init();
let fails = 0, peak = 0;
const check = (what, ok) => { console.log(`sim: ${what.padEnd(60)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };
function run(sec) {
  const t0 = performance.now(), n = Math.round(sec * 44100 / 128);
  for (let i = 0; i < n; i++) {
    ex.sim_render(128);
    const l = new Float32Array(ex.memory.buffer, ex.sim_out_l(), 128);
    for (const x of l) peak = Math.max(peak, Math.abs(x));
  }
  return (performance.now() - t0) / (sec * 1000);
}
const tap = (b) => { ex.sim_button(b, 1); run(0.05); ex.sim_button(b, 0); run(0.05); };
run(0.5);
check("silent at power-on", peak < 0.001);
for (const k of [12, 16, 19, 23]) ex.sim_key(k, 1);
const load = run(4);
check(`a chord sounds (peak ${peak.toFixed(2)})`, peak > 0.1 && peak <= 1.0 && ex.sim_voices() === 4);
const fb = new Uint16Array(ex.memory.buffer, ex.sim_fb(), 240 * 240);
let lit = 0;
for (let i = 0; i < 240 * 240; i++) if (fb[i] > 0x2000) lit++;
check(`the orbit on the screen (${lit} bright pixels; no planet zoomed at power-on)`, lit > 1500);
const pads = new Uint32Array(ex.memory.buffer, ex.sim_pads(), 16);
check("the pads glow, four groups, four colours", [0, 2, 4, 6].every((p) => pads[p] !== 0) && new Set([pads[1], pads[2], pads[4], pads[6]]).size === 4);
check("switched on: nothing selected, the knobs are the orbit", (ex.sim_ui() >> 16) === 1 && ((ex.sim_ui() >> 8) & 255) === 0);
const warp = ex.sim_param(0, 1);
ex.sim_enc(0, 3); run(0.2);
check("K1, no planet selected: WARP", ex.sim_param(0, 1) === warp + 3);
ex.sim_pad(0, 110); run(0.05); ex.sim_pad(0, 0); run(0.05);   // SELECT group, top left: planet 1
check("SELECT pad, top left: planet 1, its knobs", ((ex.sim_ui() >> 8) & 255) === 1 && (ex.sim_ui() >> 16) === 0);
const cut = ex.sim_param(1, 3);
ex.sim_enc(5, 4); run(0.2);
check("K6: corner 1's cutoff up", ex.sim_param(1, 3) === cut + 4);
const mode = ex.sim_param(2, 5);
ex.sim_pad(3, 110); run(0.05); ex.sim_pad(3, 0); run(0.05);   // MODE group, top right: corner 2
check("MODE pad, top right: corner 2's next mode", ex.sim_param(2, 5) === (mode + 1) % 5);
ex.sim_pad(9, 110); run(0.05); ex.sim_pad(9, 0); run(0.05);   // SELECT group, bottom right: corner 4
check("SELECT pad, bottom right: corner 4 selected", ((ex.sim_ui() >> 8) & 255) === 8);
ex.sim_pad(9, 110); run(0.05); ex.sim_pad(9, 0); run(0.05);   // the same pad again: out, the orbit's knobs
check("... tapped again: none selected, the knobs are the orbit", ((ex.sim_ui() >> 8) & 255) === 0 && (ex.sim_ui() >> 16) === 1);
tap(3);
check("> : the next sound", (ex.sim_ui() & 255) === 1);
{   // the page's faders and mod wheel stand where the sound's values are; a touch from there moves on, no jump
  const size = ex.sim_param(0, 4), spd = ex.sim_param(0, 9);
  check(`faders in sync: SIZE ${size} at ${ex.sim_ctl(1)}, MOON SPEED ${spd} at ${ex.sim_ctl(4)}`,
        ex.sim_ctl(1) === size && ex.sim_ctl(4) === 12 + Math.floor((spd * 118 + 63) / 127));
  const morph = ex.sim_param(0, 42), f = ex.sim_ctl(3);   // (G_MORPH; -64..63 over the fader's 0..127)
  ex.sim_fader(3, f - 1); run(0.6);
  check(`... MORPH's handle (${f}) touched at its place: ${morph} -> ${ex.sim_param(0, 42)}, no jump`,
        Math.abs(ex.sim_param(0, 42) - morph) <= 2);
}
for (const k of [12, 16, 19, 23]) ex.sim_key(k, 0);
run(0.5);
check(`cost: ${(load * 100).toFixed(1)} % of real time`, load < 0.5);
if (process.argv[3]) {
  const out = Buffer.alloc(240 * 240 * 3);
  for (let i = 0; i < 240 * 240; i++) {
    const c = fb[i];
    out[3 * i] = ((c >> 11) & 31) * 255 / 31; out[3 * i + 1] = ((c >> 5) & 63) * 255 / 63; out[3 * i + 2] = (c & 31) * 255 / 31;
  }
  fs.writeFileSync(process.argv[3], Buffer.concat([Buffer.from("P6\n240 240\n255\n"), out]));
}
{   // the sound list (PATCH): the banks with < >, a FACTORY 2 sound from its pad, a pad held: SAVE into USER, kept
  const snd = () => { const v = ex.sim_sound(); return { list: v >>> 24, lbank: (v >> 16) & 255, bank: (v >> 8) & 255, slot: v & 255 }; };
  tap(12); run(0.3);
  check("PATCH: the sound list open, its light", snd().list === 1 && ((ex.sim_leds() >> 12) & 1) === 1);
  tap(3); tap(3); run(0.2);
  check("... > > : FACTORY, USER, FACTORY 2 shown", snd().lbank === 2);
  ex.sim_pad(0, 110); run(0.05); ex.sim_pad(0, 0); run(0.2);
  check("... pad 1: FACTORY 2's first sound (17)", snd().bank === 2 && snd().slot === 0);
  peak = 0;
  ex.sim_key(19, 1); run(1.5); ex.sim_key(19, 0); run(0.3);
  check(`... it plays (peak ${peak.toFixed(2)})`, peak > 0.05 && peak <= 1.0);
  const empty = ex.sim_store_size(), st0 = new Uint8Array(ex.memory.buffer, ex.sim_store(), empty).slice();
  ex.sim_pad(4, 110); run(1.3); ex.sim_pad(4, 0); run(0.2);
  const st1 = new Uint8Array(ex.memory.buffer, ex.sim_store(), empty).slice();
  check("... pad 5 held 1 s: SAVED into U5, the USER bank shown", snd().bank === 1 && snd().slot === 4 && snd().lbank === 1
        && st1.some((x, i) => x !== st0[i]));
  tap(11); run(0.2);
  check("BT: the SETUP set, its light", ((ex.sim_leds() >> 11) & 1) === 1);
  // the next visit: a new unit given the kept store, then a power cycle; U5 is there
  const { instance: i2 } = await WebAssembly.instantiate(bytes, {});
  const e2 = i2.exports;
  if (e2._initialize) e2._initialize();
  e2.sim_init();
  new Uint8Array(e2.memory.buffer, e2.sim_store(), empty).set(st1);
  e2.sim_restart();
  const r2 = (sec) => { for (let i = 0; i < Math.round(sec * 44100 / 128); i++) e2.sim_render(128); };
  const t2 = (b) => { e2.sim_button(b, 1); r2(0.05); e2.sim_button(b, 0); r2(0.05); };
  r2(0.3); t2(12); r2(0.3); t2(3); r2(0.2);
  e2.sim_pad(4, 110); r2(0.05); e2.sim_pad(4, 0); r2(0.2);
  const v2 = e2.sim_sound();
  check("the store kept (the page's localStorage): after a restart U5 plays", ((v2 >> 8) & 255) === 1 && (v2 & 255) === 4);
}
console.log(fails ? `sim: ${fails} FAILED` : "sim: all ok");
process.exit(fails ? 1 : 0);
