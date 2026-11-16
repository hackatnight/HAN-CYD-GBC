// H@N-CYD-GBC "Send games over USB": talks to the board's transfer mode (see xferCommand in
// src/main.cpp). Works on any pair of Web Streams, so the same code runs in the browser
// (Web Serial) and in the Node test (tools/usbtest).

export const BAUD = 921600;
export const SYSTEM_DIRS = { gb: '/Roms/GB', gbc: '/Roms/GBC', nes: '/Roms/NES' };
export const CHEAT_DIRS = { gb: '/CHEATS/GB', gbc: '/CHEATS/GBC', nes: '/CHEATS/NES' };

let crcTable = null;
export function crc32(bytes) {
  if (!crcTable) {
    crcTable = new Uint32Array(256);
    for (let i = 0; i < 256; i++) {
      let c = i;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      crcTable[i] = c >>> 0;
    }
  }
  let c = 0xFFFFFFFF;
  for (let i = 0; i < bytes.length; i++) c = crcTable[(c ^ bytes[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

export function extOf(name) {
  const i = name.lastIndexOf('.');
  return i < 0 ? '' : name.slice(i + 1).toLowerCase();
}

// Where a file goes. dest: 'auto' or a folder path. cheatSys: 'gb' | 'gbc' | 'nes'.
// Returns the full path, or null for a file type the emulator doesn't use.
export function targetFor(name, dest, cheatSys) {
  const ext = extOf(name);
  const clean = name.replace(/[\\/:*?"<>|]/g, '_');
  if (ext in SYSTEM_DIRS) return (dest === 'auto' ? SYSTEM_DIRS[ext] : dest) + '/' + clean;
  if (ext === 'cht') return CHEAT_DIRS[cheatSys] + '/' + clean;
  return null;
}

// "Tetris.gb" -> "Tetris (2).gb"
export function numbered(path, k) {
  const slash = path.lastIndexOf('/'), dot = path.lastIndexOf('.');
  return dot > slash ? `${path.slice(0, dot)} (${k})${path.slice(dot)}` : `${path} (${k})`;
}

export class HnxLink {
  constructor(readable, writable) {
    this.reader = readable.getReader();
    this.writer = writable.getWriter();
    this.lines = [];
    this.waiters = [];
    this.closed = false;
    this._pump();
  }

  async _pump() {
    const dec = new TextDecoder();
    let buf = '';
    try {
      while (true) {
        const { value, done } = await this.reader.read();
        if (done) break;
        buf += dec.decode(value, { stream: true });
        let i;
        while ((i = buf.indexOf('\n')) >= 0) {
          const line = buf.slice(0, i).replace(/\r$/, '');
          buf = buf.slice(i + 1);
          if (line.startsWith('HNX ')) this._push(line.slice(4));   // everything else is the boot log
        }
        if (buf.length > 8192) buf = '';
      }
    } catch (e) { /* port closed */ }
    this.closed = true;
    this._push(null);
  }

  _push(line) {
    const w = this.waiters.shift();
    if (w) w(line); else if (line !== null) this.lines.push(line);
  }

  next(timeout = 5000) {
    if (this.lines.length) return Promise.resolve(this.lines.shift());
    if (this.closed) return Promise.reject(new Error('The board was disconnected'));
    return new Promise((resolve, reject) => {
      const done = line => {
        clearTimeout(timer);
        if (line === null) reject(new Error('The board was disconnected')); else resolve(line);
      };
      const timer = setTimeout(() => {
        const k = this.waiters.indexOf(done);
        if (k >= 0) this.waiters.splice(k, 1);
        reject(new Error('The board stopped answering'));
      }, timeout);
      this.waiters.push(done);
    });
  }

  send(text) { return this.writer.write(new TextEncoder().encode(text)); }

  async command(line, timeout = 5000) {
    await this.send(`HNX ${line}\n`);
    return this.next(timeout);
  }

  // The board may restart when the port opens and play its boot animation, so keep asking.
  async hello(tries = 15) {
    for (let i = 0; i < tries; i++) {
      this.lines = [];
      await this.send('HNX HELLO\n');
      try {
        const l = await this.next(1000);
        if (l.startsWith('OK ')) {
          const [version, build, freeMB, board] = l.slice(3).split(' ');
          return { version, build, freeMB: Number(freeMB), board: board || '' };
        }
      } catch (e) { /* not yet */ }
    }
    throw new Error('No answer from the board. Is it showing the game list or the USB transfer screen?');
  }

  async listFolders(dir) {
    await this.send(`HNX LIST ${dir}\n`);
    const out = [];
    while (true) {
      const l = await this.next(10000);
      if (l === 'END') return out;
      if (l.startsWith('D ')) out.push(l.slice(2));
    }
  }

  async stat(path) {
    const l = await this.command(`STAT ${path}`);
    return l.startsWith('YES ') ? Number(l.slice(4)) : null;
  }

  // data: Uint8Array. onProgress(sentBytes, total).
  async put(path, data, onProgress) {
    await this.send(`HNX PUT ${path}\t${data.length}\n`);
    let l = await this.next(10000);
    if (!l.startsWith('GO ')) throw new Error(l.startsWith('ERR ') ? l.slice(4) : l);
    const chunk = Number(l.slice(3));
    for (let off = 0; off < data.length;) {
      const n = Math.min(chunk, data.length - off);
      const part = data.subarray(off, off + n);
      const pkt = new Uint8Array(n + 4);
      pkt.set(part);
      new DataView(pkt.buffer).setUint32(n, crc32(part), true);
      for (let tries = 0; ; tries++) {
        await this.writer.write(pkt);
        l = await this.next(10000);
        if (l === 'A') break;
        if (l === 'N' && tries < 5) continue;   // damaged on the way: send it again
        throw new Error(l.startsWith('ERR ') ? l.slice(4) : 'Transfer failed');
      }
      off += n;
      if (onProgress) onProgress(off, data.length);
    }
    l = await this.next(15000);
    if (l !== 'DONE') throw new Error(l.startsWith('ERR ') ? l.slice(4) : l);
  }

  async bye() {
    try { await this.command('BYE', 2000); } catch (e) { /* already gone */ }
  }

  async release() {
    try { await this.reader.cancel(); } catch (e) {}
    try { this.reader.releaseLock(); } catch (e) {}
    try { this.writer.releaseLock(); } catch (e) {}
  }
}

// Folders for the "Save to" list: everything under /Roms (two levels deep), plus game
// folders kept at the top of the card (the board shows those too).
const SKIP_TOP = new Set(['roms', 'gbc', 'cheats', 'saves', 'system volume information']);
export async function romFolders(link) {
  const out = [];
  const walk = async (dir, depth, skipTop) => {
    let subs = [];
    try { subs = await link.listFolders(dir); } catch (e) { return; }
    for (const s of subs.sort((a, b) => a.localeCompare(b))) {
      if (skipTop && (SKIP_TOP.has(s.toLowerCase()) || s.startsWith('.'))) continue;
      const p = `${dir === '/' ? '' : dir}/${s}`;
      out.push(p);
      if (depth < 2) await walk(p, depth + 1, false);
    }
  };
  await walk('/Roms', 0, false);
  await walk('/', 1, true);
  return out;
}
