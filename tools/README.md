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
- `build_all.sh` – builds every firmware image the flasher page offers and merges each into one
  `.bin`. Needs both ESP32 cores (Bluepad32 for the Bluetooth builds, plain esp32 for touch-only).
- `release_check.py` – run before pushing a release: fails if the page header, a manifest, a firmware image
  or the demo zip doesn't match the version in `src/main.cpp`.
- `gen_patexp.py` – writes `src/gnuboy/patexp.h`, the tile-row lookup tables used by the Game Boy renderer.
- `hosttest/palstress.c` – runs a ROM against a deliberately slow, uneven display and fails if the emulator
  has to hold color changes back; catches stalls between the emulator and the display task.
- `hosttest/sramtest.c` – counts cartridge-RAM swaps to the SD card for a given number of resident pages.
- `hosttest/ringstall.c` – wedges the display and checks the emulator keeps finishing frames instead of
  spinning forever, which on the board would be a frozen menu and a reset. Build with a short
  `-DGB_RING_WAIT_US=2000` so it runs in a second.
- `hosttest/romtrace.c` – records which 4 KB ROM page a real game reads, in order, for working out how
  much of a big game has to come off the SD card. Build with `-DGB_TRACE_ROM`.
- `hosttest/dkjam.c` – prints the hardware state frame by frame (registers, bank, PC, interrupts, palettes)
  for working out why a game stops drawing. Build with `-DGB_PROBE`.
