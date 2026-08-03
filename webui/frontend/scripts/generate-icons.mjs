/**
 * Generate PWA app icons (pure Node.js, no dependencies).
 *
 * Draws a "绯英" blossom icon:
 *   - full-bleed deep-purple radial background (maskable-safe)
 *   - six pink petal ellipses arranged radially
 *   - warm golden center
 * Anti-aliased via 4x4 supersampling. Writes PNGs (RGBA, 8-bit) directly.
 *
 * Usage:  node scripts/generate-icons.mjs
 * Output: public/icon.svg, public/icons/icon-192.png,
 *         public/icons/icon-512.png, public/icons/apple-touch-icon.png
 */
import { deflateSync } from 'node:zlib';
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const OUT_DIR = join(ROOT, 'public', 'icons');

/* ─────────────────────────── PNG encoder ─────────────────────────── */

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) {
      c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    }
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) {
    c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  }
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length, 0);
  const typeBuf = Buffer.from(type, 'ascii');
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([typeBuf, data])), 0);
  return Buffer.concat([len, typeBuf, data, crc]);
}

function encodePNG(width, height, rgba) {
  const sig = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; // bit depth
  ihdr[9] = 6; // color type: RGBA
  const stride = width * 4;
  const raw = Buffer.alloc((stride + 1) * height);
  for (let y = 0; y < height; y++) {
    raw[y * (stride + 1)] = 0; // filter: none
    rgba.copy(raw, y * (stride + 1) + 1, y * stride, (y + 1) * stride);
  }
  const idat = deflateSync(raw, { level: 9 });
  return Buffer.concat([
    sig,
    chunk('IHDR', ihdr),
    chunk('IDAT', idat),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

/* ─────────────────────── drawing primitives ─────────────────────── */

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const lerp = (a, b, t) => a + (b - a) * t;

// Smooth coverage from an SDF value: 1 inside, 0 outside, ~AA at boundary.
function coverage(sdf, edge) {
  return clamp((1 - sdf) / edge + 0.5, 0, 1);
}

// Squared-normalized ellipse SDF (< 1 inside). rot in radians.
function ellipseSDF(x, y, cx, cy, rx, ry, rot) {
  const dx = x - cx;
  const dy = y - cy;
  const cos = Math.cos(rot);
  const sin = Math.sin(rot);
  const xr = dx * cos + dy * sin;
  const yr = -dx * sin + dy * cos;
  return Math.sqrt((xr * xr) / (rx * rx) + (yr * yr) / (ry * ry));
}

/* ─────────────────────────── palette ─────────────────────────── */

const BG_INNER = [42, 31, 77];   // #2a1f4d
const BG_OUTER = [15, 13, 30];   // #0f0d1e
const PETAL_LIGHT = [255, 197, 226]; // #ffc5e2
const PETAL_DEEP = [236, 72, 153];   // #ec4899
const CORE_LIGHT = [254, 243, 199];  // #fef3c7
const CORE_DEEP = [245, 158, 11];    // #f59e0b

const PETALS = 6;
const PETAL_DIST = 0.21;   // distance of petal centers from middle
const PETAL_RX = 0.185;    // radial radius
const PETAL_RY = 0.13;     // tangential radius
const CORE_RADIUS = 0.135;
const EDGE = 0.035;        // AA edge width (SDF units)

function shade(light, deep, t) {
  const u = clamp(t, 0, 1);
  return [
    lerp(light[0], deep[0], u),
    lerp(light[1], deep[1], u),
    lerp(light[2], deep[2], u),
  ];
}

/* ─────────────────────────── renderer ─────────────────────────── */

// Returns [r, g, b, a] in [0,1] for a unit-space point.
function colorAt(u, v) {
  const dx = u - 0.5;
  const dy = v - 0.5;
  const dist = Math.hypot(dx, dy);
  const bg = shade(BG_INNER, BG_OUTER, Math.min(dist * 1.6, 1));

  let r = bg[0], g = bg[1], b = bg[2];

  // Petals
  for (let i = 0; i < PETALS; i++) {
    const ang = (i / PETALS) * Math.PI * 2 + Math.PI / PETALS;
    const cx = 0.5 + Math.cos(ang) * PETAL_DIST;
    const cy = 0.5 + Math.sin(ang) * PETAL_DIST;
    const sdf = ellipseSDF(u, v, cx, cy, PETAL_RX, PETAL_RY, ang + Math.PI / 2);
    const cov = coverage(sdf, EDGE);
    if (cov <= 0) continue;
    const petal = shade(PETAL_LIGHT, PETAL_DEEP, Math.min(sdf, 1));
    // alpha-composite over current color
    r = lerp(r, petal[0], cov);
    g = lerp(g, petal[1], cov);
    b = lerp(b, petal[2], cov);
  }

  // Golden core
  const coreSdf = Math.hypot(dx, dy) / CORE_RADIUS;
  const coreCov = coverage(coreSdf, EDGE);
  if (coreCov > 0) {
    const core = shade(CORE_LIGHT, CORE_DEEP, Math.min(coreSdf, 1));
    r = lerp(r, core[0], coreCov);
    g = lerp(g, core[1], coreCov);
    b = lerp(b, core[2], coreCov);
  }

  return [r, g, b, 1];
}

function render(size) {
  const SS = 4; // supersampling factor
  const buf = Buffer.alloc(size * size * 4);
  for (let py = 0; py < size; py++) {
    for (let px = 0; px < size; px++) {
      let r = 0, g = 0, b = 0;
      for (let sy = 0; sy < SS; sy++) {
        for (let sx = 0; sx < SS; sx++) {
          const u = (px + (sx + 0.5) / SS) / size;
          const v = (py + (sy + 0.5) / SS) / size;
          const [cr, cg, cb] = colorAt(u, v);
          r += cr; g += cg; b += cb;
        }
      }
      const n = SS * SS;
      const off = (py * size + px) * 4;
      buf[off] = Math.round((r / n) * 255);
      buf[off + 1] = Math.round((g / n) * 255);
      buf[off + 2] = Math.round((b / n) * 255);
      buf[off + 3] = 255;
    }
  }
  return encodePNG(size, size, buf);
}

/* ─────────────────────────── SVG fallback ─────────────────────────── */

const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">
  <defs>
    <radialGradient id="bg" cx="50%" cy="50%" r="70%">
      <stop offset="0%" stop-color="#2a1f4d"/>
      <stop offset="100%" stop-color="#0f0d1e"/>
    </radialGradient>
    <radialGradient id="petal" cx="35%" cy="35%" r="90%">
      <stop offset="0%" stop-color="#ffc5e2"/>
      <stop offset="100%" stop-color="#ec4899"/>
    </radialGradient>
    <radialGradient id="core" cx="40%" cy="40%" r="80%">
      <stop offset="0%" stop-color="#fef3c7"/>
      <stop offset="100%" stop-color="#f59e0b"/>
    </radialGradient>
  </defs>
  <rect width="512" height="512" fill="url(#bg)"/>
  <g>
    ${Array.from({ length: PETALS }, (_, i) => {
      const ang = (i / PETALS) * Math.PI * 2 + Math.PI / PETALS;
      const cx = 256 + Math.cos(ang) * 107.5;
      const cy = 256 + Math.sin(ang) * 107.5;
      return `<ellipse cx="${cx.toFixed(1)}" cy="${cy.toFixed(1)}" rx="${(PETAL_RX * 512).toFixed(1)}" ry="${(PETAL_RY * 512).toFixed(1)}" transform="rotate(${((ang + Math.PI / 2) * 180 / Math.PI).toFixed(1)} ${cx.toFixed(1)} ${cy.toFixed(1)})" fill="url(#petal)"/>`;
    }).join('\n    ')}
  </g>
  <circle cx="256" cy="256" r="${(CORE_RADIUS * 512).toFixed(1)}" fill="url(#core)"/>
</svg>
`;

/* ───────────────────────────── main ───────────────────────────── */

mkdirSync(OUT_DIR, { recursive: true });
writeFileSync(join(ROOT, 'public', 'icon.svg'), svg);
console.log('✔ public/icon.svg');

const sizes = [
  [192, 'icon-192.png'],
  [512, 'icon-512.png'],
  [180, 'apple-touch-icon.png'],
];
for (const [size, name] of sizes) {
  writeFileSync(join(OUT_DIR, name), render(size));
  console.log(`✔ public/icons/${name} (${size}x${size})`);
}
console.log('done.');
