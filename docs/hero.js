// The board's own logo scene, drawn in the page header.
//
// Same geometry as src/wire/wire_logo.h, same projection as wfLogo() and the same star
// field as logoScene(), so the banner is the animation the board plays on its boot
// screen and its screensaver rather than a recording of an older one. It cycles the
// four themes from THEMES[] in main.cpp; the typed address is the boot screen's own and
// is deliberately absent here.
export const WIRE_LEN = [12,8,76,10];
export const WIRE_PTS = [[-342,-128],[-289,-128],[-289,-49],[-210,-49],[-210,-128],[-157,-128],[-157,79],[-210,79],[-210,-9],[-289,-9],[-289,79],[-342,79],[-26,4],[-20,29],[1,41],[22,29],[28,5],[22,-20],[1,-32],[-20,-20],[31,46],[14,63],[-10,66],[-32,58],[-48,40],[-56,17],[-56,-7],[-49,-29],[-34,-48],[-12,-56],[12,-55],[30,-39],[35,-55],[59,-55],[60,-31],[60,-7],[60,17],[60,41],[81,29],[94,9],[96,-15],[92,-39],[80,-60],[62,-77],[41,-88],[17,-93],[-7,-94],[-31,-89],[-53,-79],[-71,-63],[-85,-43],[-94,-20],[-97,4],[-93,28],[-85,51],[-71,70],[-53,86],[-31,97],[-7,101],[17,101],[40,94],[61,82],[77,93],[69,109],[48,119],[24,126],[0,128],[-24,125],[-47,118],[-69,107],[-87,91],[-103,73],[-114,51],[-120,28],[-122,3],[-120,-21],[-113,-44],[-102,-66],[-86,-84],[-68,-100],[-46,-111],[-23,-118],[1,-121],[26,-118],[49,-112],[71,-102],[90,-87],[105,-68],[116,-47],[122,-23],[120,1],[115,24],[101,45],[81,57],[58,64],[34,67],[157,-128],[216,-128],[291,14],[291,-128],[342,-128],[342,79],[282,79],[207,-63],[207,79],[157,79]];

// bg, accent, bronze, dim, text - the fields logoScene() actually paints with
export const THEMES = [
  { name: 'H@N Night', bg: 0x0E1622, accent: 0x86A8C4, bronze: 0x9A8462, dim: 0x8494A5, text: 0xE4ECF2 },
  { name: 'Neon',      bg: 0x0A0618, accent: 0xFF3CC8, bronze: 0x2CE6FF, dim: 0x9A88B8, text: 0xF4ECFF },
  { name: 'DMG Green', bg: 0x0F380F, accent: 0x9BBC0F, bronze: 0xB8C858, dim: 0x7A9A40, text: 0xC8E08C },
  { name: 'Amber CRT', bg: 0x140A02, accent: 0xFFA830, bronze: 0xC07010, dim: 0xA88050, text: 0xFFD898 },
];
const ASPECT = 684 / 256;
const STARS = 64, HOLD = 5200, FADE = 1400;   // ms per theme, and the crossfade between

const mix = (a, b, t) => {
  const r = ((a >> 16) & 255) + (((b >> 16) & 255) - ((a >> 16) & 255)) * t;
  const g = ((a >> 8) & 255) + (((b >> 8) & 255) - ((a >> 8) & 255)) * t;
  const c = (a & 255) + ((b & 255) - (a & 255)) * t;
  return `rgb(${r | 0},${g | 0},${c | 0})`;
};
const mixN = (a, b, t) => {
  const r = ((a >> 16) & 255) + (((b >> 16) & 255) - ((a >> 16) & 255)) * t;
  const g = ((a >> 8) & 255) + (((b >> 8) & 255) - ((a >> 8) & 255)) * t;
  const c = (a & 255) + ((b & 255) - (a & 255)) * t;
  return ((r | 0) << 16) | ((g | 0) << 8) | (c | 0);
};

export function startHero(canvas) {
  const g = canvas.getContext('2d');
  const W = canvas.width, H = canvas.height;
  let seed = 12345;
  const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return ((seed >> 8) & 0xFFFF) / 65535; };
  const st = [];
  for (let i = 0; i < STARS; i++) st.push({ x: rnd() * 2 - 1, y: rnd() * 2 - 1, z: rnd() });
  const t0 = performance.now();

  function frame(now) {
    const t = (now - t0) / 1000;
    // where we are in the theme cycle
    const span = HOLD + FADE, k = ((now - t0) % (span * THEMES.length)) / span;
    const i = Math.floor(k), f = Math.min(1, Math.max(0, (k - i) * span - HOLD) / FADE);
    const A = THEMES[i % THEMES.length], B = THEMES[(i + 1) % THEMES.length];
    const bg = mixN(A.bg, B.bg, f), accent = mixN(A.accent, B.accent, f);
    const bronze = mixN(A.bronze, B.bronze, f), dim = mixN(A.dim, B.dim, f);
    const text = mixN(A.text, B.text, f);

    g.fillStyle = mix(bg, bg, 0);
    g.fillRect(0, 0, W, H);

    const cx = W / 2, cy = H * 0.55;
    for (let s = 0; s < STARS; s++) {
      const z = ((st[s].z - t * 0.25 + 8) % 1) + 0.05;
      const x = cx + st[s].x * W * 0.2 / z, y = H / 2 + st[s].y * H * 0.25 / z;
      const lvl = z > 0.75 ? 0 : z > 0.5 ? 1 : z > 0.25 ? 2 : 3;
      g.fillStyle = mix(bg, text, (70 + lvl * 60) / 255);
      g.fillRect(x | 0, y | 0, 1, 1);
      if (lvl === 3) g.fillRect((x | 0) + 1, y | 0, 1, 1);
    }

    // The board frames this for a portrait screen, where the logo is width-limited and
    // fills it. A wide banner is height-limited instead, so it gets its own framing -
    // the only thing here that is not what the board draws.
    const hPx = Math.min(H * 0.60, W * 0.88 / ASPECT);
    const ay = Math.sin(t * 1.2) * 0.35, ax = Math.sin(t * 0.9) * 0.2, dist = 6;
    const fov = hPx * 6, depth = 0.2;
    const ca = Math.cos(ay), sa = Math.sin(ay), cb = Math.cos(ax), sb = Math.sin(ax);
    let p = 0;
    g.lineWidth = 1;
    for (const n of WIRE_LEN) {
      const fx = [], fy = [], bx = [], by = [];
      for (let q = 0; q < n; q++) {
        const px = WIRE_PTS[p + q][0] / 256, py = WIRE_PTS[p + q][1] / 256;
        for (let side = 0; side < 2; side++) {
          const z = side ? depth : -depth;
          const x2 = px * ca - z * sa, z2 = px * sa + z * ca;
          const y2 = py * cb - z2 * sb;
          let z3 = py * sb + z2 * cb + dist;
          if (z3 < 0.2) z3 = 0.2;
          const sx = cx + x2 * fov / z3, sy = cy + y2 * fov / z3;
          if (side) { bx[q] = sx; by[q] = sy; } else { fx[q] = sx; fy[q] = sy; }
        }
      }
      const loop = (ax2, ay2, col) => {
        g.strokeStyle = col; g.beginPath();
        for (let q = 0; q < n; q++) {
          const r = (q + 1) % n;
          g.moveTo(ax2[q] + 0.5, ay2[q] + 0.5); g.lineTo(ax2[r] + 0.5, ay2[r] + 0.5);
        }
        g.stroke();
      };
      loop(bx, by, mix(bronze, bronze, 0));
      g.strokeStyle = mix(dim, dim, 0); g.beginPath();
      for (let q = 0; q < n; q += n > 20 ? 4 : 1) {
        g.moveTo(fx[q] + 0.5, fy[q] + 0.5); g.lineTo(bx[q] + 0.5, by[q] + 0.5);
      }
      g.stroke();
      loop(fx, fy, mix(accent, accent, 0));
      p += n;
    }
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}
