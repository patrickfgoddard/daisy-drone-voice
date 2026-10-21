// DroneVoice audio engine — a JavaScript port of the Daisy Pod firmware's DSP.
//
// The same file runs in two places:
//   * inside an AudioWorklet (registers the "drone-voice" processor), and
//   * on the main thread as a fallback (exposes DroneEngine for a
//     ScriptProcessorNode), e.g. when the page is opened from file://.
//
// Ports of DaisySP code:
//   Oscillator (polyBLEP), SmoothRandomGenerator, fonepole, SoftClip, mtof
//     DaisySP, MIT licence, (c) Electrosmith
//   LadderFilter
//     MIT licence, (c) 2021 Richard van Hoesel, (c) 2024 Infrasonic Audio LLC
//   ReverbSc
//     LGPL v2.1, (c) 2023 Electrosmith, Sean Costello, Istvan Varga,
//     Paul Batchelor. See LICENSE-LGPL in the repository root.

(function () {
  "use strict";

  const TWOPI = Math.PI * 2;
  const HALFPI = Math.PI * 0.5;
  const BLOCK = 48; // control-rate block, matches pod.SetAudioBlockSize(48)

  const mtof = (m) => Math.pow(2, (m - 69) / 12) * 440;
  const clamp = (x, lo, hi) => (x < lo ? lo : x > hi ? hi : x);
  const expMap = (x, lo, hi) => lo * Math.pow(hi / lo, x);

  function softClip(x) {
    if (x < -3) return -1;
    if (x > 3) return 1;
    return (x * (27 + x * x)) / (27 + 9 * x * x);
  }

  // ------------------------------------------------------------ Oscillator
  function polyblep(dt, t) {
    if (t < dt) {
      t /= dt;
      return t + t - t * t - 1;
    } else if (t > 1 - dt) {
      t = (t - 1) / dt;
      return t * t + t + t + 1;
    }
    return 0;
  }
  const fastmod1 = (x) => x - Math.floor(x);

  // wave: 0 polyBLEP saw, 1 polyBLEP square, 2 polyBLEP triangle, 3 sine
  class Osc {
    constructor(sr) {
      this.sr = sr;
      this.phase = 0;
      this.inc = 0;
      this.wave = 0;
      this.amp = 1;
      this.pw = 0.5;
      this.last = 0;
    }
    setFreq(f) {
      this.inc = f / this.sr;
    }
    process() {
      const p = this.phase;
      const dt = this.inc;
      let out;
      switch (this.wave) {
        case 0:
          out = 2 * p - 1;
          out -= polyblep(dt, p);
          out *= -1;
          break;
        case 1:
          out = p < this.pw ? 1 : -1;
          out += polyblep(dt, p);
          out -= polyblep(dt, fastmod1(p + (1 - this.pw)));
          out *= 0.707;
          break;
        case 2:
          out = p < 0.5 ? 1 : -1;
          out += polyblep(dt, p);
          out -= polyblep(dt, fastmod1(p + 0.5));
          out = dt * out + (1 - dt) * this.last; // leaky integrator
          this.last = out;
          out *= 4;
          break;
        default:
          out = Math.sin(p * TWOPI);
      }
      this.phase += dt;
      if (this.phase > 1) this.phase -= 1;
      return out * this.amp;
    }
  }

  // ------------------------------------------------- SmoothRandomGenerator
  class SmoothRandom {
    constructor(rate) {
      this.rate = rate;
      this.freq = 1 / rate;
      this.phase = 0;
      this.from = 0;
      this.interval = 0;
    }
    setFreq(f) {
      this.freq = clamp(f / this.rate, 0, 1);
    }
    process() {
      this.phase += this.freq;
      if (this.phase >= 1) {
        this.phase -= 1;
        this.from += this.interval;
        this.interval = Math.random() * 2 - 1 - this.from;
      }
      const t = this.phase * this.phase * (3 - 2 * this.phase);
      return this.from + this.interval * t;
    }
  }

  // ------------------------------------------------------- LadderFilter
  // Huovilainen New Moog model, 4x linear oversampling, LP24 output.
  function fastTanh(x) {
    if (x > 3) return 1;
    if (x < -3) return -1;
    const x2 = x * x;
    return (x * (27 + x2)) / (27 + 9 * x2);
  }

  class Ladder {
    constructor(sr) {
      this.sr = sr;
      this.osRecip = 1 / (sr * 4);
      this.z0 = new Float64Array(4);
      this.z1 = new Float64Array(4);
      this.alpha = 1;
      this.K = 1;
      this.Q = 1;
      this.old = 0;
      this.pbg = 0.5;
      this.drive = 0.5;
      this.driveScaled = 0.5;
      this.setPassbandGain(0.5);
      this.setInputDrive(0.5);
      this.setFreq(5000);
      this.setRes(0.2);
    }
    setFreq(freq) {
      freq = clamp(freq, 5, this.sr * 0.425);
      const wc = freq * TWOPI * this.osRecip;
      const wc2 = wc * wc;
      this.alpha = 0.9892 * wc - 0.4324 * wc2 + 0.1381 * wc * wc2 - 0.0202 * wc2 * wc2;
      this.Q = 1.006 + 0.0536 * wc - 0.095 * wc2 - 0.05 * wc2 * wc2;
    }
    setRes(r) {
      this.K = 4 * clamp(r, 0, 1.8);
    }
    setPassbandGain(g) {
      this.pbg = clamp(g, 0, 0.5);
      this.setInputDrive(this.drive);
    }
    setInputDrive(d) {
      this.drive = Math.max(d, 0);
      if (this.drive > 1) {
        this.drive = Math.min(this.drive, 4);
        this.driveScaled = 1 + (this.drive - 1) * (1 - this.pbg);
      } else {
        this.driveScaled = this.drive;
      }
    }
    lpf(s, i) {
      let ft = s * 0.76923077 + 0.23076923 * this.z0[i] - this.z1[i];
      ft = ft * this.alpha + this.z1[i];
      this.z1[i] = ft;
      this.z0[i] = s;
      return ft;
    }
    process(inp) {
      const input = inp * this.driveScaled;
      let total = 0;
      let interp = 0;
      for (let os = 0; os < 4; os++) {
        const x = interp * this.old + (1 - interp) * input;
        let u = x - (this.z1[3] - this.pbg * x) * this.K * this.Q;
        u = fastTanh(u);
        const s1 = this.lpf(u, 0);
        const s2 = this.lpf(s1, 1);
        const s3 = this.lpf(s2, 2);
        const s4 = this.lpf(s3, 3);
        total += s4 * 0.25;
        interp += 0.25;
      }
      this.old = input;
      return total;
    }
  }

  // ------------------------------------------------------------ ReverbSc
  const REV_PARAMS = [
    [2473 / 48000, 0.001, 3.1, 1966],
    [2767 / 48000, 0.0011, 3.5, 29491],
    [3217 / 48000, 0.0017, 1.11, 22937],
    [3557 / 48000, 0.0006, 3.973, 9830],
    [3907 / 48000, 0.001, 2.341, 20643],
    [4127 / 48000, 0.0011, 1.897, 22937],
    [2143 / 48000, 0.0017, 0.891, 29491],
    [1933 / 48000, 0.0006, 3.221, 14417],
  ];
  const SCALE = 0x10000000;
  const OUT_GAIN = 0.35;
  const JP_SCALE = 0.25;

  class ReverbSc {
    constructor(sr) {
      this.sr = sr;
      this.feedback = 0.97;
      this.lpfreq = 10000;
      this.prvLp = 0;
      this.damp = 1;
      this.lines = REV_PARAMS.map((p, n) => this.initLine(n));
    }
    initLine(n) {
      const p = REV_PARAMS[n];
      const size = Math.floor((p[0] + p[1] * 1.125) * this.sr + 16.5);
      const lp = {
        buf: new Float32Array(size),
        size,
        writePos: 0,
        seed: Math.floor(p[3] + 0.5),
        readPos: 0,
        frac: 0,
        fracInc: 0,
        cnt: 0,
        state: 0,
      };
      let rp = (lp.seed * p[1]) / 32768;
      rp = p[0] + rp;
      rp = size - rp * this.sr;
      lp.readPos = Math.trunc(rp);
      lp.frac = Math.trunc((rp - lp.readPos) * SCALE + 0.5);
      this.nextSeg(lp, n);
      return lp;
    }
    nextSeg(lp, n) {
      const p = REV_PARAMS[n];
      if (lp.seed < 0) lp.seed += 0x10000;
      lp.seed = (lp.seed * 15625 + 1) & 0xffff;
      if (lp.seed >= 0x8000) lp.seed -= 0x10000;
      lp.cnt = Math.trunc(this.sr / p[2] + 0.5);
      let prv = lp.writePos - (lp.readPos + lp.frac / SCALE);
      while (prv < 0) prv += lp.size;
      prv /= this.sr;
      let nxt = (lp.seed * p[1]) / 32768;
      nxt = p[0] + nxt;
      let inc = (prv - nxt) / lp.cnt;
      inc = inc * this.sr + 1;
      lp.fracInc = Math.trunc(inc * SCALE + 0.5);
    }
    process(inL, inR, out) {
      if (this.lpfreq !== this.prvLp) {
        this.prvLp = this.lpfreq;
        const d = 2 - Math.cos((this.prvLp * TWOPI) / this.sr);
        this.damp = d - Math.sqrt(d * d - 1);
      }
      const damp = this.damp;
      const lines = this.lines;
      let aL = 0;
      for (let n = 0; n < 8; n++) aL += lines[n].state;
      aL *= JP_SCALE;
      const aR = aL + inR;
      aL = aL + inL;
      let oL = 0;
      let oR = 0;
      for (let n = 0; n < 8; n++) {
        const lp = lines[n];
        const size = lp.size;
        const buf = lp.buf;
        buf[lp.writePos] = (n & 1 ? aR : aL) - lp.state;
        if (++lp.writePos >= size) lp.writePos -= size;

        if (lp.frac >= SCALE) {
          lp.readPos += Math.floor(lp.frac / SCALE);
          lp.frac %= SCALE;
        }
        if (lp.readPos >= size) lp.readPos -= size;
        let rp = lp.readPos;
        const frac = lp.frac / SCALE;

        let a2 = (frac * frac - 1) / 6;
        let a1 = (frac + 1) * 0.5;
        let am1 = a1 - 1;
        let a0 = 3 * a2;
        a1 -= a0;
        am1 -= a2;
        a0 -= frac;

        let vm1, v0, v1, v2;
        if (rp > 0 && rp < size - 2) {
          vm1 = buf[rp - 1];
          v0 = buf[rp];
          v1 = buf[rp + 1];
          v2 = buf[rp + 2];
        } else {
          if (--rp < 0) rp += size;
          vm1 = buf[rp];
          if (++rp >= size) rp -= size;
          v0 = buf[rp];
          if (++rp >= size) rp -= size;
          v1 = buf[rp];
          if (++rp >= size) rp -= size;
          v2 = buf[rp];
        }
        v0 = (am1 * vm1 + a0 * v0 + a1 * v1 + a2 * v2) * frac + v0;
        lp.frac += lp.fracInc;

        v0 *= this.feedback;
        v0 = (lp.state - v0) * damp + v0;
        lp.state = v0;
        if (n & 1) oR += v0;
        else oL += v0;

        if (--lp.cnt <= 0) this.nextSeg(lp, n);
      }
      out[0] = oL * OUT_GAIN;
      out[1] = oR * OUT_GAIN;
    }
  }

  // ------------------------------------------------------------- DroneVoice
  const NUM_VOICES = 6;
  const NUM_ROOT = 4;
  const SPREAD = [-1, -0.33, 0.33, 1, -0.6, 0.6];
  const PAN = [0.15, 0.7, 0.3, 0.85, 0.25, 0.75];
  const INTERVALS = [0, 7, 12, 5, 3, 4, 10];

  class DroneEngine {
    constructor(sr) {
      this.sr = sr;
      const blockRate = sr / BLOCK;
      this.osc = [];
      this.drift = [];
      this.gL = [];
      this.gR = [];
      for (let i = 0; i < NUM_VOICES; i++) {
        const o = new Osc(sr);
        o.amp = 0.18;
        o.phase = i * 0.17;
        this.osc.push(o);
        const d = new SmoothRandom(blockRate);
        d.setFreq(0.05 + 0.03 * i);
        this.drift.push(d);
        this.gL.push(Math.cos(PAN[i] * HALFPI));
        this.gR.push(Math.sin(PAN[i] * HALFPI));
      }
      this.sub = new Osc(sr);
      this.sub.wave = 3;
      this.fL = new Ladder(sr);
      this.fR = new Ladder(sr);
      for (const f of [this.fL, this.fR]) {
        f.setPassbandGain(0.25);
        f.setInputDrive(1.2);
      }
      this.verb = new ReverbSc(sr);
      this.verb.lpfreq = 9000;
      this.verbOut = new Float64Array(2);

      this.patch = {
        v: [[0.33, 0.35], [0.45, 0.25], [0.35, 0.5], [0.4, 0.7]],
        wave: 0,
        interval: 1,
        sub: 1,
      };
      this.lfoPhase = 0;
      this.lfo2Phase = 0;
      this.lfoL = 0;
      this.rootSm = null;
      this.cutoffSm = null;
      this.ampSm = 1;
      this.counter = 0;
      this.subAmp = 0.3;
      this.dryG = 1;
      this.wetG = 0;
    }

    setPatch(p) {
      this.patch = p;
    }

    control() {
      const p = this.patch;
      const dt = BLOCK / this.sr;
      const rootTarget = Math.round(24 + p.v[0][0] * 36);
      const detuneCt = p.v[0][1] * p.v[0][1] * 30;
      const cutoffBase = expMap(p.v[1][0], 40, 8000);
      const res = p.v[1][1] * 1.5;
      const motionHz = expMap(p.v[2][0], 0.005, 0.5);
      const depth = p.v[2][1];
      const mix = p.v[3][0];
      const feedback = 0.6 + p.v[3][1] * 0.385;

      if (this.rootSm === null) {
        this.rootSm = rootTarget;
        this.cutoffSm = cutoffBase;
      }

      this.lfoPhase += motionHz * dt;
      if (this.lfoPhase >= 1) this.lfoPhase -= 1;
      this.lfo2Phase += motionHz * 0.37 * dt;
      if (this.lfo2Phase >= 1) this.lfo2Phase -= 1;
      const lfoL = Math.sin(TWOPI * this.lfoPhase);
      const lfoR = Math.sin(TWOPI * (this.lfoPhase + 0.25));
      const lfo2 = Math.sin(TWOPI * this.lfo2Phase);
      this.lfoL = lfoL;

      this.rootSm += 0.004 * (rootTarget - this.rootSm);
      this.cutoffSm += 0.05 * (cutoffBase - this.cutoffSm);
      this.ampSm += 0.05 * (1 - depth * 0.3 * (0.5 + 0.5 * lfo2) - this.ampSm);

      const sweep = depth * 2;
      this.fL.setFreq(clamp(this.cutoffSm * Math.pow(2, lfoL * sweep), 20, 16000));
      this.fR.setFreq(clamp(this.cutoffSm * Math.pow(2, lfoR * sweep), 20, 16000));
      this.fL.setRes(res);
      this.fR.setRes(res);
      this.verb.feedback = feedback;

      const driftCt = 2 + depth * 10;
      const interval = INTERVALS[p.interval % INTERVALS.length];
      for (let i = 0; i < NUM_VOICES; i++) {
        const semis = i < NUM_ROOT ? 0 : interval;
        let cents = SPREAD[i] * detuneCt;
        cents += this.drift[i].process() * driftCt;
        this.osc[i].setFreq(mtof(this.rootSm + semis + cents * 0.01));
        this.osc[i].wave = p.wave;
      }
      this.sub.setFreq(mtof(this.rootSm - 12));

      this.subAmp = p.sub ? 0.3 : 0;
      this.dryG = Math.cos(mix * HALFPI);
      this.wetG = Math.sin(mix * HALFPI);
    }

    render(outL, outR, n) {
      const osc = this.osc;
      const gL = this.gL;
      const gR = this.gR;
      const vo = this.verbOut;
      for (let i = 0; i < n; i++) {
        if (this.counter === 0) this.control();
        if (++this.counter >= BLOCK) this.counter = 0;

        let l = 0;
        let r = 0;
        for (let v = 0; v < NUM_VOICES; v++) {
          const s = osc[v].process();
          l += s * gL[v];
          r += s * gR[v];
        }
        const s = this.sub.process() * this.subAmp;
        l += s;
        r += s;

        l = this.fL.process(l) * this.ampSm;
        r = this.fR.process(r) * this.ampSm;

        this.verb.process(l, r, vo);
        outL[i] = softClip(l * this.dryG + vo[0] * this.wetG);
        outR[i] = softClip(r * this.dryG + vo[1] * this.wetG);
      }
    }
  }

  if (typeof registerProcessor === "function") {
    class DroneProcessor extends AudioWorkletProcessor {
      constructor() {
        super();
        this.engine = new DroneEngine(sampleRate);
        this.sinceReport = 0;
        this.port.onmessage = (e) => this.engine.setPatch(e.data);
      }
      process(inputs, outputs) {
        const out = outputs[0];
        const L = out[0];
        const R = out[1] || out[0];
        this.engine.render(L, R, L.length);
        this.sinceReport += L.length;
        if (this.sinceReport >= 1024) {
          this.sinceReport = 0;
          this.port.postMessage(this.engine.lfoL);
        }
        return true;
      }
    }
    registerProcessor("drone-voice", DroneProcessor);
  } else {
    globalThis.DroneEngine = DroneEngine;
  }
})();
