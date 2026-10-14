// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 M. Geisler (makkuro)
// The simulator's wasm in Node, as the worklet runs it: boots silent, a chord sounds (audio, the orbit on the screen,
// the pads follow the corners), a corner knob and pad do their job, a held button lends the knobs its set, < > the
// sounds; cost against real time; the screen to OUT.ppm.
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
console.log(fails ? `sim: ${fails} FAILED` : "sim: all ok");
process.exit(fails ? 1 : 0);
