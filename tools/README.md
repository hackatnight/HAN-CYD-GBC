# Tools (not part of the firmware)

- `logo/` – old splash/logo generators (no longer used by the firmware).
- `wire_logo.py` – makes `src/wire/wire_logo.h`, the wireframe H@N outline.
- `hosttest/` – Game Boy core tests on a PC: `harness.c` (reference frames in `ref/`),
  `strt.c` (save-state round trip), plus ring/paging/streaming tests. Build against `src/gnuboy`
  with `-Istub`.
- `nestest/` – NES core tests on a PC: `harness.c` (runs a ROM, dumps a frame / blargg status),
  `nst.c` (save-state round trip). Build against `src/nofrendo`. Test ROMs: nes-test-roms
  (retro-go ships them in `retro-core/components/nofrendo/docs/`).
- `demos/` – the H@N demo ROMs (Game Boy/GBC and NES), built from scratch: `python3 build.py OUTDIR`.
  `gbrun.c` / `nesrun.c` run a ROM in the firmware's cores with scripted buttons and save frames
  (build lines at the top of each file).
