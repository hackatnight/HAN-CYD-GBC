# H@N-CYD-GBC v2.5.0: Game Boy, Game Boy Color and NES emulator for the ESP32-2432S024C

Runs .gb, .gbc and .nes ROMs from the SD card on the 2.4" capacitive CYD
(ILI9341 240x320, CST820 touch, microSD, speaker on GPIO26, no PSRAM).

## Flash it

There are two builds. Both are complete images; write one at **offset 0x0**:

- `CYD-GBC-v2.0-BT-ESP32-2432S024C-merged.bin`: Bluetooth controller support (PS4, Stadia, Xbox, Switch, 8BitDo...).
- `CYD-GBC-v2.0-touch-only-ESP32-2432S024C-merged.bin`: no Bluetooth, about 60 KB more free RAM.

Ways to flash:

- **Browser:** open https://espressif.github.io/esptool-js/, connect, set the address to `0x0`, choose the .bin, then click Program.
- **esptool:** `esptool.py --chip esp32 --baud 921600 write_flash 0x0 <file>.bin`

Flashing a merged image also clears settings and controller pairings.
The flash layout changed in v2.1.1: the Bluetooth build has 1 MB for the app and 2.94 MB for ROMs,
the touch-only build 640 KB for the app and 3.31 MB for ROMs. After flashing, each ROM is
copied to flash again on its first launch.

## SD card

FAT32. Put games in a `/Roms` folder, organized however you like, for example:

```
/Roms/GB/...        .gb
/Roms/GBC/RPG/...   .gbc
/Roms/NES/Action/...  .nes
```

The browser starts in `/Roms` (or at the card's root if there is no `/Roms` folder) and shows
folders first, then games. Tap a folder to open it; "< Back", a controller's B button or a
short press of BOOT goes up one level. Each game's file extension picks the emulator, so a
folder can mix systems. Folders named `saves` and hidden folders are not shown.

There is no fixed limit per folder (it used to be 400). Only the names are kept in RAM, so a
folder can hold a few thousand games; if RAM runs low, the last row says how many weren't
listed. Since v2.3.2 a folder is read in one pass straight from the card's file system (FatFs):
names and sizes come from the folder itself and no game file is opened, so the list appears
as soon as the folder is read and scrolling doesn't touch the card. (v2.3.1 looked up each
visible row's size by opening the file, which takes about a second per row in a big folder.)

Battery saves are written to `/gbc/saves/<rom name>.sav` (Game Boy, plus `.rtc` files for MBC3
clock games) and `/gbc/saves/nes/<rom name>.sav` (NES).
"Write battery save" only works for games whose original cartridge had battery-backed save memory.
Games without it use passwords, or can't save at all; the menu shows "n/a" for those.

Battery saves are written automatically, but only once the game has stopped changing its
save RAM (an in-game save is finished), or at least once a minute; pausing and quitting always
write them. Many games also use their save RAM as work RAM (Super Mario Land 2, many NES games),
and writing it every 10 s whenever it changed made the game stutter (fixed in v2.3.1 for NES,
v2.3.3 for Game Boy).

### Cheats (v2.5.0)

Pause menu > **Cheats**: a small memory changer. A cheat keeps game-memory addresses at fixed
values (infinite lives, health, money, time...), written once per frame. It costs about
4 bytes of RAM per cheat and nothing when no cheat is on.

- **Cheat files**: RetroArch-style `.cht` files (or simple `Name = CODE` lines) in
  `/CHEATS/GB/`, `/CHEATS/GBC/` and `/CHEATS/NES/` at the root of the card, one `<game name>.cht` per game. The folder is hidden from the game browser.
  Codes: GameShark `ttVVLLHH` (GB/GBC; `tt` = 01, or 90-97 for a GBC work RAM bank), NES
  Pro Action Replay `AAAA:VV`, `AAAAVV`, `00AAAAVV`, or `AAAA=VV` on any system; several codes
  joined with `+`. Game Genie codes patch the ROM rather than memory: they're listed as "n/a".
  Switching cheats on or off writes the file back (RetroArch format).
- **Find a cheat**: the cheat finder. Enter the value you see on screen (e.g. 3 lives),
  play until it changes, search the new value, and repeat until a few places are left; pick
  one and the value it should keep. The new cheat is switched on and saved to the file.
  While searching it uses one bit per byte of game memory (NES up to 1.3 KB, GB 1 KB,
  GBC 4 KB), freed when the game ends.

### Save states (v2.3.0)

The pause menu has **Save state** and **Load state** for Game Boy, Game Boy Color and NES games:
one slot per game, saved at any moment and resumed exactly there. State files sit next to the
battery saves as `<rom name>.st0` (about 34 KB for a GB game, 58 KB for GBC plus its save RAM,
around 15-30 KB for NES). Saving writes a `.tmp` file first and renames it, so a failed save
never damages the previous state.

- A state includes the game's save RAM. Loading a state therefore also puts the battery save
  back to how it was at that moment (it is written to the `.sav` file at the next autosave).
- A state belongs to one game: loading a state made by another game (or a different version
  of this firmware) is refused without changing anything.
- Battery saves still load by themselves when a game starts; load them with the game's own
  Continue/Load menu as on the real console.

Tested on a PC: after save, play, load and replay, the picture and sound come out
byte-for-byte identical (8 Game Boy test ROMs, including paged save RAM, and 261 NES test
ROMs/demos across mappers 0-5 and others). For NES this needed two additions to nofrendo's
state format: an extra block with the CPU/PPU/APU timing it left out, and the real CHR-RAM
bank layout (it assumed CHR-RAM was never banked, which broke MMC1 games).

## How it works

- There is no PSRAM, so on the first launch of a ROM the emulator copies it from SD into
  flash and memory-maps it. You'll see a progress bar, usually 5-20 s for a 1-2 MB game.
  Launching the same ROM again starts instantly.
- Up to 2.94 MB (Bluetooth build) or 3.31 MB (touch-only build) of a ROM goes into flash, which covers almost every GB/GBC game. Pokémon
  Crystal is 2 MB.
- **Bigger ROMs (4 MB, up to 8 MB)**, such as Grand Theft Auto, are split: as much as fits goes
  to flash and the rest is read from the ROM file on the SD card in 4 KB pages while you play.
  Expect brief pauses when the game reaches parts it hasn't used recently. The SD card runs at
  40 MHz when it supports it (25 MHz otherwise).
- (v2.0 tried "learned" flash layouts for big ROMs. They made games slower on real hardware,
  so v2.0.1 switched them off; any ROM that v2.0 rearranged is copied again in plain order
  once. The planner code is still in gb_glue.c, unused.)
- How smooth a big ROM plays depends on free RAM. The touch-only build has about 60 KB more
  for this than the BT build. If the BT build says "Not enough memory" for a big ROM, try the
  touch-only build, or turn Bluetooth off in Setup → Controller and restart: Bluetooth then
  isn't started at all, which frees its memory.
- The screen is shown at 1.5x (240x216) with touch controls underneath. Display frames are
  skipped automatically while the game itself keeps running at full speed (60 emulated fps).

## Boot splash (v2.4.0)

The H@N splash is drawn entirely by code (`src/splash`), with no stored image and no image
decoder. The letters are signed distance fields computed once into a 33 KB lookup table
that only exists while the splash runs. Their depth is 30 slices shrinking toward a vanishing
point below the logo, darker the deeper. The face is chrome, the bevel is lit from the top
left, stars come from a pixel hash (the same every boot), and the mist is drifting value noise.

The logo appears in place (v2.5.0: the fly-in wasn't smooth on the board), "hackatnight.com"
types itself with a blinking cursor, and a shine sweeps across the chrome. It runs as a task on
core 0 while the rest of the board starts up, and only the parts that change are redrawn.
Tap the screen (or press BOOT) to skip it. Landscape and portrait have their own layout.
`tools/splashtest/host.c` renders the same code on a PC.

## Controls

- D-pad (8-way) on the left. A and B on the right; touching the gap between them presses both.
- SEL and START are in the middle.
- MENU, the BOOT button, or a controller's PS / home button pauses the game. Press it
  again (or B) to resume.
- Every menu has a touch Back button; in the Bluetooth build it shows the controller's Back
  symbol too (drawn PlayStation symbols: Circle = OK and Cross = Back with the "Game Boy"
  button layout, swapped with the "Xbox" layout). With a controller connected, the header
  also shows the OK symbol. Long menus scroll (drag, or the d-pad).
- In the game list, swipe to scroll, tap a folder to open it and a game to launch it.
  "Setup" opens the settings; on a PS4 controller the Share button does the same (the
  Setup button shows a "Share" tip in the Bluetooth build).
- Show FPS puts "drawn/emulated" frames per second in the top-left corner of the game
  (emulated should stay at 60, or 50 for PAL NES games).
- Hold BOOT for 1 second in the ROM list to switch to the next orientation.
- The BT build starts in Landscape L; the touch-only build starts in Portrait.
- With the on-screen controls hidden, tap anywhere on the screen to open the menu.

The CST820 touch panel only reports one touch point. A Bluetooth controller doesn't have that limit.

## Bluetooth controllers (BT build)

Tested with a PS4 controller.

Go to **Setup → Controller (Bluetooth) → Pair new controller**:

- **PS4:** hold SHARE + PS until the light bar flashes quickly.
- **Other controllers** (Xbox, Switch Pro, 8BitDo...): use the controller's own pairing mode.

Paired controllers reconnect on their own. Scanning stops while a controller is connected
and restarts when none are.

Controller mapping:

- D-pad or left stick = D-pad
- Start / Options = START, Select / Share = SELECT
- PS / home button = pause menu
- **Menus:** D-pad moves the highlight (hold to repeat), A or Start selects, B goes back.
  In the ROM list, left/right jumps a page and Select opens Setup.
- The default **Button layout** is "Game Boy" (positional): the right face button is A and the
  bottom button is B. Switch it to "Xbox" if you want the bottom button to be A.

**Save memory paging:** when there isn't enough RAM for a game's whole save memory, the
emulator keeps as many 4 KB pages in RAM as fit. The rest stays in the game's .sav file and is
read in from the SD card when the game uses it, the way a battery-backed cartridge would. The
pause menu then shows "(paged)" next to Save game to SD. The first time a game touches a page
that isn't in RAM there may be a short hitch.

Setup shows **Free memory**, measured while you browse. The ROM list is freed during play,
so a game gets more than that number shows. Large-save GBC games such as Pokémon Gold/Silver/Crystal need about
64 KB free. If one won't start on the BT build, use the touch-only build. The serial monitor
(115200 baud) prints free memory at boot and at game start.

## Settings (Setup in the ROM list)

- **Settings:** Volume; Scaling (1.5x sharp, 1.5x smooth, 1x, Fit, Stretch); DMG palette;
  Brightness; Show FPS (drawn/emulated); Free memory; Controller (BT build).
- **Display & touch:** Orientation (Portrait, Landscape R, Portrait flip, Landscape L; the
  landscape options are BT build only), Calibrate touch, Clear calibration, Touch test,
  Touch flip X / Y, Mirror fix, Invert colors, Swap red/blue.
- **Controller (BT build):** Bluetooth on/off, Pair new controller, Button layout,
  On-screen controls (Auto = hidden while a controller is connected, Always, Never),
  Forget all controllers.

Notes:

- Landscape R means you turn the board clockwise; Landscape L means counter-clockwise.
- The on-screen controls only appear in portrait.
- Scaling is remembered separately for portrait and landscape. Landscape defaults to Stretch.
- Scaling sizes in landscape (320x240):
  - 1.5x: 240x216
  - Fit: 266x240 (keeps the correct shape)
  - Stretch: 320x240 (fills the screen)
- Scaling sizes in portrait with the controls hidden: 1.5x and Fit are 240x216, centered;
  Stretch is 240x320.
- Frames are skipped automatically, so the game itself keeps running at full speed. Stretch
  has the most pixels to send.

To reset all settings, including calibration, hold **BOOT** while powering on.

## Bluetooth memory slimming (BT build)

`src/bt_slim/` holds modified copies of files from the Bluepad32 4.1.0 board package. They
only compile in the BT build, and the linker uses them in place of the precompiled originals:

- `uni_hid_device_slim.c`: 1 controller slot instead of 4 (saves about 17.5 KB of RAM), and
  **PS4 controllers only**: the other controller drivers and the controller ID table are left
  out. Other controllers fall back to the generic HID driver and may not work.
- `btstack_port_esp32_slim.c`: an 8 KB Bluetooth receive buffer instead of about 22 KB (the
  original size is meant for audio streaming; gamepad reports are about 80 bytes). Saves about
  14 KB of RAM. Bluetooth audio and the LE device database are not set up.
- `ble_off_slim.c`: no Bluetooth Low Energy. The PS4 controller uses classic Bluetooth, so
  Bluepad32's LE scanning and GATT service and BTstack's LE security manager are replaced by
  empty versions, which keeps the GATT/ATT/crypto code out of the build.

Together the PS4-only and no-LE changes cut the app by about 109 KB of flash and 4 KB of RAM.

Together they free about 32 KB of RAM. Only one controller can be connected at a time.

## Troubleshooting restarts

If the board restarted unexpectedly, the next boot shows why:

- **"power dip (brownout)":** the supply voltage sagged, usually because of the speaker plus
  a weak USB cable, port or hub. Try a short, good cable or a 5 V / 1 A+ adapter, or lower
  the volume.
- **"software crash"** or **"froze (watchdog)":** a bug. Note what you were doing.

## Build from source

- **Arduino IDE (verified):**
  1. Board package for the BT build: add
     `https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json`
     as a board URL and install "esp32_bluepad32" 4.1.0.
  2. Board package for the touch-only build: Arduino-ESP32 2.0.17.
  3. Install LovyanGFX 1.1.16.
  4. Choose "ESP32 Dev Module", 240 MHz, QIO 80 MHz, 4 MB. `partitions.csv` is picked up
     automatically. For the touch-only build, copy `partitions_touch.csv` over `partitions.csv`
     first (smaller app, more room for ROMs). The code turns Bluetooth on automatically when it's built with the
     Bluepad32 board.
- **PlatformIO:** `cyd2432s024c` builds the touch-only version. The `cyd2432s024c_bt` env is
  untested.

Core: gnuboy (GPL v2), taken from GBCCAT / ODROID-GO with these fixes:

- restored proper audio mixing (22 kHz, internal DAC)
- re-enabled sound register reads, which GBCCAT had disabled because of a crash bug
- Blargg cpu_instrs fixes (LD HL,SP+e, ADD SP,e, DAA, POP AF)
- HALT now waits correctly when interrupts are disabled
- ROM bank wrap-around
- fixed a NULL framebuffer pointer on the first frame
- no frame buffer: finished lines go to the display through a 32-line RGB565 ring, converted with the palette in effect at that line, so GBC hi-color screens are exact (saves about 13 KB)
- work RAM allocated per game in 4 KB banks; cartridge RAM in 4 KB pages, paged from SD when RAM is short

It passes all 11 Blargg cpu_instrs tests and instr_timing.

## NES (v2.2.0)

Core: nofrendo, as optimized by retro-go (LGPL v2, `src/nofrendo`), with these changes:

- no frame buffer: each line goes to the display through a ring of palette indices (a whole
  224-line frame, 57 KB, when it fits; smaller rings otherwise), lines 8-231 shown
- ROM data is read straight from the memory-mapped flash copy (nothing copied to RAM)
- the core's own memory (work RAM, name tables, sound, save RAM) is borrowed from the Game Boy
  core's idle video memory while an NES game runs, and given back afterwards
- CHR-RAM only for games that need it (saves 8 KB for most games)
- fixed: zero-page pointers at $FF now wrap to $00 (Blargg instr_test `(z,X)` / `(z),Y`)
- the 4,900-game fix-up database is left out to save 49 KB of flash; games use their iNES
  headers, and "(E)", "(Europe)", "(A)", "(Australia)" or "(PAL)" in the file name selects PAL timing
- in the touch-only build the 6502 core runs from IRAM; the BT build has no IRAM left for it

Mappers 0-5, 7, 9-11, 15, 16, 18, 19, 21-25, 30, 32-34, 40-42, 46, 50, 64-66, 70, 71, 73-76, 78,
79, 85, 87, 93, 94, 119, 160, 162, 163, 176, 185, 191-195, 206, 228, 229 and 231 are supported
(this covers the vast majority of licensed games). Famicom Disk System games are not (mapper
20 and the NSF music mapper 31 were removed in v2.4.0).
Known nofrendo limits: MMC3 scanline IRQ timing is approximate, and a few demos that rely
on exact mid-line timing don't draw.

A game on an unsupported mapper shows "Mapper N isn't supported" with "Probably a bootleg cart"
(unlicensed carts such as the NES Street Fighter II/III ports use their own chips: 91, 187, ...)
or "Not supported yet" for the few licensed games on chips nofrendo lacks (e.g. 69: Batman:
Return of the Joker, Gimmick!).

NES has no 1.5x mode (256 pixels is already wider than the portrait screen), so the
1.5x settings show the game in Fit. Setup and the pause menu have an "NES palette" choice.

## Roadmap

- Demoscene-style menus (copper-bar header, raster-bar highlight, sine scroller, one bitmap
  font): mock-up GIF first.
- Cheats: on-screen code entry, Game Genie (ROM patch) codes.
- NES bootleg mappers, if flash allows: 91 (Street Fighter III and others) and 187
  (Street Fighter II); more as they come up. The JY Company family (90/209/211) used by many
  bootleg fighting games is a bigger job.
- UI customization (e.g. the H@N logo faded behind loading screens): about 10-15 KB of flash
  per full-screen image, no RAM while playing. Postponed until the emulation side is settled.
