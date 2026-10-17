// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 M. Geisler (makkuro)
// Vector37 in an AudioWorklet: sim.wasm is the firmware's engine, UI and screen against a simulated SMK-37 Pro.
// Each render quantum runs the device's clock; ~30 times a second the screen, the pads and the LEDs go out; once a
// second, if the store (your sounds, the settings) changed, its bytes go to the page to keep.
class Vector37 extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.sent = -1;
    this.last = 0;
    this.lastKeep = 0;
    this.kept = null;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }
  async onMessage(m) {
    if (m.type === "load") {
      try {
        const mod = await WebAssembly.compile(m.wasm);
        const imports = {};
        for (const i of WebAssembly.Module.imports(mod)) {       // (none expected; stubs if the toolchain adds any)
          imports[i.module] = imports[i.module] || {};
          if (i.kind === "function") imports[i.module][i.name] = () => 0;
        }
        const inst = await WebAssembly.instantiate(mod, imports);
        this.ex = inst.exports;
        if (this.ex._initialize) this.ex._initialize();
        this.ex.sim_init();
        if (m.store && m.store.byteLength === this.ex.sim_store_size()) {   // (your sounds from the last visit)
          new Uint8Array(this.ex.memory.buffer, this.ex.sim_store(), m.store.byteLength).set(new Uint8Array(m.store));
          this.ex.sim_restart();
        }
        this.kept = this.store().slice();
        this.port.postMessage({ type: "ready", rate: this.ex.sim_rate() });
      } catch (err) {
        this.port.postMessage({ type: "error", message: String((err && err.message) || err) });
      }
      return;
    }
    const ex = this.ex;
    if (!ex) return;
    if (m.type === "pad") ex.sim_pad(m.pad, m.vel);
    else if (m.type === "key") ex.sim_key(m.key, m.down ? 1 : 0);
    else if (m.type === "button") ex.sim_button(m.b, m.down ? 1 : 0);
    else if (m.type === "enc") ex.sim_enc(m.e, m.steps | 0);
    else if (m.type === "fader") ex.sim_fader(m.i, m.v);
    else if (m.type === "wheel") ex.sim_wheel(m.i, m.raw);
    else if (m.type === "midi") ex.sim_midi(m.d[0], m.d[1], m.d[2]);
  }
  store() { return new Uint8Array(this.ex.memory.buffer, this.ex.sim_store(), this.ex.sim_store_size()); }
  keep() {                                                    // (a SAVE or a setting written: to the page, to keep)
    const now = this.store(), was = this.kept;
    for (let i = 0; i < now.length; i++)
      if (now[i] !== was[i]) {
        this.kept = now.slice();
        const copy = this.kept.slice();
        this.port.postMessage({ type: "store", store: copy.buffer }, [copy.buffer]);
        return;
      }
  }
  publish() {
    const ex = this.ex, mem = ex.memory.buffer, msg = { type: "frame" }, tr = [];
    const d = ex.sim_draws();
    if (d !== this.sent) {
      this.sent = d;
      msg.fb = new Uint16Array(mem, ex.sim_fb(), 240 * 240).slice();
      tr.push(msg.fb.buffer);
    }
    msg.pads = Array.from(new Uint32Array(mem, ex.sim_pads(), 16));
    msg.leds = ex.sim_leds();
    msg.ctl = [0, 1, 2, 3, 4].map((i) => ex.sim_ctl(i));   // (the faders' and the mod wheel's places: the values)
    this.port.postMessage(msg, tr);
  }
  process(inputs, outputs) {
    const ex = this.ex;
    if (!ex) return true;
    const out = outputs[0], n = out[0].length;
    ex.sim_render(n);
    out[0].set(new Float32Array(ex.memory.buffer, ex.sim_out_l(), n));
    if (out[1]) out[1].set(new Float32Array(ex.memory.buffer, ex.sim_out_r(), n));
    if (currentTime - this.last >= 1 / 30) {
      this.last = currentTime;
      this.publish();
    }
    if (currentTime - this.lastKeep >= 1) {
      this.lastKeep = currentTime;
      this.keep();
    }
    return true;
  }
}
registerProcessor("vector37", Vector37);
