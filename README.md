# H@N-CYD-GBC

**Game Boy, Game Boy Color and NES emulator for the ESP32 "Cheap Yellow Display" (CYD):
the 2.4" ESP32-2432S024C and ESP32-2432S024R, the 2.8" ESP32-2432S028R, ESP32-2432S028C and two-USB
"CYD2USB", the 2.8" JC2432W328C, the 3.2" ESP32-2432S032C and ESP32-2432S032R, the 3.5" ESP32-3248S035C and
ESP32-3248S035R, the touch-less ESP32-2432S024N and ESP32-2432S032N, the ESP32-2432S028R Plus, and the Elecrow CrowPanel ESP32 2.4" and
2.8" (including the 2.4" "ESP32 Solo Miner LCD", SKU DHM04624D)**, with Bluetooth controllers (PS4, PS5, Xbox, Switch Pro, 8BitDo and more), save states, cheats, menu themes, an A–Z game list, board LED effects
and a one-click web flasher.

<p align="center"><img src="docs/images/boot.gif" width="640" alt="The wireframe H@N boot screen"></p>

> [!WARNING]
> **Use the build for your exact board** (see [Supported boards](#supported-boards)). Other "CYD"
> boards (ESP32-S3 boards, the 4.3" ESP32-8048S043, the 2.2" ESP32-2432S022...) use different chips,
> screens or pins. The firmware won't work on them and will most likely show a
> blank screen.

**Flash it from your browser:** <https://hackatnight.com/HAN-CYD-GBC/>

---

## Contents

- [Supported boards](#supported-boards)
- [Features](#features)
- [Quick start](#quick-start)
- [Flashing in detail](#flashing-in-detail)
- [Setting up the SD card](#setting-up-the-sd-card)
- [Sending games over USB](#sending-games-over-usb) and the [demo ROMs](#demo-roms)
- [Using it](#using-it): game list, A–Z jump, controls, pause menu, save states, cheats, settings, themes, LED
- [How it works](#how-it-works)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)
- Board note: [Elecrow ESP32 Solo Miner LCD 2.4" - the touchscreen reads GPIO4, not GPIO12](docs/elecrow-esp32-solo-miner-2.4-touch.md)
- [Credits and licenses](#credits-and-licenses)

> The menu pictures in this README are illustrations drawn from the firmware's own layout and colors,
> not photos of the screen. The boot and loading animations were rendered by the firmware's own
> wireframe code.

---

## Supported boards

| Board | Screen and touch | Status |
|---|---|---|
| **ESP32-2432S024C** | 2.4" ILI9341 · CST820 capacitive | ✅ **Tested** |
| **Elecrow CrowPanel ESP32 2.4" / 2.8"**<br><sub>incl. the 2.4" Solo Miner, SKU `DHM04624D`</sub> | ILI9341V · XPT2046 resistive (shared bus) · no RGB LED | ✅ **Tested** |
| **ESP32-2432S024R** | 2.4" ILI9341 · XPT2046 resistive (shared bus) | 🟡 Untested |
| **ESP32-2432S028R** | 2.8" ILI9341 · XPT2046 resistive · one micro-USB | 🟡 Untested |
| **ESP32-2432S028 "CYD2USB"** | 2.8" ST7789 (usually) · XPT2046 resistive · two USB ports | 🟡 Untested |
| **ESP32-2432S028R Plus** | 2.8" ST7789 (no inversion) · XPT2046 resistive · USB-C | 🟡 Untested |
| **ESP32-2432S028C** | 2.8" ILI9341 · CST820 or GT911 capacitive | 🟡 Untested |
| **ESP32-2432S032R** | 3.2" ST7789 IPS · XPT2046 resistive (shared bus) | 🟡 Untested |
| **ESP32-2432S032C** | 3.2" ST7789 IPS · GT911 capacitive | 🟡 Untested |
| **JC2432W328C** | 2.8" ST7789 IPS · CST820 capacitive (Guition) | 🟡 Untested |
| **ESP32-3248S035R** | 3.5" ST7796 320×480 · XPT2046 resistive (shared bus) | 🟡 Untested · letterboxed † |
| **ESP32-3248S035C** | 3.5" ST7796 320×480 · GT911 capacitive | 🟡 Untested · letterboxed † |
| **ESP32-2432S024N** | 2.4" ILI9341 · **no touch** (controller only) | 🟡 Untested |
| **ESP32-2432S032N** | 3.2" ST7789 IPS · **no touch** (controller only) | 🟡 Untested |

✅ **Tested** - display, touch, sound, SD card, PS4 and other Bluetooth controllers all confirmed on a real
board. 🟡 **Untested** - built in and expected to work, but nobody has reported back yet.

† The 3.5" boards have a 320×480 screen and currently show the same 240×320 picture centred, with a black
border. A full-screen layout is planned once someone confirms the board works.

- **Two builds per board:** a Bluetooth one, and for the 2.4" C a touch-only one that drops the controller
  (and so stays in portrait, since landscape needs one).
- **Other controllers:** new in 2.15.0, on every Bluetooth build: PS5 DualSense, Switch Pro and Joy-Con, Xbox
  Wireless (Bluetooth models), 8BitDo, Stadia and most Android-style pads, including Bluetooth Low Energy ones.
  PS4 and Stadia have been tried on real boards.
- Tried a board? **[Send a board test report](https://github.com/hackatnight/HAN-CYD-GBC/issues/new?template=board-test.md)**
  and the table gets updated.

All of them are the classic ESP32 (dual core, 240 MHz, 4 MB flash, no PSRAM), with a microSD slot and a speaker
connector, so every feature works on every board. All but the CrowPanel have an RGB LED (on the 2.8" R Plus it uses other pins). They differ in the backlight
pin (21 on the 2.8" CYDs, 27 on the others), the touch chip and the screen chip.

**Which build?** Check the model number on the back of the board.
- **2.4":** the last letter is the touch type: **C** = capacitive (your fingertip), **R** = resistive (press firmly).
- **2.8":** **R** with one micro-USB port, the **CYD2USB** with two ports, or **C** (capacitive; its touch ribbon
  has 6 wires instead of 4).
- **3.2" (ESP32-2432S032) and 3.5" (ESP32-3248S035):** again **C** = capacitive, **R** = resistive.
- **N** (2.4" ESP32-2432S024N, 3.2" ESP32-2432S032N) = **no touch screen**. These builds are played with a Bluetooth
  controller; see [Boards with no touch screen](#boards-with-no-touch-screen).
- **JC2432W328C:** a 2.8" capacitive board from another maker (Guition), wired like the 2.4" C but with an ST7789
  IPS screen. It asks for a touch calibration on first start.
- **ESP32-2432S028R Plus:** a newer 2.8" with a USB-C port and an ESP32-WROOM-32E, often sold in NerdMiner
  Bitcoin-miner kits. Use its own build. Going by the maker's pin guide, it differs from the 2.8" R in three ways:
  an ST7789 screen with no color inversion, the RGB LED on GPIO 17 / 22 / 16 (red / green / blue), and GPIO4 switching
  the speaker amplifier on (the 2.8" R build would flicker that pin as the LED's red channel and cut the sound).
  The build holds GPIO4 high. If the board makes no sound at all, tell us: the amplifier may want it low instead.
- **Elecrow CrowPanel ESP32 HMI 2.4" and 2.8":** one build for both (the original ESP32-WROOM versions, *not* the
  "Advance" ESP32-S3 ones). They're wired like the 2.4" R (ILI9341, resistive touch on the screen's bus, backlight 27,
  same SD card and speaker pins), with two differences: there's no RGB LED, so the LED settings are left out (its
  pins 16 and 17 go to the UART1 connector instead), and the touch chip's pen interrupt is on GPIO36.
  This build also covers the **2.4" ESP32 Solo Miner LCD** (SKU `DHM04624D`, sold as a Bitcoin solo miner
  display), which is the same board with one wiring difference: its touch chip answers on **GPIO4**, not
  GPIO12. If touch does nothing on one of these, that pin is almost certainly why -
  see **[Elecrow ESP32 Solo Miner LCD 2.4" - the touchscreen reads GPIO4, not GPIO12](docs/elecrow-esp32-solo-miner-2.4-touch.md)**.
  The CrowPanel 3.5" isn't supported yet (ILI9488 screen, two hardware versions with different pins).

### Boards with no touch screen

The **N** builds (ESP32-2432S024N, ESP32-2432S032N) start in landscape and are made for a Bluetooth controller.
1. Flash the board and power it on. While no controller is connected, the bottom of the game list shows how to
   pair, and the board is already listening for one.
2. Put the controller in pairing mode (PS4: SHARE + PS; PS5: CREATE + PS; Xbox: pair button; Switch Pro: sync
   button). It connects in a few seconds and reconnects by itself from then on.

Until a controller connects, the **BOOT** button works the menus: **tap** = down, **hold and let go** = select,
**hold 2 s** = back (on the game list's top level, that opens Setup). Once a controller is connected, BOOT goes back
to its usual jobs. The Bluetooth on/off switch, the touch items and the on-screen buttons are left out of these
builds, so you can't lock yourself out. Holding BOOT while powering on resets the settings and forgets all paired
controllers.

### Testing the new builds (everything but the 2.4" C)

These builds have passed every check I could run without the boards in hand, but nobody has run them on a real
board yet. If you have one, please try it and
**[send a quick board test report](https://github.com/hackatnight/HAN-CYD-GBC/issues/new?template=board-test.md)**.

The checks run so far:
- It compiles, and every pin matches the board's published pin map with no clashes.
- On the 2.8" R and CYD2USB, the touch chip is read in software on its own pins, so it never shares a
  connection with the SD card.
- On the 2.4" R, the touch chip shares the screen's connection. Touch reads wait for the screen to finish drawing,
  so the two never talk over each other.
- On the 2.8" C, the firmware checks at start whether a GT911 answers, and otherwise uses the CST820. The serial
  log says which. The 3.2" C and 3.5" C do the same (they normally have a GT911).
- On the 3.2" R and 3.5" R, the touch chip shares the screen's connection, like on the 2.4" R.
- On the 3.5", the screen is set up as a 240×320 window in the middle of its 320×480 panel, and the rest is
  cleared to black at start.
- [`tools/touchtest`](tools/touchtest) runs the firmware's touch calibration against simulated resistive panels, in
  all 8 ways the touch layer can be wired and all 4 screen orientations. Taps land within 1.4 px, and a bad
  calibration is always refused. It also covers the 3.5" case, where the touch layer spans the whole screen but
  the picture uses only the middle (taps land within 2.4 px).

What to expect:
1. **First start:** after the boot animation, it asks you to **tap four crosses** to calibrate the touch screen.
   A resistive screen needs this once; it's saved after that. The 2.8", 3.2" and 3.5" C boards ask too, because
   their touch chip varies (and on the 3.5" the picture doesn't start at the touch layer's corner).
   *Setup → Display & touch → Calibrate touch* redoes it.
2. **It starts in portrait** with on-screen buttons, so it can be played by touch right away. If the picture is
   upside down, **hold BOOT for 1 second** on the game list to rotate it. Pair a controller to play in
   landscape.
3. Everything else (SD card, sound, LED, USB transfer, cheats, saves) is the same as on the 2.4" C board.

**Which 2.8" build?** Count the USB ports.
- **One** micro-USB port: the **ESP32-2432S028R** build.
- **Two** ports (micro-USB and USB-C): the **CYD2USB** build.

The 3.2" boards use an ST7789 IPS screen, so their builds start with ST7789 and inverted colors and have the same
**Screen chip** switch. If the colors look wrong, use **Invert colors** and **Swap red/blue** in the same menu.

The two-USB board is the same board with a different screen chip on most units: an ST7789 instead of an ILI9341,
with colors that come out inverted. Not every unit is the same, so both 2.8" builds have a
**Setup → Display & touch → Screen chip** switch (ILI9341 / ST7789).
- If the colors look wrong, washed out or like a photo negative, flip **Screen chip** and **Invert colors**
  until the picture looks right. Everything else is identical.
- The CYD2USB build starts with ST7789 and inverted colors; the S028R build starts with ILI9341, no inversion.
- Holding **BOOT** while powering on puts those defaults back.

---

## Features

| | |
|---|---|
| **Systems** | Game Boy (`.gb`), Game Boy Color (`.gbc`), NES (`.nes`, most licensed games) |
| **Speed** | Full speed (60 fps emulated, 50 for PAL NES), sound through the board's speaker |
| **Games** | Read from a microSD card, organized in any folders you like, thousands per folder |
| **Two builds** | Bluetooth (PS4, PS5, Xbox, Switch Pro, 8BitDo... controllers, landscape) or touch-only (on-screen buttons, portrait, more game space) |
| **Saving** | Battery saves written automatically, plus one save state per game for every system |
| **Cheats** | GameShark / Pro Action Replay codes from `.cht` files, a built-in cheat finder, and a ready-made pack for 2,572 games |
| **Game list** | Folders, scrolling long names, A–Z jump (letter strip in landscape, letter grid in portrait) |
| **Shortcuts** | Continue your last game in one tap, plus Favorites, Recent and Most played lists at the top |
| **Looks** | Code-drawn wireframe boot and loading screens, 4 menu themes, animated menus |
| **LED** | The board's RGB LED: solid or breathing in any color, blinks on saves and cheats |
| **Big games** | Up to 8 MB: what doesn't fit in flash is read from the SD card while you play |
| **Menu music** | Two original chiptune loops in the menus, with their own on/off and volume |
| **USB transfer** | Send games from the web page straight to the SD card over the USB cable, sorted into the right folder |

---

## Quick start

You need:

- an **ESP32-2432S024C** or **ESP32-2432S028R** board
- a **microSD card** (any size; it gets formatted FAT32)
- a **USB cable that carries data** (some cables only charge)
- **Chrome or Edge on a computer**, for the web flasher
- optional: a **Bluetooth controller** for the Bluetooth build: PS4, PS5, Xbox (Bluetooth models), Switch Pro, 8BitDo...
- your own game files (`.gb`, `.gbc`, `.nes`). This project doesn't include or link to any games.

Steps:

1. **Pick a build.** On the **ESP32-2432S024C**:
   - **Bluetooth:** best with a controller (PS4, PS5, Xbox, Switch Pro, 8BitDo...). Starts in landscape and has 2.8 MB of flash for games.
   - **Touch-only:** plays with on-screen buttons in portrait. It has 3.25 MB of flash for games and more free memory.

   On any **other board**, use the build with its model number: **ESP32-2432S024R**, **ESP32-2432S028R** (one USB
   port), **CYD2USB** (two USB ports), **ESP32-2432S028C**, **JC2432W328C**, **ESP32-2432S032R / C / N**,
   **ESP32-2432S024N**, **ESP32-3248S035R / C** or the **Elecrow CrowPanel 2.4" / 2.8"** (one build for both).
   Each has touch and Bluetooth, and 2.8 MB for games.
   See [Testing the new builds](#testing-the-new-builds-everything-but-the-24-c).
2. **Flash it:** open the **[web flasher](https://hackatnight.com/HAN-CYD-GBC/)**, plug the board in, click
   *Install*, pick the board's port, choose *Erase* and wait about a minute.
3. **Prepare the SD card:** format it FAT32 and create `Roms/GB`, `Roms/GBC` and `Roms/NES`, then copy your
   games into them. Optional: unzip the [cheat pack](docs/cheats/HAN-CHEATS-GB-GBC-NES.zip) to the root of the card.
4. **Power on:** insert the card and press RST. After the boot animation you'll see your folders.
5. **Bluetooth build only:** open *Setup → Controller (Bluetooth) → Pair new controller*. Then put the controller
   in pairing mode: **PS4** hold SHARE + PS, **PS5** hold CREATE + PS, **Xbox** hold the pair button on top,
   **Switch Pro** hold the sync button on top. For others, see the controller's manual.
6. **Tap a game.** The first launch copies it into the board's flash, with a progress bar. After that it starts instantly.

---

## Flashing in detail

Every build is a single **merged image** that goes at address **0x0**. It contains the bootloader, the
partition table and the app, so no other files are needed. Flashing a merged image also clears saved settings
and controller pairings. Your saves are on the SD card and aren't touched.

### Option 1: web flasher (easiest)

Open **<https://hackatnight.com/HAN-CYD-GBC/>** in Chrome or Edge on a computer. The page uses
[ESP Web Tools](https://esphome.github.io/esp-web-tools/) and has every build built in.

1. Click **Install Bluetooth build** or **Install touch-only build**.
2. Pick the board in the port list. It's usually *USB-SERIAL CH340* or *CP210x*. If nothing shows up, install the
   [CH340 driver](https://www.wch-ic.com/downloads/CH341SER_ZIP.html), replug the board and try again.
3. Tick **Erase device** the first time, then **Install**.
4. When it says done, press **RST** on the board.

If it can't connect, hold **BOOT**, tap **RST**, let go of BOOT and click Install again.

### Option 2: esptool (command line)

The images are in [`docs/firmware/`](docs/firmware):

```sh
pip install esptool
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-BT.bin
# or
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-touch.bin
# ESP32-2432S028R (2.8", one USB port):
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-2432S028R.bin
# 2.8" with two USB ports (CYD2USB):
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-2432S028-2USB.bin
# ESP32-2432S024R (2.4" resistive) / ESP32-2432S028C (2.8" capacitive):
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-2432S024R.bin
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-2432S028C.bin
# 3.2" ESP32-2432S032R / C and 3.5" ESP32-3248S035R / C:
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-2432S032R.bin
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-2432S032C.bin
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-3248S035R.bin
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-3248S035C.bin
# JC2432W328C (2.8" capacitive) and the no-touch ESP32-2432S024N / ESP32-2432S032N:
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-JC2432W328C.bin
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-2432S024N.bin
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-2432S032N.bin
# ESP32-2432S028R Plus (USB-C, NerdMiner kits):
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-2432S028R-Plus.bin
# Elecrow CrowPanel ESP32 2.4" / 2.8":
esptool.py --chip esp32 --baud 921600 write_flash 0x0 HAN-CYD-GBC-CrowPanel-2.4-2.8.bin
```

### Option 3: Espressif's online tool

Open <https://espressif.github.io/esptool-js/>, click *Connect*, set the address to `0x0`, choose the `.bin`
and click *Program*.

---

## Setting up the SD card

<p align="center"><img src="docs/images/sd-layout.png" width="720" alt="SD card folder layout"></p>

- Format the card as **FAT32**.
- Put games under **`/Roms`**, in any folders and sub-folders you like. The file extension picks the emulator,
  so one folder can mix systems.
- The game list starts in `/Roms`. If there's no `/Roms` folder, it starts at the root of the card.
- Other folders at the top of the card are ignored when `/Roms` exists. For example, a `DCIM` folder that a camera or
  card reader adds won't show up or get in the way.
- **`/CHEATS`** holds cheat files in `GB`, `GBC` and `NES` sub-folders. It never shows in the game list.
- **`/gbc/saves`** is created for you. It holds battery saves (`.sav`, plus `.rtc` for clock games) and save
  states (`.st0`). It's hidden from the game list too.
- **There's no limit on games per folder.** Only the names are kept in memory, so a folder can hold a few thousand.
  If memory ever runs short, the last row says how many weren't listed.

---

## Sending games over USB

You don't have to take the SD card out to add games. The board can receive them over the same USB cable you
flash with:

1. Plug the board into a computer and leave it on the game list. You can also open
   *Setup → Transfer games (USB)*.
2. Open **<https://hackatnight.com/HAN-CYD-GBC/#send>** in Chrome or Edge and click **Connect**. Pick the board's
   port. The board switches to its *USB transfer* screen by itself.
3. Drop games on the page, or click to choose them. You can drop several at once.
4. Click **Done** when you're finished. The board goes back to the game list with the new games in it.

Where files go:

| File | Goes to |
|---|---|
| `.gb` | `/Roms/GB` |
| `.gbc` | `/Roms/GBC` |
| `.nes` | `/Roms/NES` |
| `.cht` | `/CHEATS/GB`, `/CHEATS/GBC` or `/CHEATS/NES` (you pick which on the page) |
| anything else | skipped |

The page also has a **Save games to** list with every folder under `/Roms`, if you'd rather put a game in a
category folder like `Roms/GBC/RPG`.

**Nothing on the card is erased.**
- Each file is written as `name.part` and renamed only once it has fully arrived and been checked. If the cable
  comes out halfway, you won't get a broken game in your list.
- If a game with the same name is already there, the page asks: **Replace it**, **Keep both** (saved as
  "Name (2).gb") or **Skip**.

**Speed:** the USB link runs at 921,600 baud, about **75–85 KB/s**. That's roughly 13 seconds per MB, so a 1 MB
game takes about 13 seconds and a 4 MB game a bit under a minute. For dozens of games at once, putting the card
in a card reader is still faster.

**How it works:**
- The page uses Chrome's Web Serial feature to talk to the board's USB-serial chip.
- It sends each file in 4 KB pieces, each with a CRC-32 checksum. The board writes a piece to the SD card only
  if its checksum matches, and asks for it again otherwise.
- The protocol is a few text commands (`HELLO`, `LIST`, `STAT`, `MKDIR`, `PUT`, `BYE`) described at
  `xferCommand` in `src/main.cpp`. The page's side is [`docs/send.js`](docs/send.js).
- [`tools/usbtest`](tools/usbtest) runs the firmware's transfer code on a PC against `send.js`. It sends files from
  0 bytes to 2 MB, including runs where pieces are damaged or lost on the way, and checks every byte arrives.

The ESP32 on this board can't appear as a USB drive (that needs an ESP32-S2 or S3), which is why it works through
the page instead.

---


### Demo ROMs

Want something to test a new board with? There's an original **H@N demo** for each system, free to share:
[`HAN-DEMO.gb`](docs/demos/HAN-DEMO.gb), [`HAN-DEMO.gbc`](docs/demos/HAN-DEMO.gbc),
[`HAN-DEMO.nes`](docs/demos/HAN-DEMO.nes) (or [all three as a .zip](docs/demos/HAN-DEMO-ROMS.zip)).
On the web page, **Send the demos to my board** copies all three over USB in one go.

<p align="center"><img src="docs/images/demo-gbc.gif" width="320" alt="Game Boy Color demo"> <img src="docs/images/demo-nes.gif" width="256" alt="NES demo"></p>

| Scene | What it shows |
|---|---|
| Intro | "H@N presents", the system's name and hackatnight.com typed out over a drifting starfield |
| Wireframe H@N | The boot screen's 3D H@N outline swinging in perspective, with a warp starfield |
| Spinning cubes | Two wireframe cubes turning opposite ways, with falling stars |
| Dot ball | A sphere of dots turning, a small H@N inside, twinkling stars |
| Pyramid | A wireframe pyramid with a small H@N orbiting it, diagonal stars |
| D-pad cube | A cube you spin with the D-pad (each press pushes the spin that way), H@N inside; the stars stream against the spin |
| Tesseract | A 4D hypercube turning inside out, H@N in its core, warp-in starfield |
| Raster wobble + scroller | Text rows bent one scanline at a time, plus a sprite sine scroller with demoscene greetings |
| Input test | A controller drawing; each button lights up while held, with its name |

HACKATNIGHT.COM flies across every scene on a row nothing else uses.

Controls: **A** next scene, **B** next tune (Night Drive, Starfield, Pixel Parade, Vector Balls, Arpeggio, then off;
the tune's name shows on the hint row). In the shape scenes **SELECT** recolors the shape and **START** recolors the
H@N (8 colors each; Game Boy Color and NES), and **left / right** turns the spin around. While you're pressing buttons
a scene stays put; the input test goes back to the intro after about 13 seconds without a press.

How it works:
- **Game Boy / GBC** (one ROM, MBC5, 512 KB; the .gbc copy runs in color mode with per-scene palettes). The 3D
  frames are rendered on a PC and stored in the ROM. Two copies of the picture's tiles sit in video memory and the
  screen shows one while the next frame is copied into the other; then they swap, about 15 times a second. On the
  Game Boy Color the hardware does the copy itself, 16 bytes in every HBlank (HBlank DMA); on the original Game Boy
  the program copies six bytes in each HBlank. The flying hackatnight.com and the wobble change the horizontal
  scroll per line from an interrupt that only runs on the lines that need it, and on the GBC the same interrupt
  rewrites a palette color for each line.
- **NES** (mapper 4 / MMC3, 32 KB code, 256 KB graphics). Each 3D frame is its own pair of 1 KB graphics banks, so
  showing the next frame is just two mapper writes. The wobble and the flying hackatnight.com use the MMC3's
  scanline interrupt to change the scroll every two lines.
- **Music**: four channels (two pulses, the GB wave / NES triangle for bass, noise for drums) playing the same note
  data as the firmware's menu tunes, from [`tools/demos/tunes.py`](tools/demos/tunes.py).
- Everything is built from scratch by [`tools/demos/build.py`](tools/demos): a small assembler for each CPU
  (`sm83asm.py`, `asm6502.py`), the demo code (`gb_demo.asm`, `nes_demo.asm`), and the graphics (`gfx.py`, using the
  same wireframe outline as the firmware). `gbrun.c` and `nesrun.c` run the ROMs in the firmware's own emulator
  cores on a PC, press buttons from a script and save frames; that's how the GIFs above were made. The font is the
  BSD-licensed Adafruit 5x7 font ([license](tools/demos/FONT-LICENSE.txt)); no game or BIOS data is included.

## Using it

### The game list

<p align="center">
<img src="docs/images/browser-landscape.png" width="400" alt="Game list in landscape with the A-Z strip">
<img src="docs/images/browser-portrait.png" width="300" alt="Game list in portrait">
</p>

- Folders come first, then games, A to Z. Each row shows the system and the file size.
- **Long names:**
  - With a controller, the selected row's name scrolls sideways when it's cut off.
  - With touch, each cut-off name on screen takes a turn scrolling.
  - Other long names end in `~`.
- **Swipe** to scroll. Tap a folder to open it and a game to start it. Use **Back**, the controller's Back button or a
  short press of **BOOT** to go up a folder.
- The selected row glows, and the header has twinkling stars and a light that runs along its bronze edge.
- **Hold BOOT for 1 second** to switch to the next screen orientation.

### Continue, Favorites, Recent and Most played

<p align="center"><img src="docs/images/main-list.png" width="400" alt="The main list with Continue, Favorites, Recent and Most played"></p>

The top of the main list has shortcuts, so you don't have to dig through thousands of games:

| Row | What it does |
|---|---|
| ▶ **Continue: *game*** | Starts the last game you played, in one tap |
| ★ **Favorites** | The games you've marked, A to Z |
| 🕒 **Recent** | Your last 15 games, newest first |
| 📊 **Most played** | Your top 15 by total time played, with the time shown |

Each row only appears once it has something in it, so after a fresh flash or with a new card the list looks the
same as before.
1. Play any game for a moment, then choose *Save & quit to list*.
2. Go back up to the **main list**, the first screen with your GB / GBC / NES folders. The rows only show there,
   not inside a system folder.

Continue, Recent and Most played then appear. Favorites appears once you mark your first one.

**Marking a favorite:** either way switches it on and off.
- **Hold your finger on a game** in any list for half a second. The LED blinks if *Flash on cheat* is on.
- **In a game:** *pause menu → Favorite*.

Favorites show a small star next to their name everywhere.

**How it's stored:**
- A small text file, `/gbc/history.txt`, on the SD card, so it survives reflashing. Each line holds a game's path,
  total play time, when you last started it, and whether it's a favorite.
- It's read and rewritten one line at a time, so it needs almost no memory. It's updated just before a game starts
  and just after it ends, never while you play.
- Games you move or delete simply drop out of the lists.
- Only the last 300 games you've played are kept (favorites are always kept).

### A–Z jump

This shows up in any folder that doesn't fit on one screen. Letters with no games are dimmed and skipped.

| Landscape: letter strip | Controller on the strip | Portrait: letter grid |
|---|---|---|
| <img src="docs/images/browser-landscape.png" width="260"> | <img src="docs/images/az-strip-focus.png" width="260"> | <img src="docs/images/az-grid-portrait.png" width="200"> |
| Tap a letter. The underlined letter is where you are now. | **Select (Share)** highlights the strip. Left/right picks a letter and the list follows; OK, Select or Down goes there; Back returns to where you were. | Tap **A-Z** (or press Select) to open the grid. Tap a letter, or use the d-pad and OK. |

### Controls

**Touch (portrait, on-screen buttons):**

- D-pad (8-way) on the left. **A** and **B** on the right; touching the gap between them presses both.
- **SEL** and **START** in the middle. **MENU** or the board's **BOOT** button pauses the game.
- The touch screen reads one finger at a time (a CST820 limit).
- With the on-screen buttons hidden, tap anywhere to open the pause menu.

**Bluetooth controller (Bluetooth build)** (PlayStation names; other controllers use the same positions):

| Controller | In a game | In menus |
|---|---|---|
| D-pad or left stick | D-pad | Move (hold to repeat) |
| Right / bottom face button (Game Boy layout) | A / B | OK / Back |
| Options | START | Opens Setup from the game list |
| Share | SELECT | A–Z jump in the game list |
| PS button | Pause menu | Close the menu |

- On Xbox and Switch controllers, Options / Share / PS are Menu / View / Xbox (or + / − / Home).
- The default **Button layout** is "Game Boy": the right button is A, like on a real Game Boy. Switch it to "Xbox"
  in *Setup → Controller* if you'd rather have the bottom button as A.
- With a controller connected, menus show which buttons do OK and Back, using the marks on that controller:
  ◯ / ✕ on PlayStation, B / A on Xbox-style pads, A / B on Switch and 8BitDo.

<p align="center"><img src="docs/images/controller-button-hints.png" width="480" alt="Menu button hints for PlayStation, Xbox-style and Nintendo-style controllers"></p>

- Every menu also has a touch Back button, so you're never stuck if the controller drops out.

### Pause menu, save states and cheats

<p align="center"><img src="docs/images/pause-cheats.png" width="720" alt="Pause menu and cheat list"></p>

Open it with MENU, BOOT, the PS button or a tap (when the on-screen buttons are hidden). It has: Resume,
**Save state**, **Load state**, **Cheats**, **Favorite**, **Write battery save**, Volume, Scaling, palette, Show FPS, **LED**,
Reset game and **Save & quit to list**.

**Battery saves.** These work like the cartridge's own save. They load when the game starts, and the emulator
writes them to the SD card by itself once the game has finished saving, or at least once a minute. Pausing and
quitting always write them. Games whose cartridge had no save memory (password games) show "n/a".

**Save states.** One slot per game, on every system. A save state freezes the exact moment so you can resume
there later.
- It's written to a temporary file first and then renamed, so a failed save never damages the old one.
- A state remembers the game's save memory too. Loading it puts the battery save back to that moment as well.
- A state from another game, or from an older firmware version, is refused without changing anything.

**Cheats.** The emulator re-writes chosen memory addresses with fixed values every frame. That's how GameShark
and Pro Action Replay codes work.
- **Codes it reads:**
  - GB/GBC: GameShark `ttVVLLHH`, where `tt` = `01` normal, `80`–`8F` a save-RAM bank, `90`–`97` a GBC work-RAM bank
  - NES: Pro Action Replay `AAAA:VV`, `AAAAVV`, `00AAAAVV`
  - Any system: `AAAA=VV`
  - Up to 4 codes per cheat, joined with `+`
- **Game Genie codes** change the game program itself rather than memory, so this cheat system can't use them.
  They're listed as "n/a".
- **Files:** one RetroArch-style `.cht` file per game in `/CHEATS/GB`, `/CHEATS/GBC` or `/CHEATS/NES`.
  The emulator finds the file even if the name doesn't match the ROM exactly: it ignores
  capitals, punctuation and anything in `( )` or `[ ]`, and accepts a single file whose name starts
  like the ROM's. Switching a cheat on or off saves the file.
- **Cheat finder:** enter a number you see on screen (for example 3 lives), play until it changes, and enter the
  new number. Repeat until only a few memory addresses are left, then pick one and the value it should keep.
  The new cheat is switched on and saved to the file.
- **Cheat pack:** [`docs/cheats/HAN-CHEATS-GB-GBC-NES.zip`](docs/cheats/HAN-CHEATS-GB-GBC-NES.zip) has usable codes
  for 739 Game Boy, 548 Game Boy Color and 1,285 NES games. They're filtered from
  [libretro-database](https://github.com/libretro/libretro-database) (CC BY-SA 4.0) by
  [`tools/cheats/make_cheat_pack.py`](tools/cheats/make_cheat_pack.py). Unzip it to the root of the card.

### Settings and the LED

<p align="center"><img src="docs/images/settings-led.png" width="720" alt="Settings and LED menus"></p>

**Setup** (the button in the game list's header, or Options on the controller):

| Setting | What it does |
|---|---|
| Game volume | Mute, 1–4 (sound in games) |
| Music | Opens the Music menu: on/off, which tune, and its own volume (see below) |
| Scaling | 1.5x sharp, 1.5x smooth, 1x, Fit, Stretch. Remembered separately for portrait and landscape; landscape defaults to Stretch. |
| DMG palette | Colors for original Game Boy games: GB green, Gray, Pocket, Warm, Red |
| NES palette | Nofrendo, Composite, NES Classic, NTSC, PVM, Smooth |
| Brightness | Screen backlight 25–100 % |
| Show FPS | Shows "drawn / emulated" frames per second in a corner of the game screen (emulated stays at 60) |
| Theme | Menu colors (see below) |
| LED | The board's RGB LED (see below) |
| Display & touch | Orientation, touch calibration and test, flip/mirror fixes, invert colors, swap red/blue |
| Transfer games (USB) | Opens the USB transfer screen (see [Sending games over USB](#sending-games-over-usb)) |
| System info | What this board is and how this build drives it: firmware and build, board name, chip, CPU, flash, free RAM, MAC, screen size and bus pins, DMA, backlight, touch chip with its data, CS and interrupt pins and clock, SD pins and card size, speaker, LED and Bluetooth. The same list is printed to the USB serial port when the screen opens, so it can be pasted into a bug report. Handy when a board misbehaves and you need to look up its schematic |
| Controller (Bluetooth build) | Bluetooth on/off, pair, button layout, on-screen buttons (Auto / Always / Never), forget controllers |

**Settings are saved on the board** the moment you change them, and they're still there after turning it off and on.
That covers Game volume, music on/off, tune and volume, theme, LED, scaling, orientation, button layout and so on.

With **Show FPS** on, a game that is too big for flash shows a third number, `s12`: the 4 KB pages it read from
the SD card in the last second. The first number is frames drawn, the second frames emulated (60 is full speed).
A high `s` number is why a 4 MB game stutters - the parts that don't fit in flash are being fetched as you play.
- For example, with *Music: Off* the board starts silent.
- Two things put everything back to the defaults:
  - **holding BOOT while powering on**
  - **flashing a merged `.bin`**, which includes the web flasher
- Your saves, save states, cheats, favorites and play history live on the SD card, so neither of those touches them.

**Music** (*Setup → Music*):

| Option | Choices |
|---|---|
| Music | On (default) / Off |
| Tune | **Night Drive** (A minor, 112 BPM, driving) or **Starfield** (E minor, 92 BPM, a calm arpeggio with an echo) |
| Music volume | 1–4, default 1. Separate from Game volume: changing one never changes the other. |

The music plays in the game list and the menus. It fades out before a game starts, because games use the same
sound output, and comes back when you return to the list.

**LED** (in Setup and in the pause menu, so you can change it mid-game):

| Option | Choices |
|---|---|
| Mode | Off, Solid, Breathing (a slow 3-second pulse) |
| Color | Theme (follows the menu theme), Red, Orange, Yellow, Green, Cyan, Blue, Purple, Magenta, White |
| Brightness | 25 / 50 / 75 / 100 % |
| During games | Same as menus, Steady, Off. The pause menu counts as a menu. |
| Flash on save | Three green blinks when a battery save or save state is written |
| Flash on cheat | Three yellow blinks when you switch a cheat on |

### Themes

<p align="center"><img src="docs/images/themes.png" width="720" alt="The four menu themes"></p>

*Setup → Theme* cycles through **H@N Night** (the default, taken from the H@N logo), **Neon**, **DMG Green** and
**Amber CRT**. Themes change the menus, the boot screen and the loading screen. Games are never affected.

### Loading screen

<p align="center"><img src="docs/images/loading.gif" width="480" alt="The wireframe loading screen"></p>

The first time you start a game, it's copied from the SD card into the board's flash. The H@N wireframe rocks
above the game's name and a progress bar while that happens. A 1–2 MB game takes about 5–20 seconds. The next
launch of the same game is instant. The logo pauses for a moment about every 64 KB copied, while the chip
erases flash and nothing can draw.

---

## How it works

<p align="center"><img src="docs/images/architecture.png" width="760" alt="How the parts fit together"></p>

**Two cores.** The ESP32 has two processor cores.
- **Core 1** runs the emulator, one frame at a time, and hands each finished line of the picture to a "line ring".
  It also sends sound to the speaker through the chip's I2S unit and internal DAC (GPIO 26).
- **Core 0** runs a display task. It takes lines from the ring, scales them to the chosen size, adds the FPS
  counter, and sends them to the ILI9341 screen by DMA while the emulator is already working on the next frame.
- Bluetooth also runs on core 0.

If the screen can't keep up, frames are skipped. The game itself always runs at full speed.

**Game Boy renderer.** Each tile row is two bytes holding one bit of every pixel's color. Instead of looping over
the eight bits, the renderer looks both bytes up in `src/gnuboy/patexp.h` (generated by `tools/gen_patexp.py`),
which spreads a byte into eight, and ORs them: `PATEXP[lo] | (PATEXP[hi] << 1)`. That cut about a sixth off the
emulator's total work. A whole tile of a Game Boy Color background is also blended into the line in two 32-bit
steps instead of eight single-byte ones, worth another ~6%. Both measured with callgrind on real games.
`tools/hosttest` checks the result against stored reference frames from dmg-acid2, cgb-acid2, cpu_instrs,
instr_timing, mem_timing and halt_bug.

Game Boy lines sit in the ring as 1-byte color numbers, so a whole frame is 23 KB instead of 46 KB as RGB565.
That matters on Game Boy Color games: they need 24 KB more work RAM, and when a whole frame doesn't fit, the
emulator has to wait for the screen. To keep games that change colors partway down the screen exact, the emulator
writes each color change (line, slot, color) to a log as it draws, and the display replays the log as it reaches
each line, starting from the colors in effect at line 0. The log holds 2048 changes and the emulator never
waits on it: if a frame fills it, the rest are retried on the next line, so they show a line or two late
rather than holding the emulator up.
The log is sized from the ring (a game streaming from the SD card has a small ring and
gets a small log), because on a 4 MB game every kilobyte here is one fewer 4 KB page of ROM kept in RAM.

The emulator and the on-screen menu are the same task, so the emulator never waits on the display
without a way out: while it waits it lets the rest of the core run, and if the display has not freed a
line in a quarter of a second it gives up on the rest of that frame and carries on. A wedged display
then costs a torn picture instead of a board that stops answering its buttons and has to be reset. The
frame counter shows `!n` when that happens, and `tools/hosttest/ringstall.c` tests it.
`tools/hosttest/ringcheck.c` checks the ring against the core's own output, pixel for pixel, with a deliberately
slow and jittery display thread, and `palstress.c` checks that no color change is ever held back.

**No frame buffer.** The board has only about 300 KB of RAM and no PSRAM.
- GB/GBC lines go straight to the ring as screen colors, so Game Boy Color "hi-color" pictures come out exactly right.
- NES lines go as 8-bit palette numbers. The ring holds a whole frame when there's room, or fewer lines when there isn't.
- While an NES game runs, the NES core borrows the Game Boy core's unused video memory.

**Games run from flash.**

<p align="center"><img src="docs/images/flash-map.png" width="760" alt="Flash layout of both builds"></p>

- A game is copied once into the "game storage" area of the 4 MB flash chip. The chip maps it into memory, so
  the emulator reads the game as if it were in RAM, with nothing copied at play time.
- The board remembers which game is stored, so the next launch skips the copy.
- Games bigger than game storage (some GBC games are 4–8 MB) keep their first part in flash. The rest is read
  from the SD card in 4 KB pieces as needed, which can cause brief pauses.

**Saving without stutter.** Many games use their save memory as scratch space and change it constantly.
- The emulator watches for writes and only saves once the memory has stopped changing, meaning an in-game save
  has finished, or at most once a minute.
- If a game's save memory doesn't fit in RAM, 4 KB pages of it are kept in RAM and the rest is read from the
  `.sav` file when needed, the way a cartridge would.

**Folders load fast.** Folders are read straight from the card's FAT file system in a single pass. Names and
sizes come from the folder itself, without opening any game file, so even a folder of thousands of games lists
almost at once.

**Code-drawn graphics.** There are no stored images in the firmware.
- The H@N logo is 106 points of outline, extruded into 3D and drawn as a wireframe into a small 4-bit buffer
  that's sent to the screen in one go, so it doesn't flicker.
- The stars, the shine, the glowing selection and the scrolling names are all drawn in code.
- Themes are seven colors each, plus three for the header.

**Menu music.** The two tunes are original, written for this project. They're stored as note data, about 1.5 KB
for both, not as recordings.
- A tiny synthesizer modeled on 8-bit sound chips plays them, with two square-wave voices, a 16-step triangle,
  a 16-step saw and noise drums.
- It runs as its own task on core 0, makes 22,050 samples a second, and fades in and out so the speaker never pops.
- The tunes are written in [`tools/music`](tools/music). `gen_tunes.py` turns them into `src/music/tunes.h`, and
  `compose.py` renders WAV previews.
- On a PC, the firmware's synthesizer matches those previews: on average they differ by 0.03 out of 255.

**The LED.** The RGB LED is on GPIO 4 (red), 16 (green) and 17 (blue) and lights when a pin is low. The chip's
LED-dimming hardware sets each color's brightness. A small timer every 30 ms works out the breathing level and
the save/cheat blinks, so the LED costs the games nothing.

**Slimmed Bluetooth (Bluetooth build).** The Bluepad32/BTstack Bluetooth library is trimmed to fit:
- one controller at a time
- a smaller receive buffer
- the drivers for PS4, PS5, Xbox, Switch Pro / Joy-Con, 8BitDo, Stadia and Android-style pads only, with a short
  list of their IDs instead of Bluepad32's 8 KB table (unknown pads use the Android driver, which suits most)
- Bluetooth Low Energy controllers work, but Bluepad32's own BLE settings service (for phone apps) is left out

BLE and the extra drivers came back in 2.15.0. They cost 69 KB of flash and 2 KB of RAM, so the app's space in
flash grew from 1 MB to 1.125 MB and the game area shrank from 2.94 MB to 2.81 MB. A game bigger than that still
plays: its least-used parts stay on the SD card, as before.

The modified files are in [`src/bt_slim`](src/bt_slim).

**The cores.**
- **gnuboy** for GB/GBC. CPU and timing are fixed so it passes all of Blargg's `cpu_instrs` tests and `instr_timing`.
- **nofrendo** (as optimized by retro-go) for NES.
- Save states were tested by saving, playing and loading on a PC. Picture and sound replayed byte-for-byte
  identically on 8 Game Boy test ROMs and 261 NES test ROMs and demos.

**NES mappers.** Supported: 0–5, 7, 9–11, 15, 16, 18, 19, 21–25, 30, 32–34, 40–42, 46, 50, 64–66, 70, 71, 73–76, 78,
79, 85, 87, 93, 94, 119, 160, 162, 163, 176, 185, 191–195, 206, 228, 229 and 231. That covers the large majority of
licensed games.
- Famicom Disk System and NSF music files aren't supported.
- Bootleg carts (mappers 91, 187...) show "Probably a bootleg cart".
- "(E)", "(Europe)", "(PAL)" and similar in a file name select PAL timing.

---

## Troubleshooting

| Problem | Try this |
|---|---|
| Blank or white screen after flashing | Make sure you flashed the build for your exact board (model number on the back): ESP32-2432S024C / 024R (2.4"), ESP32-2432S028R / 028C, CYD2USB (2.8", two USB ports), ESP32-2432S032R / C (3.2") or ESP32-3248S035R / C (3.5"). Other CYD boards aren't supported. |
| 2.8" or 3.2" board: colors inverted or washed out | *Setup → Display & touch → Screen chip* (ILI9341 / ST7789) and *Invert colors*. |
| 3.5" board: colors swapped or picture mirrored | *Setup → Display & touch → Swap red/blue* or *Mirror fix*. |
| Taps land in the wrong place (any board but the 2.4" C) | *Setup → Display & touch → Calibrate touch*, then tap each cross precisely (a stylus or fingernail helps on resistive screens). |
| Web flasher can't find the board | Use a data cable, install the CH340 driver, or hold BOOT, tap RST, release BOOT, then click Install. |
| "No SD card found" | Format FAT32, reseat the card, tap the screen to retry. |
| "Restarted: power dip (brownout)" | The speaker drew more than the USB supply could give. Use a short good cable or a 5 V 1 A+ adapter, or lower the volume. |
| Touch is off | *Setup → Display & touch → Calibrate touch*. |
| Touch does nothing at all on an Elecrow 2.4" board | If it's the **ESP32 Solo Miner LCD** (SKU `DHM04624D`), its touch chip answers on GPIO4, not GPIO12 like every other board of this family - fixed in 2.20.0, so flash that or later. Background: [the touchscreen reads GPIO4, not GPIO12](docs/elecrow-esp32-solo-miner-2.4-touch.md). |
| Controller won't pair | Bluetooth build only. Open *Pair new controller* first, then put the controller in pairing mode (PS4: SHARE + PS; PS5: CREATE + PS; Xbox: pair button; Switch Pro: sync button). If it paired with another device before, *Forget all controllers* and try again. Xbox controllers may need a firmware update (Xbox Accessories app) for Bluetooth. |
| A big GBC game says "Not enough memory" | Use the touch-only build, or turn Bluetooth off in *Setup → Controller* and restart. |
| No cheats for a game | The `.cht` file name must match the game's name (case, punctuation and `( )` / `[ ]` tags are ignored). Rename the file if needed. |
| NES game says "Mapper N isn't supported" | That cartridge chip isn't emulated (see the list above). Bootleg carts like the NES Street Fighter ports aren't supported. |

If the board restarts on its own, the next boot tells you why (power dip, crash or freeze).

---

## Building from source

The [web flasher](https://hackatnight.com/HAN-CYD-GBC/) is all you need to play. To build it yourself:

1. **Arduino IDE or arduino-cli** with:
   - **Bluetooth build:** the *esp32_bluepad32* 4.1.0 board package. Board manager URL:
     `https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json`
   - **Touch-only build:** the Arduino-ESP32 2.0.17 board package
   - the **LovyanGFX** 1.1.16 library
2. Board settings: *ESP32 Dev Module*, 240 MHz, QIO 80 MHz flash, 4 MB.
   - `partitions.csv` (Bluetooth layout) is picked up automatically.
   - For the touch-only build, copy `partitions_touch.csv` over `partitions.csv` first.
3. Bluetooth support turns on by itself when you build with the Bluepad32 board package.
   - For another board, define one of `BOARD_024R`, `BOARD_028R`, `BOARD_CYD2USB`, `BOARD_028C`, `BOARD_032R`,
     `BOARD_032C`, `BOARD_035R`, `BOARD_035C`, `BOARD_W328C`, `BOARD_CROW`, `BOARD_028PLUS`, `BOARD_024N` or `BOARD_032N` (the last two need the
     Bluetooth build). Either
     uncomment the line near the top of `src/main.cpp`, or with arduino-cli add
     `--build-property compiler.cpp.extra_flags=-DBOARD_028R` (for example). Use the Bluetooth partition layout.
4. To make a single merged image like the ones in `docs/firmware`:
   ```sh
   esptool.py --chip esp32 merge_bin -o merged.bin --flash_mode dio --flash_freq 80m --flash_size 4MB \
     0x1000 gbc_cyd.ino.bootloader.bin 0x8000 gbc_cyd.ino.partitions.bin 0x10000 gbc_cyd.ino.bin
   ```

Project layout:

| Path | What's there |
|---|---|
| `src/main.cpp` | Menus, display, sound, input, saves, cheats, themes, LED |
| `src/gnuboy/` | Game Boy / Game Boy Color core, plus glue (`gb_glue.c`) |
| `src/nofrendo/` | NES core, plus glue (`nes_glue.c`) |
| `src/wire/` | The wireframe H@N outline (generated by `tools/wire_logo.py`) |
| `src/music/` | The menu tunes as note data (generated by `tools/music/gen_tunes.py`) |
| `src/bt_slim/` | Slimmed Bluetooth library files (Bluetooth build) |
| `docs/` | Web flasher and "Send games" page (`send.js`), firmware images, demo ROMs, cheat pack, README images |
| `tools/` | Host tests (`usbtest` for USB transfers, `touchtest` for touch calibration, `bootnavtest` for the BOOT-button menus, `histtest`), logo, music and cheat-pack generators, and the demo ROMs (`demos/`) |

---

## Credits and licenses

- **gnuboy** (GB/GBC core): GNU GPL v2. Via GBCCAT / ODROID-GO, with fixes listed in the source.
- **nofrendo** (NES core), as optimized by **retro-go**: GNU (L)GPL v2. See `src/nofrendo/COPYING`.
- **Bluepad32** and **BTstack** (Bluetooth build): the BTstack files in `src/bt_slim` keep BlueKitchen's license,
  which allows personal, non-commercial use only.
- **LovyanGFX** (display driver): FreeBSD license.
- **Cheat pack**: adapted from libretro-database, CC BY-SA 4.0. See `LICENSE-CHEATS.txt` inside the zip.
- **ESP Web Tools** (web flasher): Apache 2.0, loaded from unpkg.
- Everything else © H@N ([hackatnight.com](https://hackatnight.com)). Because the emulator cores are GPL v2, the
  firmware as a whole is distributed under the **GNU GPL v2** (see [LICENSE](LICENSE)).

No games are included. Use only game files you have the right to use.
