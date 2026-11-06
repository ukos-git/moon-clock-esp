/* moonlab.js — the Moon Phase Clock simulator for the book's interactive
 * chapter (docs/science/interactive).
 *
 * This is a line-faithful JavaScript port of the portable C core
 * (components/moon/core/moon_bake.c :: bake_px + moon_relief_row /
 * moon_relief_build, moon_render.c :: g565_build), with the geometry
 * coming from the validated Python prototype (moon.astro) precomputed
 * into a table by docs/tools/gen_moonjs.py. It runs in the browser
 * (window.MOONLAB) and in Node (module.exports) so the book's A/B
 * figures can be generated from the same code that the reader drives.
 *
 * C -> JS correspondence:
 *   bake_px            -> MoonLab.prototype._bakePx (inlined in bake())
 *   moon_relief_build  -> MoonLab.prototype._buildRelief (gradient cache)
 *   moon_backlit_night -> MoonLab.prototype._backlit
 *   earthshine         -> MoonLab.prototype._earthshine
 *   g565_build         -> MoonLab.prototype._g565
 *   (geometry)         -> MoonLab.prototype.geometryAt (table lerp)
 *
 * Constants (DEM_MIN/MAX, ES_*, TEX_LON_OFFSET, RADIUS_M) are the C
 * device constants from moon_consts.h, carried in moonlab_data.js.
 */
(function (root, factory) {
  const mod = factory();
  if (typeof module !== "undefined" && module.exports) module.exports = mod;
  root.MOONLAB = mod;
})(typeof self !== "undefined" ? self : globalThis, function () {
  "use strict";

  const RAD2DEG = 57.29577951308232;

  function b64U8(s) {
    const n = atob(s);
    const a = new Uint8Array(n.length);
    for (let i = 0; i < n.length; i++) a[i] = n.charCodeAt(i);
    return a;
  }

  class MoonLab {
    constructor(data) {
      this.D = data;
      this.alb = b64U8(data.alb);
      this.dem = b64U8(data.dem);
      this.lut = b64U8(data.lut);
      this.geo = new Float32Array(b64U8(data.geo).buffer);
      this.pack = this._buildRelief();
      this._warm565 = this._g565(true);
      this._neutral565 = this._g565(false);
    }

    /* moon_relief_build: the DEM's 4-neighbor central-difference slopes,
     * quantized to int8 units of 0.003 rad, packed (glon low byte /
     * glat high byte) — one Int16Array of DEM_W x DEM_H cells. */
    _buildRelief() {
      const D = this.D;
      const W = D.DEM_W, H = D.DEM_H;
      const mps = (D.DEM_MAX_M - D.DEM_MIN_M) / D.DEM_STEPS;
      const dlon = (2 * Math.PI) / W;
      const dlat = Math.PI / H;
      const twoR = 2 * D.RADIUS_M;
      const dem = this.dem;
      const pack = new Int16Array(W * H);
      const GINV = 0.003;
      const slopeQ = (s) => {
        let q = Math.round(s / GINV);
        if (q > 127) q = 127;
        else if (q < -127) q = -127;
        return q | 0;
      };
      for (let r = 0; r < H; r++) {
        const rn = (r > 0 ? r - 1 : 0) * W;
        const rm = r * W;
        const rs = (r + 1 < H ? r + 1 : H - 1) * W;
        let rho = Math.sin((r + 0.5) * (Math.PI / H));
        if (rho < 0.05) rho = 0.05;
        const invGlon = 1 / (twoR * dlon * rho);
        const invGlat = 1 / (twoR * dlat);
        const cell = (c, cw, ce) => {
          const dhlon = (dem[rm + ce] - dem[rm + cw]) * mps;
          const dhlat = (dem[rn + c] - dem[rs + c]) * mps;
          const qn = slopeQ(dhlon * invGlon);
          const qt = slopeQ(dhlat * invGlat);
          pack[rm + c] = (qn & 0xff) | ((qt & 0xff) << 8);
        };
        cell(0, W - 1, 1);
        cell(W - 1, W - 2, 0);
        for (let c = 1; c <= W - 2; c++) cell(c, c - 1, c + 1);
      }
      return pack;
    }

    /* g565_build (moon_render.c): the gray -> RGB565 LUT, warm (the
     * display default, R x1.038 / B x0.933) or neutral. Returns a
     * Uint8ClampedArray of 256 RGB triples (8-bit) for canvas painting. */
    _g565(warm) {
      const out = new Uint8ClampedArray(256 * 3);
      for (let g = 0; g < 256; g++) {
        let r5 = Math.floor((g * 31 + 127) / 255);
        const g6 = Math.floor((g * 63 + 127) / 255);
        let b5 = r5;
        if (warm) {
          r5 = Math.floor((g * 32 + 127) / 255);
          if (r5 > 31) r5 = 31;
          b5 = Math.floor((g * 29 + 127) / 255);
          if (b5 > 31) b5 = 31;
        }
        out[g * 3] = Math.round((r5 * 255) / 31);
        out[g * 3 + 1] = Math.round((g6 * 255) / 63);
        out[g * 3 + 2] = Math.round((b5 * 255) / 31);
      }
      return out;
    }

    /* earthshine (moon_bake.c): A*x + B*x^2 + C*x^3, x = max(0, -d). */
    _earthshine(d) {
      const x = -d > 0 ? -d : 0;
      const D = this.D;
      return D.ES_A * x + D.ES_B * x * x + D.ES_C * x * x * x;
    }

    /* moon_backlit_night: A * clip(-lns, 0, 1) * K, 8-bit. */
    _backlit(A, lns, K) {
      let l = -lns;
      if (l < 0) l = 0;
      else if (l > 1) l = 1;
      const out = A * l * K;
      return out < 0 ? 0 : out > 255 ? 255 : Math.round(out);
    }

    /* atan2(y, x) in degrees, [0, 360) — the C atan2_deg convention
     * (the device uses a 257-entry table; JS's exact atan2 is the same
     * function to < 0.001 deg). */
    _atan2deg(x, y) {
      let a = Math.atan2(y, x) * RAD2DEG;
      if (a < 0) a += 360;
      return a;
    }

    /* Geometry at a UTC time (ms since epoch): linear interpolation in
     * the precomputed 2-hourly table (gen_moonjs.py), unit-vector
     * renormalized. The table is the validated geometry (moon.astro,
     * PyERFA ephemeris + calibrated IAU_Moon body frame). */
    geometryAt(ms) {
      const D = this.D;
      const jd = 2440587.5 + ms / 86400000.0;
      const stepDays = D.GEO_STEP_H / 24.0;
      let f = (jd - D.GEO_START_JD) / stepDays;
      if (f < 0) f = 0;
      else if (f > D.GEO_N - 2) f = D.GEO_N - 2;
      const i0 = Math.floor(f);
      const t = f - i0;
      const a = i0 * 14, b = (i0 + 1) * 14;
      const g = this.geo;
      const lerp3 = (o) => {
        const v = [
          g[a + o] + (g[b + o] - g[a + o]) * t,
          g[a + o + 1] + (g[b + o + 1] - g[a + o + 1]) * t,
          g[a + o + 2] + (g[b + o + 2] - g[a + o + 2]) * t,
        ];
        const n = Math.hypot(v[0], v[1], v[2]);
        return [v[0] / n, v[1] / n, v[2] / n];
      };
      return {
        S: lerp3(0),
        E: lerp3(3),
        right: lerp3(6),
        up: lerp3(9),
        phase: g[a + 12] + (g[b + 12] - g[a + 12]) * t,
        illum: g[a + 13] + (g[b + 13] - g[a + 13]) * t,
      };
    }

    /* Phase label. The stored "phase" is the Sun-Moon-Earth angle i:
     * i = 0 -> fully lit face toward Earth (FULL), i = 180 -> new moon.
     * Waxing = i shrinking toward 0 (toward full); the sign of its hourly
     * derivative decides waxing vs waning between the quarters. */
    phaseLabel(ms) {
      const g = this.geometryAt(ms);
      const gp = this.geometryAt(ms - 3600e3);
      const dph = g.phase - gp.phase; // <0: phase angle shrinking -> waxing
      const il = g.illum;
      let name;
      if (il > 0.975) name = "full moon";
      else if (il < 0.025) name = "new moon";
      else {
        const waxing = dph < 0;
        const gibbous = il > 0.5;
        name = gibbous
          ? (waxing ? "waxing gibbous" : "waning gibbous")
          : (waxing ? "waxing crescent" : "waning crescent");
      }
      return { name, illum: il, phase: g.phase };
    }

    /* bake: one 8-bit grayscale frame (size x size, row-major) — the
     * port of moon_bake. params: { K, soft, relief, backlit }.
     * backlit = true is the DEVICE path (dark_zero = 1: the computed
     * backlit dark side, optional soft terminator); false is the
     * VALIDATED gate model (dark_zero = 0: the earthshine dark side). */
    bake(ms, size, params) {
      const D = this.D;
      const g = this.geometryAt(ms);
      const S = g.S, E = g.E, R = g.right, U = g.up;
      const alb = this.alb, lut = this.lut, pack = this.pack;
      const albW = D.ALB_W, albH = D.ALB_H, demW = D.DEM_W, demH = D.DEM_H;
      const perDeg = albW / 360.0;
      const K = params.K, soft = params.soft || 0;
      const doRelief = params.relief !== false;
      const backlit = !!params.backlit;
      const out = new Uint8ClampedArray(size * size);
      for (let y = 0; y < size; y++) {
        const v = (size / 2 - y) / (size / 2); // radius = size/2, centered
        const rowOff = y * size;
        for (let x = 0; x < size; x++) {
          const u = (x - size / 2) / (size / 2);
          const r2 = u * u + v * v;
          if (r2 > 1) continue; // 0 outside the disk
          const w = Math.sqrt(1 - r2);
          const rx = u * R[0] + v * U[0] + w * E[0];
          const ry = u * R[1] + v * U[1] + w * E[1];
          const rz = u * R[2] + v * U[2] + w * E[2];
          const d = rx * S[0] + ry * S[1] + rz * S[2];

          const theta = this._atan2deg(rx, ry);
          const rowFrac = (90 - Math.asin(rz) * RAD2DEG) / 180;

          /* Lighting: full-disk displaced-normal Lambert. */
          let lns;
          if (doRelief) {
            let dcf = (theta + D.TEX_LON_OFFSET) * (demW / 360);
            if (dcf >= demW) dcf -= demW;
            let dc = Math.round(dcf);
            if (dc >= demW) dc = demW - 1;
            let dr = Math.round(rowFrac * demH);
            if (dr >= demH) dr = demH - 1;
            const gp = pack[dr * demW + dc];
            const glon = (((gp & 0xff) | ((gp & 0x80) ? -256 : 0)) * 0.003);
            const glat = ((((gp >>> 8) & 0xff) | ((gp & 0x8000) ? -256 : 0)) * 0.003);
            let rho = Math.sqrt(rx * rx + ry * ry);
            if (rho < 1e-4) rho = 1e-4;
            const inv = 1 / rho;
            const eastS = (-ry * S[0] + rx * S[1]) * inv;
            const northS = (-rz * rx * S[0] - rz * ry * S[1] + rho * S[2]) * inv;
            lns = (d - (glon * eastS + glat * northS)) /
                  Math.sqrt(1 + glon * glon + glat * glat);
          } else {
            lns = d;
          }
          const L = lns > 1 ? 1 : lns < 0 ? 0 : lns;

          /* Albedo, nearest neighbor (the C bakes the same way). */
          let colf = (theta + D.TEX_LON_OFFSET) * perDeg;
          if (colf >= albW) colf -= albW;
          let col = Math.round(colf);
          if (col >= albW) col = albW - 1;
          let r = Math.round(rowFrac * albH);
          if (r < 0) r = 0;
          else if (r >= albH) r = albH - 1;
          const A = alb[r * albW + col];
          const es = this._earthshine(d);

          if (backlit && soft > 0) {
            /* Soft terminator (MOON_TERM_SOFT smoothstep, display only). */
            let idx = Math.round(A * L);
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;
            const litv = lut[idx] + es;
            const darkv = this._backlit(A, lns, K);
            let t = lns <= -soft ? 0 : lns >= soft ? 1 : (lns + soft) / (2 * soft);
            t = t * t * (3 - 2 * t);
            const o = darkv + (litv - darkv) * t;
            out[rowOff + x] = o < 0 ? 0 : o > 255 ? 255 : Math.round(o);
          } else if (backlit) {
            const isNight = d < 0 || lns <= 0; // dk_sup = 0
            if (isNight) {
              out[rowOff + x] = this._backlit(A, lns, K);
            } else {
              let idx = Math.round(A * L);
              if (idx < 0) idx = 0;
              else if (idx > 255) idx = 255;
              const o = lut[idx] + es;
              out[rowOff + x] = o < 0 ? 0 : o > 255 ? 255 : Math.round(o);
            }
          } else {
            /* dark_zero = 0: the validated earthshine dark side. */
            if (lns <= 0) {
              const o = lut[0] + es;
              out[rowOff + x] = o < 0 ? 0 : o > 255 ? 255 : Math.round(o);
            } else {
              let idx = Math.round(A * L);
              if (idx < 0) idx = 0;
              else if (idx > 255) idx = 255;
              const o = lut[idx] + es;
              out[rowOff + x] = o < 0 ? 0 : o > 255 ? 255 : Math.round(o);
            }
          }
        }
      }
      return out;
    }

    /* Paint a grayscale frame into a canvas via the gray->565 LUT
     * (browser only). warm = the display tint (default) or neutral. */
    draw(canvas, gray, size, warm) {
      const lut = warm === false ? this._neutral565 : this._warm565;
      const ctx = canvas.getContext("2d");
      const img = ctx.createImageData(size, size);
      const px = img.data;
      for (let i = 0; i < size * size; i++) {
        const g = gray[i];
        const o = i * 4, l = g * 3;
        px[o] = lut[l];
        px[o + 1] = lut[l + 1];
        px[o + 2] = lut[l + 2];
        px[o + 3] = 255;
      }
      ctx.putImageData(img, 0, 0);
    }
  }

  return { MoonLab, b64U8 };
});
