// End-to-end test: docs/send.js (the web page's code) <-> the firmware's transfer code.
//   ./build.sh && node test.mjs            (CORRUPT=1 node test.mjs damages some chunks)
import { spawn } from 'node:child_process';
import { Readable, Writable } from 'node:stream';
import fs from 'node:fs';
import crypto from 'node:crypto';
import { HnxLink, targetFor, numbered, romFolders } from '../../docs/send.js';

const card = new URL('./card', import.meta.url).pathname;
fs.rmSync(card, { recursive: true, force: true });
fs.mkdirSync(card + '/Roms/GBC/RPG', { recursive: true });
fs.writeFileSync(card + '/Roms/GBC/Keep me.gbc', 'existing game');

const board = spawn('./harness', [], { cwd: new URL('.', import.meta.url).pathname, stdio: ['pipe', 'pipe', 'inherit'] });
const link = new HnxLink(Readable.toWeb(board.stdout), Writable.toWeb(board.stdin));
const ok = (c, m) => { if (!c) { console.error('FAIL:', m); process.exit(1); } console.log('ok  ', m); };

const info = await link.hello();
ok(info.version === 'test', 'hello: ' + JSON.stringify(info));
ok(JSON.stringify(await romFolders(link)) === JSON.stringify(['/Roms/GBC', '/Roms/GBC/RPG']), 'folder list');

ok(targetFor('Tetris.GB', 'auto', 'gb') === '/Roms/GB/Tetris.GB', 'sort .gb');
ok(targetFor('Zelda.gbc', 'auto', 'gb') === '/Roms/GBC/Zelda.gbc', 'sort .gbc');
ok(targetFor('Mario.nes', '/Roms/NES/Action', 'gb') === '/Roms/NES/Action/Mario.nes', 'chosen folder');
ok(targetFor('Mario.cht', 'auto', 'nes') === '/CHEATS/NES/Mario.cht', 'cheat file');
ok(targetFor('readme.txt', 'auto', 'gb') === null, 'other files skipped');
ok(numbered('/Roms/GB/Tetris.gb', 2) === '/Roms/GB/Tetris (2).gb', 'keep-both name');

for (const size of [0, 1, 4095, 4096, 4097, 300001, 2 * 1024 * 1024]) {
  const data = new Uint8Array(crypto.randomBytes(size));
  const path = `/Roms/GB/test ${size}.gb`;
  const t0 = Date.now();
  await link.put(path, data);
  const back = fs.readFileSync(card + path);
  ok(Buffer.compare(back, Buffer.from(data)) === 0 && !fs.existsSync(card + path + '.part'),
     `put ${size} bytes (${Date.now() - t0} ms on the PC)`);
}
ok(await link.stat('/Roms/GB/test 1.gb') === 1, 'stat existing');
ok(await link.stat('/Roms/GB/nope.gb') === null, 'stat missing');
ok(fs.readFileSync(card + '/Roms/GBC/Keep me.gbc', 'utf8') === 'existing game', 'other games untouched');
await link.put('/Roms/GBC/Keep me.gbc', new TextEncoder().encode('replaced'));
ok(fs.readFileSync(card + '/Roms/GBC/Keep me.gbc', 'utf8') === 'replaced', 'replace when asked');
let bad = false; try { await link.put('/Roms/../x.gb', new Uint8Array(3)); } catch (e) { bad = /bad path/.test(e.message); }
ok(bad, 'refuses paths with ..');
await link.bye();
await link.release();
board.on('exit', c => { ok(c === 0, 'board left transfer mode'); });
