/*
 * H@N-CYD-GBC : Game Boy / Game Boy Color / NES emulator for the ESP32-2432S024C
 * (2.4" ILI9341 240x320, CST820 capacitive touch, microSD, speaker on GPIO26)
 *
 * Cores: gnuboy (ODROID-GO / GBCCAT optimized build, GPL v2) and nofrendo
 * (retro-go build, LGPL v2). No PSRAM on this board, so the selected ROM is copied
 * from the SD card into a flash partition ("rom") and memory-mapped. Re-launching
 * the same ROM skips the copy.
 *
 * SD layout:   games in /Roms and any sub-folders (or the root without /Roms);
 *              the file extension picks the emulator (.gb/.gbc/.cgb, .nes)
 *              /gbc/saves/<name>.sav       Game Boy battery saves
 *              /gbc/saves/nes/<name>.sav   NES battery saves
 */
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <esp_partition.h>
#include "ff.h"          // FatFs: folder listing with sizes in one pass (see listDirFat)
#include <esp_spi_flash.h>
#include <driver/i2s.h>
#include <soc/spi_periph.h>   // spi_periph_signal: which signal index is this host's data input
#include <soc/spi_reg.h>      // SPI_DMA_OUT_LINK_REG: disarm DMA before a FIFO touch read
#include <esp_rom_gpio.h>     // esp_rom_gpio_connect_in_signal: point that input at a pin
#include <vector>
#include <algorithm>
#include <stdarg.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "gnuboy/gb_glue.h"
#include "nofrendo/nes_glue.h"

#if __has_include(<Bluepad32.h>)
#include <Bluepad32.h>
#define HAVE_BT 1
#else
#define HAVE_BT 0   // plain Arduino-ESP32 build: no Bluetooth controller support
#endif

// ----------------------------------------------------------------------------
// Which board. Default: ESP32-2432S024C (2.4", capacitive CST820 touch). For another
// board define ONE of these (uncomment it here, or with arduino-cli add
//   --build-property compiler.cpp.extra_flags=-DBOARD_xxx):
//   BOARD_024R    2.4" ESP32-2432S024R: resistive XPT2046 sharing the screen's SPI bus
//   BOARD_028R    2.8" ESP32-2432S028R (the original CYD, one USB port): resistive XPT2046
//                 on its own pins
//   BOARD_CYD2USB 2.8" with two USB ports: as BOARD_028R, but most have an ST7789 screen
//                 whose colors come out inverted
//   BOARD_028C    2.8" ESP32-2432S028C: capacitive touch (CST820 or GT911, found at start)
//   BOARD_032R    3.2" ESP32-2432S032R: ST7789 screen, resistive XPT2046 on the screen's bus
//   BOARD_032C    3.2" ESP32-2432S032C: ST7789 screen, capacitive GT911
//   BOARD_035R    3.5" ESP32-3248S035R: 320x480 ST7796 screen, resistive XPT2046 on its bus
//   BOARD_035C    3.5" ESP32-3248S035C: 320x480 ST7796 screen, capacitive GT911
//   BOARD_W328C   2.8" JC2432W328C: ST7789 IPS screen, capacitive CST820 (wired like the 2.4" C)
//   BOARD_024N    2.4" ESP32-2432S024N: no touch screen (Bluetooth controller only)
//   BOARD_032N    3.2" ESP32-2432S032N: ST7789 screen, no touch (Bluetooth controller only)
//   BOARD_028PLUS 2.8" ESP32-2432S028R Plus (ESP32-WROOM-32E, USB-C, sold in NerdMiner kits):
//                 as the 2.8" R, but an ST7789 screen without inversion, the RGB LED on
//                 17 / 22 / 16, and the speaker amplifier switched on by GPIO4
//   BOARD_E32R28T 2.8" LCDWiki ESP32-32E (E32R28T, ESP32-WROOM-32E, USB-C, "ZLX"): as the
//                 2.8" R (ILI9341, XPT2046 on its own pins), but the RGB LED on 22 / 16 / 17
//                 and the speaker amplifier switched on by GPIO4 LOW (lcdwiki.com pin table)
//   BOARD_CROW    Elecrow CrowPanel ESP32 2.4" / 2.8" (ESP32-WROOM versions), and the same
//                 board sold as the ESP32 Solo Miner LCD: like the 2.4" R (resistive XPT2046
//                 on the screen's bus, pen interrupt on GPIO36) but no RGB LED. Its
//                 connectors take the pins a separate touch bus would need: GPIO_D has
//                 IO25/IO32, I2C has IO21/IO22, UART1 has IO16/IO17, SPK has IO26.
// All are the original ESP32 (4 MB flash, no PSRAM), same SD card, speaker (GPIO26),
// RGB LED (4/16/17) and BOOT button. All but the 3.5" are 240x320; the 3.5" shows the
// same 240x320 picture in the middle of its screen.
// ----------------------------------------------------------------------------
// #define BOARD_024R
// #define BOARD_028R
// #define BOARD_CYD2USB
// #define BOARD_028C
// #define BOARD_032R
// #define BOARD_032C
// #define BOARD_035R
// #define BOARD_035C
// #define BOARD_W328C
// #define BOARD_024N
// #define BOARD_032N
// #define BOARD_CROW
// #define BOARD_028PLUS
// #define BOARD_E32R28T
#define TOUCH_CST     0   // CST820 on I2C (2.4" C)
#define TOUCH_XPT_OWN 1   // XPT2046 on its own pins, software SPI (2.8" R, CYD2USB)
#define TOUCH_XPT_BUS 2   // XPT2046 sharing the screen's SPI bus (2.4" R, 3.2" R, 3.5" R)
#define TOUCH_I2C_ANY 3   // CST820 or GT911 on I2C, whichever answers (2.8" C, 3.2" C, 3.5" C)
#define TOUCH_NONE    4   // no touch screen ("N" boards): played with a Bluetooth controller
// PANEL_CHOICE: the board ships with an ILI9341 or an ST7789 ("Screen chip" setting).
// PANEL_BIG: 320x480 ST7796 (3.5"). BL_PIN: backlight.
#if defined(BOARD_CYD2USB)
#define BOARD_NAME "ESP32-2432S028-2USB"
#define TOUCH_KIND TOUCH_XPT_OWN
#define PANEL_CHOICE 1
#define DEFAULT_PANEL 1    // ST7789
#define DEFAULT_INVERT 1
#define BL_PIN 21
#elif defined(BOARD_028R)
#define BOARD_NAME "ESP32-2432S028R"
#define TOUCH_KIND TOUCH_XPT_OWN
#define PANEL_CHOICE 1
#define BL_PIN 21
#elif defined(BOARD_028C)
#define BOARD_NAME "ESP32-2432S028C"
#define TOUCH_KIND TOUCH_I2C_ANY
#define PANEL_CHOICE 1
#define BL_PIN 21
#define TP_INT_PIN -1      // GPIO21 is the backlight on this board: touch is polled
#elif defined(BOARD_032R)
#define BOARD_NAME "ESP32-2432S032R"
#define TOUCH_KIND TOUCH_XPT_BUS
#define PANEL_CHOICE 1
#define DEFAULT_PANEL 1    // ST7789 (IPS)
#define DEFAULT_INVERT 1
#define BL_PIN 27
#elif defined(BOARD_032C)
#define BOARD_NAME "ESP32-2432S032C"
#define TOUCH_KIND TOUCH_I2C_ANY
#define PANEL_CHOICE 1
#define DEFAULT_PANEL 1    // ST7789 (IPS)
#define DEFAULT_INVERT 1
#define BL_PIN 27
#define TP_INT_PIN 21
#elif defined(BOARD_035R)
#define BOARD_NAME "ESP32-3248S035R"
#define TOUCH_KIND TOUCH_XPT_BUS
#define PANEL_BIG 1
#define BL_PIN 27
#elif defined(BOARD_035C)
#define BOARD_NAME "ESP32-3248S035C"
#define TOUCH_KIND TOUCH_I2C_ANY
#define PANEL_BIG 1
#define BL_PIN 27
#define TP_INT_PIN 21
#elif defined(BOARD_W328C)
#define BOARD_NAME "JC2432W328C"
#define TOUCH_KIND TOUCH_CST
#define PANEL_CHOICE 1
#define DEFAULT_PANEL 1    // ST7789 (IPS)
#define DEFAULT_INVERT 1
#define BL_PIN 27
#define CAL_AT_START 1     // touch wiring not confirmed on this board: calibrate once
#elif defined(BOARD_024N)
#define BOARD_NAME "ESP32-2432S024N"
#define TOUCH_KIND TOUCH_NONE
#define BL_PIN 27
#elif defined(BOARD_032N)
#define BOARD_NAME "ESP32-2432S032N"
#define TOUCH_KIND TOUCH_NONE
#define PANEL_CHOICE 1
#define DEFAULT_PANEL 1    // ST7789 (IPS)
#define DEFAULT_INVERT 1
#define BL_PIN 27
#elif defined(BOARD_028PLUS)
#define BOARD_NAME "ESP32-2432S028R-Plus"
#define TOUCH_KIND TOUCH_XPT_OWN
#define PANEL_CHOICE 1
#define DEFAULT_PANEL 1    // ST7789, BGR, no inversion (from the maker's pin guide)
#define DEFAULT_INVERT 0
#define BL_PIN 21
#define LED_PINS 17, 22, 16   // red, green, blue
#define AUDIO_EN_PIN 4     // speaker amplifier enable (assumed on when high)
#elif defined(BOARD_E32R28T)
#define BOARD_NAME "E32R28T"
#define TOUCH_KIND TOUCH_XPT_OWN   // XPT2046 on 25 / 32 / 39, CS 33, IRQ 36
#define PANEL_CHOICE 1
#define DEFAULT_PANEL 1    // ST7789: driven as an ILI9341 (as the wiki says) the picture came
#define DEFAULT_INVERT 0   // out mirrored and short of one edge on a NerdMiner V2 unit
#define BL_PIN 21
#define LED_PINS 22, 16, 17   // red, green, blue (common anode, lit when low)
#define AUDIO_EN_PIN 4
#define AUDIO_EN_LEVEL LOW // this board's amplifier is enabled by a low level
#elif defined(BOARD_CROW)
#define BOARD_NAME "CrowPanel-ESP32-2.4-2.8"
#define TOUCH_KIND TOUCH_XPT_BUS
#define BL_PIN 27
// The touch chip's pen interrupt is on GPIO36, measured on a Solo Miner board: it sits
// high and goes low while the panel is pressed. This was -1 (polled) on the guess that
// the line was not brought out, and polled is not the same thing - the driver then has
// to decide from pressure alone whether a press is real, and on this board it never did.
#define TP_INT_PIN 36
#define TOUCH_SPI_HZ 600000   // see TOUCH_SPI_HZ below: this panel will not read faster
#define HAS_LED 0          // no RGB LED; GPIO16/17 go to the UART1 connector
// Which pin carries the touch chip's answer. On the CYD and on Elecrow's own CrowPanel
// 2.4 it is the screen's MISO, GPIO12. Not on the Solo Miner. Its Eagle schematic puts
// the XPT2046's DOUT on GPIO4 - net IO4_TFT_SDO/TP_OUT, with U3.DOUT the only thing on
// it - and leaves GPIO12 on a net called plain IO12 that reaches nothing but the pad of
// an unfitted resistor.
//
// That single pin accounts for every reading this board has given us. Driving GPIO12
// high and low read back high and low, which we read as "the chip is not driving it"
// when it meant "nothing is attached to it". All eight chip selects returned zeros
// because the chip select was never the problem: the answer was arriving on a pin we
// were not reading. And the pen interrupt kept working throughout, because the chip was
// powered and sensing the whole time.
//
// Elecrow's own example configs contradict each other here - one ESPHome file says
// miso_pin 33, which is the chip select, another says 12, and neither of them enables
// touch at all, so nobody ever found out. The netlist generated from the PCB is the
// authority, and the screen has no SDO pin on its connector, so this bus's MISO is only
// ever used by the touch chip.
#define PIN_LCD_MISO 4
#elif defined(BOARD_024R)
#define BOARD_NAME "ESP32-2432S024R"
#define TOUCH_KIND TOUCH_XPT_BUS
#define BL_PIN 27
#else
#define BOARD_NAME "ESP32-2432S024C"
#define TOUCH_KIND TOUCH_CST
#define BL_PIN 27
#endif
#ifndef PANEL_CHOICE
#define PANEL_CHOICE 0
#endif
#ifndef PANEL_BIG
#define PANEL_BIG 0
#endif
#ifndef CAL_AT_START
#define CAL_AT_START 0
#endif
#ifndef HAS_LED
#define HAS_LED 1          // RGB LED on GPIO 4 / 16 / 17
#endif
#if TOUCH_KIND == TOUCH_NONE && !HAVE_BT
#error "Boards with no touch screen need the Bluetooth build"
#endif
#define TOUCH_RESISTIVE (TOUCH_KIND == TOUCH_XPT_OWN || TOUCH_KIND == TOUCH_XPT_BUS)
// The screen's SPI DMA channel, 0 for none. Every board gets one, including those whose
// touch chip shares the bus - see busDmaIdle() for what that costs and how it is paid.
#define BUS_DMA_CH 1
// Ask for a touch calibration at the first start (where the touch layer isn't known to
// line up with the screen out of the box)
#define TOUCH_ASK_CAL (TOUCH_RESISTIVE || TOUCH_KIND == TOUCH_I2C_ANY || CAL_AT_START)
#ifndef DEFAULT_PANEL
#define DEFAULT_PANEL 0    // ILI9341
#define DEFAULT_INVERT 0
#endif

// Default orientation. LovyanGFX rotation 2 = portrait, confirmed correct on the
// ESP32-2432S024C (touch lines up with no transform); 3 = Landscape L (board
// turned counter-clockwise). Landscape needs a controller, so it's BT-only.
#if HAVE_BT && (TOUCH_KIND == TOUCH_CST || TOUCH_KIND == TOUCH_NONE)
#define DEFAULT_ROTATION 3
#else   // the other boards start in portrait so they can be played by touch right away
#define DEFAULT_ROTATION 2
#endif

// ----------------------------------------------------------------------------
// Board pins. Display, SD card, speaker (GPIO26), RGB LED (4/16/17) and BOOT are the same
// on every board. The backlight is on 27 (2.4", 3.2", 3.5") or 21 (2.8").
// ----------------------------------------------------------------------------
#define PIN_LCD_SCLK 14
#define PIN_LCD_MOSI 13
#ifndef PIN_LCD_MISO
#define PIN_LCD_MISO 12
#endif
#define PIN_LCD_DC    2
#define PIN_LCD_CS   15
#define PIN_LCD_BL   BL_PIN
#if TOUCH_KIND == TOUCH_XPT_OWN
#define PIN_TP_SCLK  25
#define PIN_TP_MOSI  32
#define PIN_TP_MISO  39
#define PIN_TP_CS    33
#define PIN_TP_INT   36
#elif TOUCH_KIND == TOUCH_XPT_BUS
#define PIN_TP_CS    33   // on the screen's bus (14 / 13 / 12)
// How fast to clock the touch chip. 2.5 MHz is the usual figure for an XPT2046 and is
// what the other boards use, but Elecrow's own setup for these panels clocks it at
// 600 kHz, and NMMiner - the firmware these boards ship with, whose touch works - had
// to put out a release fixing its touch driver for exactly the 2.4 and 2.8 inch Elecrow
// boards. A resistive panel needs time to settle, and this one evidently needs more
// than most: too fast and every sample is noise, which the driver quietly discards,
// leaving a touch chip that interrupts correctly and reads nothing.
#ifndef TOUCH_SPI_HZ
#define TOUCH_SPI_HZ 2500000
#endif
#ifdef TP_INT_PIN
#define PIN_TP_INT   TP_INT_PIN
#else
#define PIN_TP_INT   36
#endif
#elif TOUCH_KIND == TOUCH_I2C_ANY
#define PIN_TP_SDA   33
#define PIN_TP_SCL   32
#define PIN_TP_RST   25
#define PIN_TP_INT   TP_INT_PIN
#else
#define PIN_TP_SDA   33
#define PIN_TP_SCL   32
#define PIN_TP_RST   25
#define PIN_TP_INT   21
#endif
#define PIN_SD_SCK   18
#define PIN_SD_MISO  19
#define PIN_SD_MOSI  23
#define PIN_SD_CS     5
#define PIN_BOOT_BTN  0
// Speaker: GPIO26 = internal DAC channel 2 (driven through I2S0 built-in DAC)

#define APP_VERSION "2.31.1"
#define XFER_BAUD 921600   // USB serial: boot log and game transfers
#define APP_NAME "H@N-CYD-GBC"
#define AUDIO_RATE  22050
// Game volume steps, 0 = mute. Four was too coarse to find a comfortable level on a
// small speaker: the gap between 1 and 2 was most of the usable range.
#define VOL_MAX 10
// Sound waiting to be played, in 128-sample buffers (8 = 46 ms). Raising this hides
// stutters, but the RAM comes out of the pages a big ROM keeps on hand while streaming
// from the SD card, and 4 MB games slowed badly for it. Fix the stalls instead.
#define AUDIO_DMA_BUFS 8
#define FRAME_US    16743      // Game Boy: 59.73 Hz
#define NES_FRAME_US     16639 // NES NTSC: 60.1 Hz
#define NES_FRAME_US_PAL 20007 // NES PAL: 50 Hz

// Screen size follows the orientation (240x320 portrait, 320x240 landscape)
static int SCR_W = 240;
static int SCR_H = 320;
#define GAME_H  216            // portrait game area above the touch controls (144 * 1.5)
#define CTRL_Y  GAME_H

// ----------------------------------------------------------------------------
// Display
// ----------------------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
public:
#if PANEL_BIG
  lgfx::Panel_ST7796 panel;    // 3.5": 320x480, the game and menus use a 240x320 window in the middle
#else
  lgfx::Panel_ILI9341 panel;
#endif
#if PANEL_CHOICE
  lgfx::Panel_ST7789 panel2;   // 2.8" boards: many (all "CYD2USB") use this chip instead; 3.2" all do
#endif
  lgfx::Bus_SPI bus;
  lgfx::Light_PWM light;
#if TOUCH_RESISTIVE
  lgfx::Touch_XPT2046 touch;
#elif TOUCH_KIND != TOUCH_NONE
  lgfx::Touch_CST816S touch;   // CST820
#endif
#if TOUCH_KIND == TOUCH_I2C_ANY
  lgfx::Touch_GT911 touchGt;   // 3.2" / 3.5" C, and some 2.8" C
#endif

  LGFX() {
    {
      auto c = bus.config();
      c.spi_host = HSPI_HOST;
      c.spi_mode = 0;
      c.freq_write = 40000000;
      c.freq_read = 16000000;
      c.spi_3wire = false;
      c.use_lock = true;
      // Every board draws with DMA, shared touch bus or not. Where the touch chip is on
      // this bus, busDmaIdle() disarms the outgoing DMA link before each touch read so
      // the chip still gets its command bytes from the FIFO.
      c.dma_channel = BUS_DMA_CH;
      c.pin_sclk = PIN_LCD_SCLK;
      c.pin_mosi = PIN_LCD_MOSI;
      c.pin_miso = PIN_LCD_MISO;
      c.pin_dc = PIN_LCD_DC;
      bus.config(c);
    }
    auto setupPanel = [&](lgfx::Panel_Device &p) {
      p.setBus(&bus);
      auto c = p.config();
      c.pin_cs = PIN_LCD_CS;
      c.pin_rst = -1;
      c.pin_busy = -1;
      c.panel_width = 240;
      c.panel_height = 320;
      c.offset_x = 0;
      c.offset_y = 0;
      c.offset_rotation = 0;
      c.readable = false;
      c.invert = false;
      c.rgb_order = false;
      c.dlen_16bit = false;
      // True only where something else really is on the screen's SPI bus: on these
      // boards the XPT2046 touch chip is. LovyanGFX uses this to decide whether it may
      // hold the bus and cache its register setup between draws; told it owns the bus
      // outright, the touch chip's own transaction never gets a clean hand-off and its
      // readings come back empty. The boards with touch on its own pins, or on I2C, do
      // own the bus and are better off keeping it.
      c.bus_shared = (TOUCH_KIND == TOUCH_XPT_BUS);
#if PANEL_BIG
      c.memory_width = 320;      // centered 240x320 window
      c.memory_height = 480;
      c.offset_x = (320 - 240) / 2;
      c.offset_y = (480 - 320) / 2;
#endif
      p.config(c);
      p.setLight(&light);
#if TOUCH_KIND != TOUCH_NONE
      p.setTouch(&touch);
#endif
    };
    {
      auto c = light.config();
      c.pin_bl = PIN_LCD_BL;
      c.invert = false;
      // Above hearing, deliberately. 12 kHz sat right in the audible range, and the
      // backlight draws its current in pulses at this frequency from the same 3.3 V rail
      // that feeds the speaker amplifier - whose shutdown pin is tied to ground on these
      // boards, so it is always on and always amplifying whatever reaches it. 40 kHz
      // moves the whine past anyone's hearing and still leaves about 10 bits of
      // brightness resolution at the ESP32's 80 MHz LEDC clock.
      c.freq = 40000;
      c.pwm_channel = 7;
      light.config(c);
    }
#if TOUCH_RESISTIVE
    {
      // raw readings are 0..4095; Setup's touch calibration maps them to the screen
      auto c = touch.config();
      c.x_min = 0; c.x_max = 4095;
      c.y_min = 0; c.y_max = 4095;
      c.pin_int = PIN_TP_INT;
      c.offset_rotation = 0;
#if TOUCH_KIND == TOUCH_XPT_OWN
      c.bus_shared = false;
      c.spi_host = -1;          // software SPI on its own pins
      c.freq = 1000000;
      c.pin_sclk = PIN_TP_SCLK;
      c.pin_mosi = PIN_TP_MOSI;
      c.pin_miso = PIN_TP_MISO;
#else
      c.bus_shared = true;      // on the screen's bus: reads pause the screen transfer
      c.spi_host = HSPI_HOST;
      c.freq = TOUCH_SPI_HZ;
      c.pin_sclk = PIN_LCD_SCLK;
      c.pin_mosi = PIN_LCD_MOSI;
      c.pin_miso = PIN_LCD_MISO;
#endif
      c.pin_cs = PIN_TP_CS;
      touch.config(c);
    }
#elif TOUCH_KIND != TOUCH_NONE
    {
      auto c = touch.config();
      c.x_min = 0; c.x_max = 239;
      c.y_min = 0; c.y_max = 319;
      c.pin_int = PIN_TP_INT;
      c.pin_rst = PIN_TP_RST;
      c.bus_shared = false;
      c.offset_rotation = 0;
      c.i2c_port = 1;
      c.i2c_addr = 0x15;
      c.pin_sda = PIN_TP_SDA;
      c.pin_scl = PIN_TP_SCL;
      c.freq = 400000;
      touch.config(c);
    }
#endif
#if TOUCH_KIND == TOUCH_I2C_ANY
    {
      auto c = touchGt.config();
      c.x_min = 0; c.x_max = PANEL_BIG ? 319 : 239;
      c.y_min = 0; c.y_max = PANEL_BIG ? 479 : 319;
      c.pin_int = PIN_TP_INT;
      c.pin_rst = PIN_TP_RST;
      c.bus_shared = false;
      c.offset_rotation = 0;
      c.i2c_port = 1;
      c.i2c_addr = 0x5D;        // the driver also tries 0x14
      c.pin_sda = PIN_TP_SDA;
      c.pin_scl = PIN_TP_SCL;
      c.freq = 400000;
      touchGt.config(c);
    }
#endif
    setupPanel(panel);
#if PANEL_CHOICE
    setupPanel(panel2);
#endif
    setPanel(&panel);
  }
  // 0 = ILI9341, 1 = ST7789 (2.8" / 3.2" boards only); takes effect at the next init()
  void selectPanel(int t) {
#if PANEL_CHOICE
    if (t) { setPanel(&panel2); return; }
#endif
    setPanel(&panel);
  }
  void setRgbOrder(bool rgb) {
    auto c = panel.config(); c.rgb_order = rgb; panel.config(c);
#if PANEL_CHOICE
    auto d = panel2.config(); d.rgb_order = rgb; panel2.config(d);
#endif
  }
  // Capacitive boards other than the 2.4" C: use the GT911 if one answers, else the
  // CST820. Returns the chip name.
  const char *detectTouch() {
#if TOUCH_KIND == TOUCH_I2C_ANY
    if (touchGt.init()) {
      panel.setTouch(&touchGt);
#if PANEL_CHOICE
      panel2.setTouch(&touchGt);
#endif
      return "GT911";
    }
    return "CST820";
#else
    return "";
#endif
  }
  // 3.5": black out the whole 320x480 screen (the edges around the 240x320 window
  // otherwise show whatever was there at power-on). Call after init().
  void clearWholeScreen() {
#if PANEL_BIG
    auto keep = panel.config();
    auto c = keep;
    c.panel_width = 320; c.panel_height = 480; c.offset_x = 0; c.offset_y = 0;
    panel.config(c);
    setRotation(0);
    fillScreen(0);
    panel.config(keep);
    setRotation(0);
#endif
  }
};

static LGFX lcd;
static SemaphoreHandle_t lcdMutex;

// ----------------------------------------------------------------------------
// Settings (NVS)
// ----------------------------------------------------------------------------
struct Settings {
  uint8_t volume = 2;      // 0..4 (0 = mute)
  uint8_t scale = 0;       // portrait: 0 = 1.5x sharp, 1 = 1.5x smooth, 2 = 1x, 3 = fit, 4 = stretch
  uint8_t scaleLand = 4;   // landscape scaling, default Stretch
  uint8_t showFps = 0;
  uint8_t rotation = DEFAULT_ROTATION;   // LovyanGFX rotation 0..3
  uint8_t touchPad = 0;    // on-screen controls: 0 = auto (hide with a controller), 1 = always, 2 = never
  uint8_t padLand = 2;     // landscape pad: 0 = off, 1 = touch zones only, 2 = drawn outlines
  uint8_t mirrorFix = 0;   // drop the X mirror if the image is mirrored
  uint8_t invert = DEFAULT_INVERT;
  uint8_t panelType = DEFAULT_PANEL;   // screen chip on 2.8" / 3.2" boards: 0 = ILI9341, 1 = ST7789
  uint8_t swapRB = 0;
  uint8_t touchFlipX = 0;
  uint8_t touchFlipY = 0;
  // touch calibration: raw -> panel coords (before orientation)
  uint8_t btEnabled = 1;
  uint8_t btLayout = 0;    // 0 = positional (east = A), 1 = Xbox style (south = A)
  uint8_t calValid = 0;
  uint8_t calSwap = 0;
  float calAx = 1, calBx = 0, calAy = 1, calBy = 0;
  uint8_t dmgPalette = 0;
  uint8_t brightness = 255;
  uint8_t nesPalette = 0;  // see NES_PALETTE_NAMES
  uint8_t theme = 0;       // menu colors, see THEMES
  // board RGB LED
  uint8_t ledMode = 2;     // 0 = off, 1 = solid, 2 = breathing
  uint8_t ledColor = 0;    // see LED_COLORS (0 = theme)
  uint8_t ledBright = 1;   // 0..3 = 25 / 50 / 75 / 100 %
  uint8_t ledGame = 1;     // while playing: 0 = same as menus, 1 = steady, 2 = off
  uint8_t ledFlashSave = 1;
  uint8_t ledFlashCheat = 1;
  // menu music (its own volume, separate from the games)
  uint8_t musicOn = 1;
  uint8_t musicTune = 0;   // see TUNES
  uint8_t idleMin = 5;     // screensaver after this many idle minutes (0 = never)
  uint8_t musicVol = 1;    // 1..4
};
static Settings cfg;
static Preferences prefs;

static void loadSettings() {
  prefs.begin("gbc", true);
  cfg.volume = prefs.getUChar("vol", cfg.volume);
  if (cfg.volume && cfg.volume <= 4 && !prefs.getUChar("vol10", 0)) cfg.volume = cfg.volume * VOL_MAX / 4;
  if (cfg.volume > VOL_MAX) cfg.volume = VOL_MAX;
  cfg.scale = prefs.getUChar("scale", cfg.scale);
  cfg.scaleLand = prefs.getUChar("scaleL", cfg.scaleLand);
  cfg.showFps = prefs.getUChar("fps", cfg.showFps);
  cfg.rotation = prefs.getUChar("lgrot", cfg.rotation) & 3;
  cfg.touchPad = prefs.getUChar("tpad", cfg.touchPad);
  cfg.padLand = prefs.getUChar("tpadL", cfg.padLand);
#if TOUCH_KIND == TOUCH_NONE
  cfg.touchPad = 2;    // no touch screen: never draw the on-screen buttons
#endif
  cfg.mirrorFix = prefs.getUChar("mirr", cfg.mirrorFix);
  cfg.invert = prefs.getUChar("inv", cfg.invert);
  cfg.panelType = prefs.getUChar("pnl", cfg.panelType) & 1;
  cfg.swapRB = prefs.getUChar("rgb", cfg.swapRB);
  cfg.touchFlipX = prefs.getUChar("tfx", cfg.touchFlipX);
  cfg.touchFlipY = prefs.getUChar("tfy", cfg.touchFlipY);
  cfg.btEnabled = prefs.getUChar("bt", cfg.btEnabled);
#if TOUCH_KIND == TOUCH_NONE
  cfg.btEnabled = 1;   // the only way to play: never let it be switched off
#endif
  cfg.btLayout = prefs.getUChar("btlay", cfg.btLayout);
  cfg.calValid = prefs.getUChar("calok", 0);
  cfg.calSwap = prefs.getUChar("calsw", 0);
  cfg.calAx = prefs.getFloat("calax", 1);
  cfg.calBx = prefs.getFloat("calbx", 0);
  cfg.calAy = prefs.getFloat("calay", 1);
  cfg.calBy = prefs.getFloat("calby", 0);
  cfg.dmgPalette = prefs.getUChar("pal", cfg.dmgPalette);
  cfg.brightness = prefs.getUChar("bri", cfg.brightness);
  cfg.nesPalette = prefs.getUChar("npal", cfg.nesPalette) % NES_NUM_PALETTES;
  cfg.theme = prefs.getUChar("thm", cfg.theme);
  cfg.ledMode = prefs.getUChar("ledm", cfg.ledMode);
  cfg.ledColor = prefs.getUChar("ledc", cfg.ledColor);
  cfg.ledBright = prefs.getUChar("ledb", cfg.ledBright);
  cfg.ledGame = prefs.getUChar("ledg", cfg.ledGame);
  cfg.ledFlashSave = prefs.getUChar("ledfs", cfg.ledFlashSave);
  cfg.ledFlashCheat = prefs.getUChar("ledfc", cfg.ledFlashCheat);
  cfg.musicOn = prefs.getUChar("mus", cfg.musicOn);
  cfg.musicTune = prefs.getUChar("mtun", cfg.musicTune);
  cfg.idleMin = prefs.getUChar("idle", cfg.idleMin);
  cfg.musicVol = constrain(prefs.getUChar("mvol", cfg.musicVol), 1, 4);

  prefs.end();
}

static void saveSettings() {
  prefs.begin("gbc", false);
  prefs.putUChar("vol", cfg.volume);
  prefs.putUChar("vol10", 1);   // this value is on the 0..VOL_MAX scale
  prefs.putUChar("scale", cfg.scale);
  prefs.putUChar("scaleL", cfg.scaleLand);
  prefs.putUChar("fps", cfg.showFps);
  prefs.putUChar("lgrot", cfg.rotation);
  prefs.putUChar("tpad", cfg.touchPad);
  prefs.putUChar("tpadL", cfg.padLand);
  prefs.putUChar("mirr", cfg.mirrorFix);
  prefs.putUChar("inv", cfg.invert);
  prefs.putUChar("pnl", cfg.panelType);
  prefs.putUChar("rgb", cfg.swapRB);
  prefs.putUChar("tfx", cfg.touchFlipX);
  prefs.putUChar("tfy", cfg.touchFlipY);
  prefs.putUChar("bt", cfg.btEnabled);
  prefs.putUChar("btlay", cfg.btLayout);
  prefs.putUChar("calok", cfg.calValid);
  prefs.putUChar("calsw", cfg.calSwap);
  prefs.putFloat("calax", cfg.calAx);
  prefs.putFloat("calbx", cfg.calBx);
  prefs.putFloat("calay", cfg.calAy);
  prefs.putFloat("calby", cfg.calBy);
  prefs.putUChar("pal", cfg.dmgPalette);
  prefs.putUChar("bri", cfg.brightness);
  prefs.putUChar("npal", cfg.nesPalette);
  prefs.putUChar("thm", cfg.theme);
  prefs.putUChar("ledm", cfg.ledMode);
  prefs.putUChar("ledc", cfg.ledColor);
  prefs.putUChar("ledb", cfg.ledBright);
  prefs.putUChar("ledg", cfg.ledGame);
  prefs.putUChar("ledfs", cfg.ledFlashSave);
  prefs.putUChar("ledfc", cfg.ledFlashCheat);
  prefs.putUChar("mus", cfg.musicOn);
  prefs.putUChar("mtun", cfg.musicTune);
  prefs.putUChar("idle", cfg.idleMin);
  prefs.putUChar("mvol", cfg.musicVol);

  prefs.end();
}

// Scaling is remembered separately for portrait and landscape
static uint8_t &curScale() { return (cfg.rotation & 1) ? cfg.scaleLand : cfg.scale; }

static void applyBacklight(uint8_t v);   // defined below

static void applyDisplaySettings(bool reinit) {
  if (reinit) {
    lcd.selectPanel(cfg.panelType);
    lcd.setRgbOrder(cfg.swapRB);
    lcd.init();
    lcd.clearWholeScreen();
  }
#if !HAVE_BT
  if (cfg.rotation & 1) cfg.rotation = 2;   // landscape needs a controller
#endif
  // "Mirror fix" uses LovyanGFX's mirrored rotations (4..7) for odd panels.
  lcd.setRotation((cfg.rotation & 3) | (cfg.mirrorFix ? 4 : 0));
  SCR_W = lcd.width();
  SCR_H = lcd.height();
  lcd.invertDisplay(cfg.invert);
  applyBacklight(cfg.brightness);
}

// Backlight, with the chopping switched off entirely at full brightness.
//
// The backlight is an N-MOSFET across the LED string, so every PWM cycle switches the
// whole backlight current on and off. That current comes through the board's buck
// converter, and modulating its load is what the speaker amplifier hears - the amp's
// shutdown pin is tied to ground on these boards, so it is always on and always
// amplifying whatever lands on the 3.3 V rail.
//
// Raising the PWM to 40 kHz in 2.31.0 moved that modulation above hearing. But at full
// brightness LovyanGFX still asks for duty 510 of 512, not 512, so the current is still
// being chopped - a narrow off-pulse, 40,000 times a second, for no light gained. At
// maximum the pin is driven high as a plain output instead and the switching stops
// altogether; any lower setting needs PWM and gets it back.
static void applyBacklight(uint8_t v) {
  static bool pwmOn = true;
  if (v >= 255) {
    if (pwmOn) { ledcDetachPin(PIN_LCD_BL); pwmOn = false; }
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);
    return;
  }
  if (!pwmOn) { ledcAttachPin(PIN_LCD_BL, 7); pwmOn = true; }   // channel 7, as configured
  lcd.setBrightness(v);
}

// DMG palettes, 0xBBGGRR
static const int DMG_PALETTES[][4] = {
  { 0xd5f3ef, 0x7ab6a3, 0x3b6137, 0x161c04 },  // GB green
  { 0xffffff, 0xaaaaaa, 0x555555, 0x000000 },  // Gray
  { 0xc4cfa1, 0x8b956d, 0x4d533c, 0x1f1f1f },  // Pocket
  { 0xd0f8e0, 0x70c088, 0x566834, 0x201808 },  // Warm
  { 0xffffff, 0x63a5ff, 0x0000ff, 0x000000 },  // Red (GBC "left+A")
};
static const char *DMG_PALETTE_NAMES[] = { "GB green", "Gray", "Pocket", "Warm", "Red" };
#define NUM_DMG_PALETTES 5


// ----------------------------------------------------------------------------
// Bluetooth controllers (Bluepad32): PS4, PS5 (DualSense), Switch Pro / Joy-Con, 8BitDo
// (classic Bluetooth); Xbox Wireless, Stadia, 8BitDo and Android pads (BLE)
// ----------------------------------------------------------------------------
#define GBP_RIGHT  0x01
#define GBP_LEFT   0x02
#define GBP_UP     0x04
#define GBP_DOWN   0x08
#define GBP_A      0x10
#define GBP_B      0x20
#define GBP_SELECT 0x40
#define GBP_START  0x80

static bool btPairing = false;   // pairing screen open: keep scanning
static bool btReady = false;     // Bluepad32 started (skipped at boot when Bluetooth is off)
#if HAVE_BT
static ControllerPtr btCtl[BP32_MAX_GAMEPADS];

static int btCount() {
  int n = 0;
  for (auto c : btCtl) if (c && c->isConnected()) n++;
  return n;
}

// Scanning lets new controllers pair and BLE pads (Xbox, Stadia) reconnect.
// Stop it while a controller is connected to keep the radio/CPU quiet.
static void btUpdateScanning() {
  if (!btReady) return;
  bool want = cfg.btEnabled && (btPairing || btCount() == 0);
  BP32.enableNewBluetoothConnections(want);
}

static void onBtConnected(ControllerPtr c) {
  for (auto &slot : btCtl) if (!slot) { slot = c; break; }
  Serial.printf("BT controller connected: %s\n", c->getModelName().c_str());
  c->setColorLED(40, 80, 255);
  btUpdateScanning();
}

static void onBtDisconnected(ControllerPtr c) {
  for (auto &slot : btCtl) if (slot == c) slot = nullptr;
  Serial.println("BT controller disconnected");
  btUpdateScanning();
}

static void btInit() {
  if (btReady) return;
  BP32.setup(&onBtConnected, &onBtDisconnected);
  btReady = true;
  BP32.enableVirtualDevice(false);
  btUpdateScanning();
}

static inline void btPoll() { if (btReady) BP32.update(); }

// Returns Game Boy pad bits; *system = PS / Xbox / Home button held
static uint8_t btReadPad(bool *system) {
  uint8_t pad = 0;
  bool sys = false;
  for (auto c : btCtl) {
    // note: don't require hasData() - it is only true when a NEW report arrived since the
    // last BP32.update(); the last reported state stays valid in between (fixes "turbo")
    if (!c || !c->isConnected() || !c->isGamepad()) continue;
    uint8_t d = c->dpad();
    if (d & DPAD_UP) pad |= GBP_UP;
    if (d & DPAD_DOWN) pad |= GBP_DOWN;
    if (d & DPAD_LEFT) pad |= GBP_LEFT;
    if (d & DPAD_RIGHT) pad |= GBP_RIGHT;
    const int dz = 200;
    if (c->axisX() < -dz) pad |= GBP_LEFT;
    if (c->axisX() > dz) pad |= GBP_RIGHT;
    if (c->axisY() < -dz) pad |= GBP_UP;
    if (c->axisY() > dz) pad |= GBP_DOWN;
    // Bluepad32 reports Xbox naming: A = south, B = east, X = west, Y = north
    if (cfg.btLayout == 0) {   // positional, like a real Game Boy: right = A
      if (c->b() || c->y()) pad |= GBP_A;
      if (c->a() || c->x()) pad |= GBP_B;
    } else {                   // Xbox style: bottom button = A
      if (c->a() || c->y()) pad |= GBP_A;
      if (c->b() || c->x()) pad |= GBP_B;
    }
    if (c->miscStart()) pad |= GBP_START;
    if (c->miscSelect()) pad |= GBP_SELECT;
    if (c->miscSystem()) sys = true;
  }
  if (system) *system = sys;
  return pad;
}

static String btStatus() {
  String s;
  for (auto c : btCtl) {
    if (!c || !c->isConnected()) continue;
    if (s.length()) s += ", ";
    s += c->getModelName();
  }
  return s;
}

static void btForgetAll() {
  if (!btReady) btInit();
  for (auto c : btCtl) if (c && c->isConnected()) c->disconnect();
  BP32.forgetBluetoothKeys();
}
#else
static int btCount() { return 0; }
static void btUpdateScanning() {}
static void btInit() {}
static inline void btPoll() {}
static uint8_t btReadPad(bool *system) { if (system) *system = false; return 0; }
static String btStatus() { return String(); }
static void btForgetAll() {}
#endif

// Controller menu navigation: edge-triggered events with auto-repeat on the d-pad.
enum NavEvent { NAV_NONE, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK, NAV_SYS, NAV_SELECT };

static uint32_t navRepeatAt = 0;
static bool navFromStart = false;   // the last NAV_OK came from Start (Options), not A
static bool bootBtnDown = false, bootBtnUsed = true;   // BOOT as a menu button (bootNavMode)
static uint32_t bootBtnT0 = 0;
static bool bootRaw();
static bool bootNavMode();

// tap = down, hold 0.6-2 s and let go = OK, hold 2 s = back
static int bootNav() {
  if (!bootNavMode()) { bootBtnDown = false; bootBtnUsed = true; return NAV_NONE; }
  bool down = bootRaw();
  uint32_t now = millis();
  if (down && !bootBtnDown) { bootBtnDown = true; bootBtnUsed = false; bootBtnT0 = now; return NAV_NONE; }
  if (down) {
    if (!bootBtnUsed && now - bootBtnT0 >= 2000) { bootBtnUsed = true; return NAV_BACK; }
    return NAV_NONE;
  }
  if (!bootBtnDown) return NAV_NONE;
  bootBtnDown = false;
  if (bootBtnUsed) return NAV_NONE;
  bootBtnUsed = true;
  return now - bootBtnT0 >= 600 ? NAV_OK : NAV_DOWN;
}

// How long since the last touch, button or controller press. The screensaver waits on
// this; everything that counts as "someone is there" pokes it.
static uint32_t idleAt = 0;
static inline void idlePoke() { idleAt = millis(); }

static int btNav() {
  static uint8_t last = 0xFF;   // 0xFF: ignore whatever is held when a menu opens
  static bool lastSys = true;
  uint32_t &repeatAt = navRepeatAt;
  bool sys;
  uint8_t p = btReadPad(&sys);
  if (last == 0xFF) { last = p; lastSys = sys; return NAV_NONE; }
  uint8_t pressed = p & ~last;
  int ev = NAV_NONE;
  if (sys && !lastSys) ev = NAV_SYS;
  else if (pressed & GBP_UP) ev = NAV_UP;
  else if (pressed & GBP_DOWN) ev = NAV_DOWN;
  else if (pressed & GBP_LEFT) ev = NAV_LEFT;
  else if (pressed & GBP_RIGHT) ev = NAV_RIGHT;
  else if (pressed & (GBP_A | GBP_START)) { ev = NAV_OK; navFromStart = !(pressed & GBP_A); }
  else if (pressed & GBP_B) ev = NAV_BACK;
  else if (pressed & GBP_SELECT) ev = NAV_SELECT;
  uint32_t now = millis();
  if (pressed & (GBP_UP | GBP_DOWN)) repeatAt = now + 380;
  else if ((p & (GBP_UP | GBP_DOWN)) && now >= repeatAt) {   // held: auto-repeat
    ev = (p & GBP_UP) ? NAV_UP : NAV_DOWN;
    repeatAt = now + 90;
  }
  last = p;
  lastSys = sys;
  if (ev == NAV_NONE) { ev = bootNav(); if (ev == NAV_OK) navFromStart = false; }
  return ev;
}

// Call when entering a new screen so a button still held from before isn't seen as a press.
static void btNavReset() {
  btPoll();
  bootBtnDown = bootRaw(); bootBtnUsed = true;   // BOOT still held from before: ignore it
  btNav();   // latches the current state; a held button only counts after release
  navRepeatAt = millis() + 380;
}

// ----------------------------------------------------------------------------
// Touch
// ----------------------------------------------------------------------------
// raw controller coords -> "panel" coords (calibration or flips, no orientation)
static void rawToPanel(int rx, int ry, float &px, float &py) {
  if (cfg.calValid) {
    float ux = cfg.calSwap ? ry : rx, uy = cfg.calSwap ? rx : ry;
    px = cfg.calAx * ux + cfg.calBx;
    py = cfg.calAy * uy + cfg.calBy;
  } else {
#if TOUCH_RESISTIVE
    // uncalibrated: a rough guess at the usual range; the first boot asks for calibration
    px = (rx - 300) * 240.0f / 3500; py = (ry - 300) * 320.0f / 3500;
#else
    px = rx; py = ry;
#endif
    if (cfg.touchFlipX) px = 239 - px;
    if (cfg.touchFlipY) py = 319 - py;
  }
}

// panel coords <-> screen (logical) coords for the current orientation
// (derived from LovyanGFX Panel_Device::convertRawXY; rotation 2 = identity on this board)
static inline void panelToScreen(float px, float py, float &sx, float &sy) {
  switch (cfg.rotation & 3) {
    case 0: sx = 239 - px; sy = 319 - py; break;
    case 1: sx = 319 - py; sy = px;       break;
    case 2: sx = px;       sy = py;       break;
    default: sx = py;      sy = 239 - px; break;
  }
}
static inline void screenToPanel(float sx, float sy, float &px, float &py) {
  switch (cfg.rotation & 3) {
    case 0: px = 239 - sx; py = 319 - sy; break;
    case 1: px = sy;       py = 319 - sx; break;
    case 2: px = sx;       py = sy;       break;
    default: px = 239 - sy; py = sx;      break;
  }
}

// Let the bus take its next command from the FIFO rather than from DMA.
//
// Reading the touch chip means writing the SPI peripheral's own FIFO registers by hand.
// That only works if the outgoing DMA link is not still armed from the last screen
// push - otherwise the peripheral sends whatever the descriptor points at, the chip
// never hears the command, and the read comes back as zeros however the pins are wired.
//
// LovyanGFX does disarm it, but only on the ESP32-S3 and friends: on the original ESP32
// that code is compiled out, and the link stays armed between transfers. So disarm it
// here, which costs two register writes. Nothing is lost by it - Bus_SPI writes the link
// afresh on every DMA transfer - so the screen keeps its DMA and the touch chip keeps
// its FIFO. This is what let the screen's DMA come back in 2.22.0.
static inline void busDmaIdle(void) {
#if TOUCH_KIND == TOUCH_XPT_BUS
  const uint32_t port = HSPI_HOST + 1;
  WRITE_PERI_REG(SPI_DMA_OUT_LINK_REG(port), 0);
  WRITE_PERI_REG(SPI_DMA_CONF_REG(port), 0);
#endif
}

static bool readTouchRaw(int &rx, int &ry) {
#if TOUCH_KIND == TOUCH_NONE
  (void)rx; (void)ry;
  return false;
#else
  lgfx::touch_point_t tp;
#if TOUCH_KIND == TOUCH_XPT_BUS
  // the touch chip shares the screen's bus: wait until the other core isn't drawing
  if (lcdMutex) xSemaphoreTake(lcdMutex, portMAX_DELAY);
  busDmaIdle();
  bool got = lcd.getTouchRaw(&tp, 1);
  if (lcdMutex) xSemaphoreGive(lcdMutex);
  if (!got) return false;
#else
  if (!lcd.getTouchRaw(&tp, 1)) return false;
#endif
  rx = tp.x; ry = tp.y;
  return rx >= 0 && rx < 4096 && ry >= 0 && ry < 4096;
#endif
}

static bool readTouch(int &x, int &y) {
  btPoll();
  int rx, ry;
  if (!readTouchRaw(rx, ry)) return false;
  float px, py, sx, sy;
  rawToPanel(rx, ry, px, py);
  panelToScreen(px, py, sx, sy);
  x = constrain((int)lroundf(sx), 0, SCR_W - 1);
  y = constrain((int)lroundf(sy), 0, SCR_H - 1);
  idlePoke();
  return true;
}

// Following a finger while it drags.
//
// A fingertip on a resistive panel is not a steady contact. Its pressure wavers, the pen
// interrupt chatters with it, and the touch driver discards any reading where fewer than
// three of its seven samples agree. A plain while (readTouch(...)) therefore ends a drag
// at the first flicker - which is exactly why scrolling wanted a stylus, or a slow and
// deliberate finger, and felt broken with a normal one.
//
// So short gaps are ridden out rather than treated as a release, and the position is
// smoothed, because the centre of a fingertip wanders a few pixels even held still.
// Deliberately not done inside readTouch(): the game pad reads through that too, and
// holding a direction 90 ms past the release would make the controls feel sticky.
#define DRAG_GAP_MS 90

struct Drag { float yf; int lastY; uint32_t seen; };

static void dragBegin(Drag &d, int y) { d.yf = (float)y; d.lastY = y; d.seen = millis(); }

// False once the finger is really gone. y comes back smoothed.
static bool dragNext(Drag &d, int &x, int &y) {
  int rx, ry;
  if (readTouch(rx, ry)) {
    d.seen = millis();
    d.lastY = ry;
    x = rx;
  } else {
    if (millis() - d.seen > DRAG_GAP_MS) return false;
    ry = d.lastY;            // a blink, not a lift: carry on from where it was
  }
  d.yf += ((float)ry - d.yf) * 0.45f;
  y = (int)lroundf(d.yf);
  return true;
}

static bool bootRaw() { return digitalRead(PIN_BOOT_BTN) == LOW; }
// Boards with no touch screen: while no controller is connected, BOOT drives the menus
// instead (tap = down, hold = select, hold 2 s = back; see btNav), so its usual jobs
// (pause, folder up, rotate) are off until a controller connects.
// BOOT drives the menus when there is nothing else to drive them with: a board with no
// touch screen, or one whose touch screen does not answer, so cancelling the calibration
// does not leave the board unusable when no controller is paired either.
static bool touchUnusable = false;
static bool bootNavMode() { return (TOUCH_KIND == TOUCH_NONE || touchUnusable) && btCount() == 0; }
static bool bootPressed() { return !bootNavMode() && bootRaw(); }

// Waits for a tap (press + release). Returns false on timeout.
static void bottomHint(const char *label);
static bool waitTap(int &x, int &y, uint32_t timeoutMs = 0) {
  uint32_t t0 = millis();
  bottomHint("Continue");
  int tx, ty;
  while (readTouch(tx, ty)) delay(10);
  while (true) {
    if (readTouch(tx, ty)) {
      x = tx; y = ty;
      while (readTouch(tx, ty)) delay(10);
      return true;
    }
    int ev = btNav();
    if (ev == NAV_SYS || ev == NAV_OK || ev == NAV_BACK) { x = -1; y = ev; return true; }  // controller
    if (timeoutMs && millis() - t0 > timeoutMs) return false;
    delay(15);
  }
}

// ----------------------------------------------------------------------------
// UI helpers
// ----------------------------------------------------------------------------
#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// ---- menu themes ----
// Each theme is 10 colors (0xRRGGBB). Only the menus use them, never the games.
// H@N Night is taken from the H@N logo: night sky, gunmetal steel, icy highlight, bronze.
struct ThemeDef {
  const char *name;
  uint32_t bg, panel, hilite, accent, bronze, text, dim;
  uint32_t hdrTop, hdrA, hdrB;   // header bar: top edge, gradient top, gradient bottom
};
static const ThemeDef THEMES[] = {
  { "H@N Night", 0x0E1622, 0x1D2A38, 0x34495E, 0x86A8C4, 0x9A8462, 0xE4ECF2, 0x8494A5, 0xC8D6E2, 0x6A7E92, 0x263444 },
  { "Neon",      0x0A0618, 0x1A1030, 0x3A185A, 0xFF3CC8, 0x2CE6FF, 0xF4ECFF, 0x9A88B8, 0xFFB0EE, 0x5A2A7A, 0x1E0E34 },
  { "DMG Green", 0x0F380F, 0x204A20, 0x306230, 0x9BBC0F, 0xB8C858, 0xC8E08C, 0x7A9A40, 0xD0F0A0, 0x4A7A2A, 0x1A3A14 },
  { "Amber CRT", 0x140A02, 0x261606, 0x4A2A08, 0xFFA830, 0xC07010, 0xFFD898, 0xA88050, 0xFFE0B0, 0x7A4A14, 0x2A1606 },
};
#define NUM_THEMES (int)(sizeof THEMES / sizeof THEMES[0])
struct ThemeColors { uint16_t bg, panel, hilite, accent, bronze, text, dim; };
static ThemeColors tc;
static inline uint16_t hex565(uint32_t c) { return RGB565(c >> 16, (c >> 8) & 0xFF, c & 0xFF); }
// a..b at t/255, colors as 0xRRGGBB
static inline uint16_t mixHex(uint32_t a, uint32_t b, int t) {
  int r = ((a >> 16) * (255 - t) + (b >> 16) * t) / 255;
  int g = (((a >> 8) & 0xFF) * (255 - t) + ((b >> 8) & 0xFF) * t) / 255;
  int bl = ((a & 0xFF) * (255 - t) + (b & 0xFF) * t) / 255;
  return RGB565(r, g, bl);
}
static const ThemeDef &theme() { return THEMES[cfg.theme % NUM_THEMES]; }
static void applyTheme() {
  const ThemeDef &t = theme();
  tc = { hex565(t.bg), hex565(t.panel), hex565(t.hilite), hex565(t.accent), hex565(t.bronze), hex565(t.text), hex565(t.dim) };
}
// ----------------------------------------------------------------------------
// Board RGB LED (GPIO 4 red, 16 green, 17 blue; on when the pin is low). The LEDC
// hardware dims it; a 30 ms timer runs the breathing and the flashes, so it costs the
// games nothing.
// ----------------------------------------------------------------------------
#include <driver/ledc.h>
#include <esp_timer.h>
#ifndef LED_PINS
#define LED_PINS 4, 16, 17     // red, green, blue (lit when low)
#endif
struct LedColor { const char *name; uint32_t rgb; };
static const LedColor LED_COLORS[] = {
  { "Theme", 0 }, { "Red", 0xFF0000 }, { "Orange", 0xFF5000 }, { "Yellow", 0xFFB000 }, { "Green", 0x00FF20 },
  { "Cyan", 0x00D0FF }, { "Blue", 0x0030FF }, { "Purple", 0x8000FF }, { "Magenta", 0xFF00A0 }, { "White", 0xFFFFFF },
};
#define NUM_LED_COLORS (int)(sizeof LED_COLORS / sizeof LED_COLORS[0])
static const char *const LED_MODES[] = { "Off", "Solid", "Breathing" };
static const char *const LED_GAME[] = { "Same as menus", "Steady", "Off" };
enum { LED_FLASH_NONE, LED_FLASH_SAVE, LED_FLASH_CHEAT };
static volatile bool ledInGame = false;
static volatile uint8_t ledFlashKind = LED_FLASH_NONE;
static volatile uint32_t ledFlashAt = 0;
static bool ledReady = false;

static uint32_t ledBaseColor() {
  uint32_t c = LED_COLORS[cfg.ledColor % NUM_LED_COLORS].rgb;
  return cfg.ledColor % NUM_LED_COLORS ? c : theme().accent;
}
static void ledWrite(uint32_t rgb, int level) {   // level 0..255
  static uint32_t last = 0xFFFFFFFF;
  int b = (cfg.ledBright % 4 + 1) * 64;            // 25..100 %
  uint32_t key = (rgb ^ (level << 24)) + b;
  if (key == last) return;
  last = key;
  const uint8_t ch[3] = { (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb };
  for (int i = 0; i < 3; i++) {
    uint32_t v = ch[i] * level / 255 * b / 256;
    v = v * v / 255;                               // eyes see brightness roughly squared
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)i, v);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)i);
  }
}
static void ledTimer(void *) {
  uint32_t now = millis();
  if (ledFlashKind != LED_FLASH_NONE) {
    uint32_t t = now - ledFlashAt;
    if (t < 600) {   // three quick blinks
      ledWrite(ledFlashKind == LED_FLASH_SAVE ? 0x00FF20 : 0xFFB000, (t / 100) & 1 ? 0 : 255);
      return;
    }
    ledFlashKind = LED_FLASH_NONE;
  }
  int mode = cfg.ledMode % 3;
  if (ledInGame && cfg.ledGame == 1 && mode) mode = 1;
  if (ledInGame && cfg.ledGame == 2) mode = 0;
  int level = mode == 0 ? 0 : mode == 1 ? 255 : (int)(20 + 235 * (0.5f - 0.5f * cosf(now * (6.2832f / 3000))));
  ledWrite(ledBaseColor(), level);
}
static void ledInit() {
#if !HAS_LED
  return;   // no LED on this board: leave its pins alone
#endif
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_8_BIT;
  t.timer_num = LEDC_TIMER_0;
  t.freq_hz = 5000;
  t.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&t) != ESP_OK) return;
  const int pins[3] = { LED_PINS };
  for (int i = 0; i < 3; i++) {
    ledc_channel_config_t c = {};
    c.gpio_num = pins[i];
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = (ledc_channel_t)i;
    c.timer_sel = LEDC_TIMER_0;
    c.duty = 0;
    c.flags.output_invert = 1;   // the LED lights when the pin is low
    ledc_channel_config(&c);
  }
  esp_timer_create_args_t a = {};
  a.callback = ledTimer;
  a.name = "led";
  esp_timer_handle_t h;
  if (esp_timer_create(&a, &h) == ESP_OK) esp_timer_start_periodic(h, 30000);
  ledReady = true;
}
static void ledFlash(int kind) {
  if ((kind == LED_FLASH_SAVE && !cfg.ledFlashSave) || (kind == LED_FLASH_CHEAT && !cfg.ledFlashCheat)) return;
  ledFlashAt = millis();
  ledFlashKind = kind;
}
// While a game runs (the pause menu counts as a menu).
struct LedGameScope {
  LedGameScope() { ledInGame = true; }
  ~LedGameScope() { ledInGame = false; }
};
struct LedMenuScope {   // inside the pause menu
  LedMenuScope() { ledInGame = false; }
  ~LedMenuScope() { ledInGame = true; }
};

#define C_BG     tc.bg       // background
#define C_PANEL  tc.panel    // list rows, buttons
#define C_HILITE tc.hilite   // selected row
#define C_ACCENT tc.accent
#define C_BRONZE tc.bronze   // folder icons, header edge
#define C_TEXT   tc.text
#define C_DIM    tc.dim
#define C_RED    0xF800
#define C_GREEN  0x07E0

// Header bar color at row y: bright top edge, gradient, bronze bottom edge.
static uint16_t headerColor(int y) {
  const ThemeDef &t = theme();
  return y == 0 ? hex565(t.hdrTop) : y >= 30 ? C_BRONZE : mixHex(t.hdrA, t.hdrB, y * 255 / 31);
}

// ---- menu animation: header stars, a shine along the bronze edge, the glowing
// selected row and scrolling names. uiTick() is called from every menu's wait loop. ----
#define HDR_STARS 22
static struct { int16_t x, y; uint8_t ph; } hdrStar[HDR_STARS];
static int hdrX0 = 0, hdrX1 = 0;     // free part of the header (no title or buttons)
static bool hdrOn = false;
static uint32_t uiAnimAt = 0;

static void hdrBegin(const char *title, bool rightButton) {
  lcd.setFont(&fonts::Font2);
  hdrX0 = 8 + lcd.textWidth(title) + 10;
  hdrX1 = rightButton ? SCR_W - 70 : SCR_W - 4;
  hdrOn = true;
  uint32_t r = esp_random();
  for (int i = 0; i < HDR_STARS; i++) {
    r = r * 1103515245u + 12345u;
    hdrStar[i].x = (r >> 8) % SCR_W;
    hdrStar[i].y = 3 + (r >> 20) % 24;
    hdrStar[i].ph = r >> 24;
  }
}
// headerHints() takes part of the free span
static void hdrReserve(int xFrom) { if (xFrom - 6 < hdrX1) hdrX1 = xFrom - 6; }

// selected row: pulsing outline with a light running along its top and bottom edges
static struct { bool on; int16_t x, y, w, h; uint16_t bg; } selGlow = { false };
static void glowSet(int x, int y, int w, int h, uint16_t bg) { selGlow = { true, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, bg }; }
static void glowOff() { selGlow.on = false; }

// one name that scrolls sideways inside a clip box when it doesn't fit
static struct {
  bool on; int16_t x0, x1, yc; uint16_t fg, bg; String text; int16_t tw; uint32_t t0; uint8_t loops;
} marq = { false };
static void marqueeOff() { marq.on = false; marq.text = String(); }
// true if the text is wider than the box (then it scrolls; drawn clipped at offset 0 now)
static bool marqueeSet(int x0, int x1, int yc, const String &text, uint16_t fg, uint16_t bg) {
  lcd.setFont(&fonts::Font2);
  int tw = lcd.textWidth(text);
  if (tw <= x1 - x0) { marqueeOff(); return false; }
  marq.on = true; marq.x0 = x0; marq.x1 = x1; marq.yc = yc; marq.fg = fg; marq.bg = bg;
  marq.text = text; marq.tw = tw; marq.t0 = millis(); marq.loops = 0;
  return true;
}
static void marqueeDraw(int off) {
  lcd.setClipRect(marq.x0, marq.yc - 9, marq.x1 - marq.x0, 18);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(marq.fg, marq.bg);
  lcd.setTextDatum(middle_left);
  lcd.drawString(marq.text, marq.x0 - off, marq.yc);
  int end = marq.x0 - off + marq.tw;
  if (end < marq.x1) lcd.fillRect(end, marq.yc - 9, marq.x1 - end, 18, marq.bg);
  lcd.clearClipRect();
  lcd.setTextDatum(top_left);
}
// offset for the time since it started: wait, scroll to the end, wait, back to the start.
// Returns -1 once a whole cycle is done.
static int marqueeOffset(uint32_t ms) {
  const int over = marq.tw - (marq.x1 - marq.x0);
  const uint32_t wait = 1200, run = (uint32_t)over * 1000 / 35;   // 35 px per second
  if (ms < wait) return 0;
  if (ms < wait + run) return (int)((ms - wait) * 35 / 1000);
  if (ms < wait + run + wait) return over;
  return -1;
}

static void uiTick() {
  uint32_t now = millis();
  if (now - uiAnimAt < 40) return;
  uiAnimAt = now;
  const ThemeDef &t = theme();
  lcd.startWrite();
  if (hdrOn) {
    // stars twinkle in the free part of the header
    for (int i = 0; i < HDR_STARS; i++) {
      int x = hdrStar[i].x, y = hdrStar[i].y;
      if (x < hdrX0 || x >= hdrX1) continue;
      int b = (int)(128 + 127 * sinf(now * 0.004f + hdrStar[i].ph * 0.0245f));
      // Fade in from 45 rather than 90: below that the old curve simply switched a star
      // off, so most of them were dark at any moment and none was ever bright.
      b = b < 45 ? 0 : (b - 45) * 255 / 210;
      // Near the peak the colour runs toward white, so the theme still tints the
      // mid-range and only the brightest few go truly white.
      uint32_t tint = t.text;
      if (b > 217) {
        int k = (b - 217) * 255 / 38;
        uint32_t cr = (t.text >> 16) & 255, cg = (t.text >> 8) & 255, cb = t.text & 255;
        cr += (255 - cr) * k / 255; cg += (255 - cg) * k / 255; cb += (255 - cb) * k / 255;
        tint = (cr << 16) | (cg << 8) | cb;
      }
      // Every pixel of the footprint is painted every tick, at zero when it is not lit,
      // so a star erases its own flare as it fades instead of leaving specks behind.
      auto px = [&](int ax, int ay, int lvl) {
        if (ax < hdrX0 || ax >= hdrX1 || ay < 2 || ay > 28) return;
        uint16_t c = headerColor(ay);
        uint32_t bg = ((c >> 11) << 19) | (((c >> 5) & 63) << 10) | ((c & 31) << 3);
        lcd.drawPixel(ax, ay, mixHex(bg, tint, lvl));
      };
      int wide = b > 190 ? (b - 190) * 255 / 65 : 0;   // core swells to 2x2 at the peak
      int fa   = b > 217 ? (b - 217) * 255 / 38 : 0;   // four-point flare
      px(x, y, b);
      px(x + 1, y, wide); px(x, y + 1, wide); px(x + 1, y + 1, wide);
      px(x - 2, y, fa); px(x + 2, y, fa); px(x, y - 2, fa); px(x, y + 2, fa);
    }
    // a light runs along the bronze edge every few seconds
    static int shineLast = -100;
    int sx = (int)((now / 6) % (SCR_W + 600)) - 60;
    if (shineLast > -100) lcd.drawFastHLine(max(0, shineLast - 40), 30, 80, C_BRONZE), lcd.drawFastHLine(max(0, shineLast - 40), 31, 80, C_BRONZE);
    if (sx < SCR_W + 40) {
      for (int x = max(0, sx - 40); x < min(SCR_W, sx + 40); x++) {
        int k = 255 - abs(x - sx) * 255 / 40;
        uint16_t c = mixHex(t.bronze, 0xFFFFFF, k * 3 / 4);
        lcd.drawPixel(x, 30, c); lcd.drawPixel(x, 31, c);
      }
      shineLast = sx;
    } else {
      shineLast = -100;
    }
  }
  if (selGlow.on) {
    int p = (int)(128 + 127 * sinf(now * 0.005f));
    uint16_t edge = mixHex(t.accent, t.bronze, p);
    const int x = selGlow.x, y = selGlow.y, w = selGlow.w, h = selGlow.h;
    lcd.drawRoundRect(x, y, w, h, 5, edge);
    lcd.drawRoundRect(x - 1, y - 1, w + 2, h + 2, 6, mixHex(t.bg, t.accent, p / 3));   // soft halo
    // running light on the straight top and bottom edges
    int span = w - 12, lx = x + 6 + (int)((now / 5) % (span + 80)) - 40;
    for (int i = -24; i <= 24; i++) {
      int px = lx + i;
      if (px < x + 6 || px >= x + w - 6) continue;
      int k = 255 - abs(i) * 255 / 24;
      uint16_t c = k > 8 ? mixHex(t.accent, 0xFFFFFF, k * 2 / 3) : edge;
      lcd.drawPixel(px, y, c);
      lcd.drawPixel(px, y + h - 1, c);
    }
    // pixels the light left behind
    int px = lx - 25;
    if (px >= x + 6 && px < x + w - 6) { lcd.drawPixel(px, y, edge); lcd.drawPixel(px, y + h - 1, edge); }
  }
  if (marq.on) {
    int off = marqueeOffset(now - marq.t0);
    if (off < 0) { marq.t0 = now; off = 0; if (marq.loops < 255) marq.loops++; }
    marqueeDraw(off);
  }
  lcd.endWrite();
}

static void uiHeaderIcon(int cx, int cy, bool back);
static void uiHeader(const char *title, const char *right = nullptr, const char *hint = nullptr) {
  // gunmetal bar: bright top edge, steel gradient, bronze bottom edge
  for (int y = 0; y < 32; y++) lcd.drawFastHLine(0, y, SCR_W, headerColor(y));
  hdrBegin(title, right != nullptr);
  lcd.setTextColor(C_TEXT);   // transparent background over the gradient
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(middle_left);
  lcd.drawString(title, 8, 15);
  if (right) {
    lcd.fillRoundRect(SCR_W - 64, 4, 60, 24, 6, C_PANEL);
    lcd.setTextColor(C_TEXT, C_PANEL);
    lcd.setTextDatum(middle_center);
#if HAVE_BT
    if (!hint && !strcmp(right, "Back")) {
      // touch button that also shows its controller button (works if the pad drops out)
      uiHeaderIcon(SCR_W - 52, 16, true);
      lcd.drawString(right, SCR_W - 28, 16);
      lcd.setTextDatum(top_left);
      return;
    }
#endif
    if (hint) {
      // small second line: the controller button that does the same (e.g. "Share")
      lcd.drawString(right, SCR_W - 34, 12);
      lcd.setFont(&fonts::Font0);
      lcd.setTextColor(C_ACCENT, C_PANEL);
      lcd.drawString(hint, SCR_W - 34, 23);
      lcd.setFont(&fonts::Font2);
    } else {
      lcd.drawString(right, SCR_W - 34, 16);
    }
  }
  lcd.setTextDatum(top_left);
}

// ---- controller button hints, drawn with code ----
// Shown only while a controller is connected, with the marks printed on that controller:
//   PlayStation: Cross (bottom) and Circle (right)
//   Xbox, Stadia, Android pads: A (bottom) and B (right)
//   Nintendo Switch, 8BitDo: B (bottom) and A (right)
// Which button means OK / Back follows the "Button layout" setting (Game Boy layout:
// right = OK, bottom = Back; Xbox layout: swapped). Icons say which physical button.
static bool padHints() { return HAVE_BT && btCount() > 0; }
enum { ICON_RIGHT = 0, ICON_BOTTOM = 1 };   // the face button on the right / at the bottom
static int okIcon() { return cfg.btLayout ? ICON_BOTTOM : ICON_RIGHT; }
static int backIcon() { return cfg.btLayout ? ICON_RIGHT : ICON_BOTTOM; }

enum { PAD_PS = 0, PAD_XBOX = 1, PAD_NINTENDO = 2 };
static int padFamily() {
#if HAVE_BT
  for (auto c : btCtl) {
    if (!c || !c->isConnected()) continue;
    switch (c->getModel()) {
      case CONTROLLER_TYPE_PS3Controller:
      case CONTROLLER_TYPE_PS4Controller:
      case CONTROLLER_TYPE_PS5Controller:
        return PAD_PS;
      case CONTROLLER_TYPE_SwitchProController:
      case CONTROLLER_TYPE_SwitchJoyConLeft:
      case CONTROLLER_TYPE_SwitchJoyConRight:
      case CONTROLLER_TYPE_SwitchJoyConPair:
      case CONTROLLER_TYPE_8BitdoController:
        return PAD_NINTENDO;
      default:
        return PAD_XBOX;
    }
  }
#endif
  return PAD_PS;
}

static void drawPadIcon(int cx, int cy, int which) {
  int fam = padFamily();
  if (fam == PAD_PS) {
    if (which == ICON_RIGHT) {
      uint16_t c = RGB565(0xF0, 0x60, 0x68);   // PlayStation red circle
      lcd.drawCircle(cx, cy, 4, c);
      lcd.drawCircle(cx, cy, 3, c);
    } else {
      uint16_t c = RGB565(0x78, 0xA8, 0xF0);   // PlayStation blue cross
      for (int o = 0; o < 2; o++) {
        lcd.drawLine(cx - 4 + o, cy - 4, cx + 3 + o, cy + 3, c);
        lcd.drawLine(cx - 4 + o, cy + 3, cx + 3 + o, cy - 4, c);
      }
    }
    return;
  }
  // a lettered button: Xbox colors (A green, B red), Nintendo plain gray
  bool bottom = which == ICON_BOTTOM;
  char letter = fam == PAD_XBOX ? (bottom ? 'A' : 'B') : (bottom ? 'B' : 'A');
  uint16_t ring = fam == PAD_XBOX ? (letter == 'A' ? RGB565(0x60, 0xC8, 0x50) : RGB565(0xF0, 0x58, 0x50))
                                  : RGB565(0xC8, 0xCC, 0xD4);
  lcd.fillCircle(cx, cy, 5, ring);
  const lgfx::IFont *f = lcd.getFont();
  lcd.setFont(&fonts::Font0);
  lcd.setTextColor(TFT_BLACK);
  uint8_t d = lcd.getTextDatum();
  lcd.setTextDatum(middle_center);
  char t[2] = { letter, 0 };
  lcd.drawString(t, cx + 1, cy + 1);
  lcd.setTextDatum(d);
  lcd.setFont(f);
}

static void uiHeaderIcon(int cx, int cy, bool back) { drawPadIcon(cx, cy, back ? backIcon() : okIcon()); }

// "[icon] OK  [icon] Back" right-aligned at xRight in the header. Drops the words
// (icons only), or the hints altogether, rather than cover the title.
static void headerHints(const char *title, int xRight, const char *ok, const char *back) {
  if (!padHints()) return;
  lcd.setFont(&fonts::Font2);
  int titleEnd = 8 + lcd.textWidth(title) + 8;
  lcd.setFont(&fonts::Font0);
  struct Item { int icon; const char *label; } items[2];
  int n = 0;
  if (ok) items[n++] = { okIcon(), ok };
  if (back) items[n++] = { backIcon(), back };
  for (int withText = 1; withText >= 0; withText--) {
    int w = 0;
    for (int i = 0; i < n; i++) w += 10 + (withText ? lcd.textWidth(items[i].label) + 3 : 0) + (i ? 8 : 0);
    if (xRight - w < titleEnd) continue;
    int x = xRight - w;
    hdrReserve(x);
    lcd.setTextColor(C_TEXT);
    lcd.setTextDatum(middle_left);
    for (int i = 0; i < n; i++) {
      if (i) x += 8;
      drawPadIcon(x + 4, 15, items[i].icon);
      x += 10;
      if (withText) { lcd.drawString(items[i].label, x + 2, 15); x += lcd.textWidth(items[i].label) + 3; }
    }
    lcd.setTextDatum(top_left);
    lcd.setFont(&fonts::Font2);
    return;
  }
  lcd.setFont(&fonts::Font2);
}

// Small "[icon] Continue" in the bottom right corner of message screens.
static void bottomHint(const char *label) {
  if (!padHints()) return;
  lcd.setFont(&fonts::Font0);
  int w = 10 + lcd.textWidth(label) + 3;
  int x = SCR_W - 6 - w, y = SCR_H - 9;
  drawPadIcon(x + 4, y, okIcon());
  lcd.setTextColor(C_DIM);
  lcd.setTextDatum(middle_left);
  lcd.drawString(label, x + 12, y);
  lcd.setTextDatum(top_left);
  lcd.setFont(&fonts::Font2);
}

// ----------------------------------------------------------------------------
// Wireframe H@N: the boot screen and the "Loading game" screen draw the logo as a 3D
// wireframe (outline from src/wire/wire_logo.h, extruded), into a small 4-bit buffer
// that is then pushed to the screen, so it never flickers.
// ----------------------------------------------------------------------------
#include "wire/wire_logo.h"
static uint16_t *dmaBuf[2];
#define DMA_LINES 3
#define DMA_W 320

enum { WI_BG = 0, WI_FRONT, WI_BACK, WI_SIDE, WI_STAR1, WI_STAR2, WI_STAR3, WI_STAR4 };
struct WireFb {
  uint8_t *px = nullptr;      // 4 bits per pixel, even x in the low nibble
  uint16_t *rows = nullptr;   // RGB565 for WF_ROWS rows, for pushing
  int w = 0, h = 0, sx = 0, sy = 0;   // size and position on screen
  uint16_t pal[16];
};
#define WF_ROWS 8

static bool wfAlloc(WireFb &f, int x, int y, int w, int h) {
  f.w = (w + 1) & ~1; f.h = h; f.sx = x; f.sy = y;
  f.px = (uint8_t *)malloc(f.w * f.h / 2);
  f.rows = (uint16_t *)malloc(f.w * WF_ROWS * 2);
  if (!f.px || !f.rows) { free(f.px); free(f.rows); f.px = nullptr; f.rows = nullptr; return false; }
  return true;
}
static void wfFree(WireFb &f) { free(f.px); free(f.rows); f.px = nullptr; f.rows = nullptr; }
static inline void wfClear(WireFb &f) { memset(f.px, 0, f.w * f.h / 2); }
static inline void wfPlot(WireFb &f, int x, int y, uint8_t c) {
  if ((unsigned)x >= (unsigned)f.w || (unsigned)y >= (unsigned)f.h) return;
  uint8_t &b = f.px[(y * f.w + x) >> 1];
  b = (x & 1) ? (b & 0x0F) | (c << 4) : (b & 0xF0) | c;
}
static void wfLine(WireFb &f, int x0, int y0, int x1, int y1, uint8_t c) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
  if (dx > 2000 || dy < -2000) return;
  while (true) {
    wfPlot(f, x0, y0, c);
    if (x0 == x1 && y0 == y1) return;
    int e2 = 2 * e;
    if (e2 >= dy) { e += dy; x0 += sx; }
    if (e2 <= dx) { e += dx; y0 += sy; }
  }
}
static void wfPush(WireFb &f) {
  xSemaphoreTake(lcdMutex, portMAX_DELAY);   // the boot animation draws from the other core
  lcd.startWrite();
  for (int y = 0; y < f.h; y += WF_ROWS) {
    int n = min(WF_ROWS, f.h - y);
    const uint8_t *s = f.px + y * f.w / 2;
    uint16_t *d = f.rows;
    for (int i = 0; i < n * f.w / 2; i++) { *d++ = f.pal[s[i] & 15]; *d++ = f.pal[s[i] >> 4]; }
    lcd.pushImage(f.sx, f.sy + y, f.w, n, f.rows);
  }
  lcd.endWrite();
  xSemaphoreGive(lcdMutex);
}

// The logo, centered at (cx, cy), hPx pixels tall when straight on.
// ay / ax: turn around the vertical / horizontal axis; dist: camera distance in logo heights.
static void wfLogo(WireFb &f, float cx, float cy, float hPx, float ay, float ax, float dist) {
  const float fov = hPx * 6.0f, depth = 0.2f;
  const float ca = cosf(ay), sa = sinf(ay), cb = cosf(ax), sb = sinf(ax);
  int16_t fx[80], fy[80], bx[80], by[80];
  int p = 0;
  for (size_t c = 0; c < sizeof WIRE_LEN; c++) {
    int n = WIRE_LEN[c];
    for (int i = 0; i < n; i++) {
      float x = WIRE_PTS[p + i][0] / 256.0f, y = WIRE_PTS[p + i][1] / 256.0f;
      for (int side = 0; side < 2; side++) {
        float z = side ? depth : -depth;
        float x2 = x * ca - z * sa, z2 = x * sa + z * ca;
        float y2 = y * cb - z2 * sb, z3 = y * sb + z2 * cb + dist;
        if (z3 < 0.2f) z3 = 0.2f;
        int16_t px = (int16_t)(cx + x2 * fov / z3), py = (int16_t)(cy + y2 * fov / z3);
        if (side) { bx[i] = px; by[i] = py; } else { fx[i] = px; fy[i] = py; }
      }
    }
    for (int i = 0; i < n; i++) wfLine(f, bx[i], by[i], bx[(i + 1) % n], by[(i + 1) % n], WI_BACK);
    for (int i = 0; i < n; i += n > 20 ? 4 : 1) wfLine(f, fx[i], fy[i], bx[i], by[i], WI_SIDE);
    for (int i = 0; i < n; i++) wfLine(f, fx[i], fy[i], fx[(i + 1) % n], fy[(i + 1) % n], WI_FRONT);
    p += n;
  }
}

// palette from the theme; glow 0..255 brightens the logo toward white
static void wfPalette(WireFb &f, int glow) {
  const ThemeDef &t = theme();
  for (int i = 0; i < 16; i++) f.pal[i] = C_BG;
  f.pal[WI_FRONT] = mixHex(t.accent, 0xFFFFFF, glow);
  f.pal[WI_BACK] = mixHex(t.bronze, 0xFFFFFF, glow);
  f.pal[WI_SIDE] = mixHex(t.dim, 0xFFFFFF, glow / 2);
  for (int i = 0; i < 4; i++) f.pal[WI_STAR1 + i] = mixHex(t.bg, t.text, 70 + i * 60);
  for (int i = 0; i < 16; i++) f.pal[i] = (uint16_t)((f.pal[i] >> 8) | (f.pal[i] << 8));   // pushImage takes them byte-swapped
}

// ---- the logo scene ----
//
// One animation, drawn in two places: the boot screen and the screensaver. The boot
// screen used to run a version of its own that flew in from far away, flashed once and
// settled - fine as an entrance, but it had a beginning and an end, so it could not be
// looped and was no use standing still on an idle board. This one loops: the stars run
// on a modulo and the logo rocks on two sines, so it never arrives anywhere to stop.
//
// Both callers draw through here, so changing the scene changes the boot screen and the
// screensaver together, which is the point of there being one of it.
#define LOGO_STARS 64

static void logoScene(WireFb &f, float t) {
  static struct Star { float x, y, z; } st[LOGO_STARS];
  static bool seeded = false;
  if (!seeded) {
    uint32_t r = esp_random();
    auto rnd = [&]() { r = r * 1103515245u + 12345u; return (float)((r >> 8) & 0xFFFF) / 65535.0f; };
    for (int i = 0; i < LOGO_STARS; i++) st[i] = { rnd() * 2 - 1, rnd() * 2 - 1, rnd() };
    seeded = true;
  }
  const float cx = f.w / 2.0f, cy = f.h * 0.55f;
  wfClear(f);
  for (int i = 0; i < LOGO_STARS; i++) {   // stars fly toward the viewer, forever
    float z = fmodf(st[i].z - t * 0.25f + 8.0f, 1.0f) + 0.05f;
    int x = (int)(cx + st[i].x * f.w * 0.2f / z), y = (int)(f.h / 2 + st[i].y * f.h * 0.25f / z);
    int lvl = z > 0.75f ? 0 : z > 0.5f ? 1 : z > 0.25f ? 2 : 3;
    wfPlot(f, x, y, WI_STAR1 + lvl);
    if (lvl == 3) wfPlot(f, x + 1, y, WI_STAR4);
  }
  wfPalette(f, 0);
  const float hPx = min(f.h * 0.42f, f.w * 0.82f / (WIRE_LOGO_ASPECT / 256.0f));
  wfLogo(f, cx, cy, hPx, sinf(t * 1.2f) * 0.35f, sinf(t * 0.9f) * 0.2f, 6.0f);
  wfPush(f);
}

// ---- boot screen: the logo scene, with "hackatnight.com" typing itself underneath.
// Runs as a task on core 0 while the rest of the board starts up. ----
static volatile bool splashSkip = false, splashStop = false, splashDone = true;
static WireFb bootFb;
#define BOOT_TEXT_H 40
static const char *SP_TEXT = "hackatnight.com";

static void splashTask(void *) {
  WireFb &f = bootFb;
  const uint32_t t0 = millis();
  int typed = -1; bool cursor = false;
  const int ty = SCR_H - BOOT_TEXT_H / 2 - 4;
  while (!splashSkip) {
    float t = (millis() - t0) / 1000.0f;
    logoScene(f, t);
    // the web address types itself, then a blinking cursor
    int n = t < 1.9f ? 0 : min((int)strlen(SP_TEXT), (int)((t - 1.9f) / 0.07f) + 1);
    bool cur = n < (int)strlen(SP_TEXT) || ((int)(t * 3) & 1);
    if (t >= 1.9f && (n != typed || cur != cursor)) {
      typed = n; cursor = cur;
      xSemaphoreTake(lcdMutex, portMAX_DELAY);
      lcd.startWrite();
      lcd.setFont(&fonts::Font2);
      lcd.setTextColor(C_ACCENT, C_BG);
      lcd.setTextDatum(middle_left);
      String s = String(SP_TEXT).substring(0, n) + (cur ? "_" : " ");
      int x = (SCR_W - lcd.textWidth(String(SP_TEXT) + "_")) / 2;
      lcd.drawString(s, x, ty);
      lcd.setTextDatum(top_left);
      lcd.endWrite();
      xSemaphoreGive(lcdMutex);
    }
    if (t > 3.3f && splashStop) break;
    delay(5);
  }
  splashDone = true;
  vTaskDelete(nullptr);
}

static void splashStart() {
  lcd.fillScreen(C_BG);
  if (!wfAlloc(bootFb, 0, 0, SCR_W, SCR_H - BOOT_TEXT_H)) {
    // not enough RAM for the animation: a plain title instead
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_TEXT, C_BG);
    lcd.setTextDatum(middle_center);
    lcd.setTextSize(3);
    lcd.drawString("H@N", SCR_W / 2, SCR_H / 2 - 16);
    lcd.setTextSize(1);
    lcd.drawString("hackatnight.com", SCR_W / 2, SCR_H / 2 + 24);
    lcd.setTextDatum(top_left);
    splashDone = true;
    return;
  }
  splashSkip = splashStop = splashDone = false;
  xTaskCreatePinnedToCore(splashTask, "splash", 4096, nullptr, 1, nullptr, 0);
}

// Waits for the animation to finish (or a tap / button press to skip it), then frees it.
static void splashFinish() {
  splashStop = true;
  while (!splashDone) {
    int x, y;
    if (readTouch(x, y) || bootPressed()) splashSkip = true;
    delay(15);
  }
  int x, y;
  while (readTouch(x, y)) delay(20);   // don't let the skip tap select a ROM
  wfFree(bootFb);
}

// ---- "Loading game" screen: the logo rocks while the game is copied to flash ----
static WireFb loadFb;
static uint32_t loadT0 = 0, loadAt = 0;
static int loadNameY() { return SCR_H - 96; }

static void loaderTick() {
  if (!loadFb.px) return;
  uint32_t now = millis();
  if (now - loadAt < 40) return;
  loadAt = now;
  float t = (now - loadT0) / 1000.0f;
  WireFb &f = loadFb;
  wfClear(f);
  wfPalette(f, 0);
  float hPx = min(f.h * 0.62f, f.w * 0.85f / (WIRE_LOGO_ASPECT / 256.0f));
  wfLogo(f, f.w / 2.0f, f.h / 2.0f, hPx, sinf(t * 1.4f) * 0.9f, sinf(t * 1.0f) * 0.35f, 6.0f);
  wfPush(f);
  uiTick();   // header stars
}

static void uiHeader(const char *title, const char *right, const char *hint);
// title in the header, the game's name, two lines of notes at the bottom
static void loaderBegin(const char *title, const String &name, const char *note1, const char *note2) {
  lcd.fillScreen(C_BG);
  uiHeader(title, nullptr, nullptr);
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(middle_center);
  String n = name;
  if (lcd.textWidth(n) > SCR_W - 16) {
    while (n.length() > 4 && lcd.textWidth(n + "~") > SCR_W - 16) n.remove(n.length() - 1);
    n += "~";
  }
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.drawString(n, SCR_W / 2, loadNameY());
  lcd.setTextColor(C_DIM, C_BG);
  if (note1) lcd.drawString(note1, SCR_W / 2, SCR_H - 34);
  if (note2) lcd.drawString(note2, SCR_W / 2, SCR_H - 16);
  lcd.setTextDatum(top_left);
  int top = 38, h = loadNameY() - 14 - top;
  int w = min(SCR_W - 16, 300);
  if (h > 20) wfAlloc(loadFb, (SCR_W - w) / 2, top, w, h);   // no RAM: no animation
  loadT0 = millis(); loadAt = 0;
  loaderTick();
}

static void loaderProgress(const char *label, int pct) {
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.setTextDatum(middle_center);
  char buf[48];
  snprintf(buf, sizeof buf, "%s %d%%", label, pct);
  int y = loadNameY() + 22;
  lcd.fillRect(0, y - 9, SCR_W, 18, C_BG);
  lcd.drawString(buf, SCR_W / 2, y);
  int bw = min(240, SCR_W - 40), bx = (SCR_W - bw) / 2;
  lcd.drawRoundRect(bx, y + 12, bw, 10, 4, C_DIM);
  if (pct > 0) lcd.fillRoundRect(bx + 2, y + 14, (bw - 4) * pct / 100, 6, 3, C_ACCENT);
  lcd.setTextDatum(top_left);
  loaderTick();
}

static void loaderEnd() { wfFree(loadFb); hdrOn = false; }

// Screensaver: the loading screen's wireframe H@N, after a quiet spell.
//
// It runs in the menus and the game list, never during a game - a game has its own
// picture to show, and interrupting it would be a bug rather than a feature. Any touch,
// BOOT press or controller button ends it, and the press that wakes it is swallowed so
// it does not also pick whatever was under it.
static const uint8_t IDLE_CHOICES[] = { 0, 1, 5, 10 };   // minutes; 0 = never

static String idleName() {
  return cfg.idleMin ? String(cfg.idleMin) + " min" : String("Never");
}

static void idleCycle() {
  unsigned k = 0;
  while (k < sizeof IDLE_CHOICES && IDLE_CHOICES[k] != cfg.idleMin) k++;
  cfg.idleMin = IDLE_CHOICES[(k + 1) % (sizeof IDLE_CHOICES)];
}

// Returns true if it ran, so the caller knows to redraw what was underneath.
static bool screensaverIfIdle() {
  if (!cfg.idleMin) return false;
  if (millis() - idleAt < (uint32_t)cfg.idleMin * 60000UL) return false;
  WireFb f;
  wfAlloc(f, 0, 0, SCR_W, SCR_H);
  if (!f.px) { idlePoke(); return false; }   // no RAM for it: leave the screen alone
  bool hdrWas = hdrOn;
  hdrOn = false;                             // keep uiTick's header stars off it
  lcd.fillScreen(C_BG);
  uint32_t t0 = millis(), last = 0;
  int x, y;
  while (!readTouch(x, y) && btNav() == NAV_NONE && !bootRaw()) {
    uint32_t now = millis();
    if (now - last >= 40) {
      last = now;
      logoScene(f, (now - t0) / 1000.0f);
    }
    delay(5);
  }
  wfFree(f);
  hdrOn = hdrWas;
  while (readTouch(x, y)) delay(10);   // swallow the waking tap
  while (bootRaw()) delay(10);
  idlePoke();
  return true;
}

static void uiMessage(const char *line1, const char *line2 = nullptr, uint16_t color = C_TEXT) {
  lcd.fillRect(0, 120, SCR_W, 80, C_BG);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(color, C_BG);
  lcd.setTextDatum(middle_center);
  lcd.drawString(line1, SCR_W / 2, 145);
  if (line2) { lcd.setTextColor(C_DIM, C_BG); lcd.drawString(line2, SCR_W / 2, 170); }
  lcd.setTextDatum(top_left);
}

static void musicStart();   // defined below; the error screen keeps the tune going

extern "C" void gb_fatal(const char *file, int line, const char *fmt, ...) {
  char msg[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  Serial.printf("FATAL %s:%d %s\n", file, line, msg);
  xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(500));
  lcd.waitDMA();
  lcd.endWrite();
  lcd.fillScreen(C_BG);
  uiHeader("Emulator error");
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_RED, C_BG);
  lcd.setCursor(8, 50);
  lcd.setTextWrap(true);
  lcd.print(msg);
  lcd.setTextColor(C_DIM, C_BG);
  lcd.setCursor(8, SCR_H - 40);
  lcd.print("Tap to restart");
  musicStart();   // an error screen is no reason for silence
  int x, y;
  waitTap(x, y);
  esp_restart();
}

// ----------------------------------------------------------------------------
// Audio: I2S0 -> internal DAC2 (GPIO26). GPIO25 (DAC1) is the touch reset line,
// so only the "left" DAC channel (DAC2) is enabled.
// ----------------------------------------------------------------------------
static bool audioRunning = false;
static volatile uint32_t audioWaitUs = 0;   // time spent waiting for audio buffer space this frame
static uint16_t audioBuf[128 * 2];   // stereo frames, converted 128 at a time

// The speaker amp idles at the DAC midpoint (0x80). Any jump to 0 V and back makes
// a loud pop and a current surge that can brown out the board, so the output is
// only ever ramped, and DMA buffers are refilled with the midpoint before stopping.
static uint8_t dacLast = 0;   // last 8-bit level sent to the DAC

static void audioWriteLevel(uint8_t level, int frames) {
  uint16_t o = (uint16_t)level << 8;
  for (int i = 0; i < 128; i++) { audioBuf[i * 2] = o; audioBuf[i * 2 + 1] = o; }
  size_t w;
  while (frames > 0) {
    int n = min(frames, 128);
    i2s_write(I2S_NUM_0, audioBuf, n * 4, &w, portMAX_DELAY);
    frames -= n;
  }
  dacLast = level;
}

static void audioRamp(uint8_t to, int frames) {
  int from = dacLast;
  size_t w;
  for (int done = 0; done < frames;) {
    int n = min(frames - done, 128);
    for (int i = 0; i < n; i++) {
      int v = from + (to - from) * (done + i + 1) / frames;
      uint16_t o = (uint16_t)v << 8;
      audioBuf[i * 2] = o; audioBuf[i * 2 + 1] = o;
    }
    i2s_write(I2S_NUM_0, audioBuf, n * 4, &w, portMAX_DELAY);
    done += n;
  }
  dacLast = to;
}

static void audioInit() {
  i2s_config_t c = {};
  c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  c.sample_rate = AUDIO_RATE;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  c.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  c.intr_alloc_flags = 0;
  c.dma_buf_count = AUDIO_DMA_BUFS;
  c.dma_buf_len = 128;
  c.use_apll = false;
  c.tx_desc_auto_clear = false;   // on underrun repeat the last (midpoint) buffer, never 0 V
  i2s_driver_install(I2S_NUM_0, &c, 0, NULL);
  i2s_set_pin(I2S_NUM_0, NULL);
  i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
  // one gentle ramp 0 V -> midpoint at boot (~0.4 s), then park there
  dacLast = 0;
  audioRamp(0x80, AUDIO_RATE * 4 / 10);
  audioWriteLevel(0x80, AUDIO_DMA_BUFS * 128 + 128);
  i2s_stop(I2S_NUM_0);
  audioRunning = false;
}

static void audioStart() {
  // DMA ring already holds midpoint samples, so starting is silent
  if (!audioRunning && cfg.volume) { i2s_start(I2S_NUM_0); audioRunning = true; }
}

static void audioStop() {
  if (!audioRunning) return;
  audioRamp(0x80, 256);                 // glide back to the midpoint
  audioWriteLevel(0x80, AUDIO_DMA_BUFS * 128 + 128); // flush the DMA ring with midpoint samples
  i2s_stop(I2S_NUM_0);                  // DAC holds the midpoint while stopped
  audioRunning = false;
}

// ----------------------------------------------------------------------------
// Menu music: a tiny synthesizer in the style of an 8-bit sound chip (two square waves,
// a 16-step triangle, a 16-step saw and noise drums) playing the note data in
// src/music/tunes.h (made by tools/music). It runs as a task on core 0 while the menus
// show, fades in and out, and has its own volume. It stops before a game starts, since
// games use the same sound output.
// ----------------------------------------------------------------------------
#include "music/tunes.h"
static volatile bool musicWant = false, musicIdle = true, musicNewTune = false;
static TaskHandle_t musicTaskH = nullptr;

struct MusicVoice { uint32_t phase, inc, age; bool on; uint8_t drum; uint16_t lfsr; float nval; };

static void musicTask(void *) {
  static uint16_t out[128 * 2];
  MusicVoice mv[8];
  const Tune *t = nullptr;
  int stepLen = 1, sPos = 0, nSteps = 1;
  uint32_t si = 0;
  float fade = 0;
  const float fadeStep = 1.0f / (AUDIO_RATE * 0.25f);   // a quarter second in or out
  auto begin = [&]() {
    t = &TUNES[cfg.musicTune % NUM_TUNES];
    stepLen = AUDIO_RATE * 60 / t->bpm / 4;
    nSteps = 1;
    for (int c = 0; c < t->nch; c++) nSteps = max(nSteps, (int)t->ch[c].n);
    memset(mv, 0, sizeof mv);
    for (auto &v : mv) { v.lfsr = 1; v.nval = 1; }
    si = 0; sPos = 0;
  };
  while (true) {
    if (!musicWant && musicIdle) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); continue; }
    if (musicIdle) {   // start: the DMA ring holds the midpoint, so starting is silent
      begin();
      fade = 0;
      musicNewTune = false;
      i2s_start(I2S_NUM_0);
      audioRunning = true;
      musicIdle = false;
    }
    if (musicNewTune) { musicNewTune = false; begin(); fade = 0; }
    const float target = musicWant ? 1.0f : 0.0f;
    const float g = t->gain * cfg.musicVol / 4.0f;
    int v8 = 0x80;
    for (int i = 0; i < 128; i++) {
      if (sPos == 0) {   // next step: start, hold or stop each channel's note
        for (int c = 0; c < t->nch; c++) {
          const TuneChannel &ch = t->ch[c];
          MusicVoice &v = mv[c];
          uint8_t tok = ch.steps[((int)si - ch.delay % ch.n + ch.n) % ch.n];
          if (tok == 1) v.on = false;
          else if (tok >= 2 && tok <= 4) { v.on = true; v.age = 0; v.drum = tok; }
          else if (tok >= 12) {
            v.on = true; v.age = 0; v.drum = 0;
            float f = 440.0f * powf(2.0f, (tok - 69) / 12.0f);
            v.inc = (uint32_t)(f / AUDIO_RATE * 4294967296.0f);
          }
        }
      }
      float mix = 0;
      for (int c = 0; c < t->nch; c++) {
        const TuneChannel &ch = t->ch[c];
        MusicVoice &v = mv[c];
        if (!v.on) continue;
        float tt = v.age / (float)AUDIO_RATE;
        v.age++;
        float env = ch.decay > 0 ? max(ch.sustain, 1.0f - ch.decay * tt) : 1.0f;
        if (ch.attack > 0 && tt < ch.attack) env *= tt / ch.attack;
        float smp;
        if (ch.kind == 0) {          // square
          v.phase += v.inc;
          smp = v.phase < (uint32_t)(ch.duty * 4294967296.0f) ? 1.0f : -1.0f;
        } else if (ch.kind == 1) {   // triangle, 16 steps
          v.phase += v.inc;
          smp = roundf((4.0f * fabsf(v.phase / 4294967296.0f - 0.5f) - 1.0f) * 7.5f) / 7.5f;
        } else if (ch.kind == 2) {   // saw, 16 steps
          v.phase += v.inc;
          smp = roundf((2.0f * v.phase / 4294967296.0f - 1.0f) * 7.5f) / 7.5f;
        } else if (v.drum == 2) {    // kick: a square falling in pitch
          float kf = 150.0f * expf(-tt * 25.0f) + 45.0f;
          v.phase += (uint32_t)(kf / AUDIO_RATE * 4294967296.0f);
          smp = v.phase < 0x80000000u ? 1.0f : -1.0f;
          env = max(0.0f, 1.0f - tt * 9.0f);
        } else {                     // hat / snare: noise from a 15-bit shift register
          int rate = v.drum == 3 ? 9000 : 3000;
          if ((uint64_t)v.age * rate / AUDIO_RATE != (uint64_t)(v.age - 1) * rate / AUDIO_RATE) {
            uint16_t bit = (v.lfsr ^ (v.lfsr >> 1)) & 1;
            v.lfsr = (v.lfsr >> 1) | (bit << 14);
            v.nval = (v.lfsr & 1) ? 1.0f : -1.0f;
          }
          smp = v.nval;
          env = max(0.0f, 1.0f - tt * (v.drum == 3 ? 28.0f : 9.0f));
        }
        mix += smp * env * ch.vol;
      }
      if (fade < target) fade = min(target, fade + fadeStep);
      else if (fade > target) fade = max(target, fade - fadeStep);
      float x = mix * g * fade;
      x = x > 1 ? 1 : x < -1 ? -1 : x;
      v8 = 128 + (int)(127 * x);
      out[i * 2] = out[i * 2 + 1] = (uint16_t)(v8 << 8);
      if (++sPos >= stepLen) { sPos = 0; si = (si + 1) % nSteps; }
    }
    size_t w;
    i2s_write(I2S_NUM_0, out, sizeof out, &w, portMAX_DELAY);
    dacLast = (uint8_t)v8;
    if (!musicWant && fade <= 0) {   // faded out: park at the midpoint and stop
      audioWriteLevel(0x80, AUDIO_DMA_BUFS * 128 + 128);
      i2s_stop(I2S_NUM_0);
      audioRunning = false;
      musicIdle = true;
    }
  }
}

static void musicInit() {
  xTaskCreatePinnedToCore(musicTask, "music", 3072, nullptr, 3, &musicTaskH, 0);
}
// Plays the chosen tune if music is on (does nothing if it's already playing).
static void musicStart() {
  if (!musicTaskH || !cfg.musicOn || musicWant) return;
  musicWant = true;
  xTaskNotifyGive(musicTaskH);
}
// Fades out and waits until the sound output is free again (before a game starts).
static void musicStop() {
  if (!musicTaskH) return;
  musicWant = false;
  xTaskNotifyGive(musicTaskH);
  uint32_t t0 = millis();
  while (!musicIdle && millis() - t0 < 1500) delay(5);
}

extern "C" void gb_audio_out(const uint8_t *s, int n) {
  if (!audioRunning || cfg.volume == 0) return;
  int vol = cfg.volume;  // 1..VOL_MAX
  int v = 0x80;
  size_t w;
  while (n > 0) {
    int k = min(n, 128);
    for (int i = 0; i < k; i++) {
      v = ((int)s[i] - 128) * vol / VOL_MAX + 128;
      uint16_t o = (uint16_t)(v << 8);
      audioBuf[i * 2] = o;
      audioBuf[i * 2 + 1] = o;
    }
    int64_t t0 = esp_timer_get_time();
    i2s_write(I2S_NUM_0, audioBuf, k * 4, &w, portMAX_DELAY);
    audioWaitUs += (uint32_t)(esp_timer_get_time() - t0);   // >0 when the buffer was full
    s += k;
    n -= k;
  }
  dacLast = (uint8_t)v;
}

// ----------------------------------------------------------------------------
// Frame presentation (core 0)
// ----------------------------------------------------------------------------
static volatile bool dispBusy = false;
static SemaphoreHandle_t frameReady;
static volatile int fpsValue = 0;
static volatile int emuFps = 0;
// FPS counter ("drawn/emulated"): written into the outgoing pixel rows of every frame,
// so the game image can't paint over it (it used to show up only now and then).
static char fpsText[24] = "";
static int sdPerSec = -1;      // 4 KB pages read from the SD card in the last second (-1 = not streaming)
static int stallsPerSec = 0;   // lines dropped in the last second because the display stopped
static const uint8_t FPS_FONT[13][7] = {   // 5x7 glyphs: 0-9, '/', 's' and '!'
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
  {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
  {0x01,0x01,0x02,0x04,0x08,0x10,0x10},
  {0x00,0x0E,0x10,0x0C,0x02,0x1C,0x00},   // 11: 's', for the SD page rate
  {0x04,0x04,0x04,0x04,0x04,0x00,0x04},   // 12: '!', shown only when the display wedges
};
#define FPS_BOX_H 9

// row: one output row (byte-swapped RGB565), y: its row number in the game image
static inline void fpsOverlay(uint16_t *row, int y, int w) {
  if (y >= FPS_BOX_H || !fpsText[0]) return;
  int n = strlen(fpsText);
  int bw = min(n * 6 + 2, w);
  for (int x = 0; x < bw; x++) row[x] = 0;          // black box
  int gy = y - 1;
  if (gy < 0 || gy >= 7) return;
  const uint16_t fg = __builtin_bswap16(C_GREEN);
  for (int i = 0; i < n; i++) {
    char ch = fpsText[i];
    int g = ch == '/' ? 10 : ch == 's' ? 11 : ch == '!' ? 12 : (ch >= '0' && ch <= '9') ? ch - '0' : -1;
    if (g < 0) continue;
    uint8_t bits = FPS_FONT[g][gy];
    for (int b = 0; b < 5; b++)
      if (bits & (0x10 >> b)) { int x = 1 + i * 6 + b; if (x < w) row[x] = fg; }
  }
}

static void fpsUpdate(int drawnPerSec, int emuPerSec) {
  if (!cfg.showFps) { fpsText[0] = 0; return; }
  // A ROM too big for flash reads 4 KB pages off the SD card as it plays; that is what
  // makes big games stutter, so show the rate next to the frame rate while it happens.
  // "!n" only appears when the display stopped answering and the emulator gave up on a
  // line rather than freeze with it; it should always be 0.
  char tail[8] = "";
  if (stallsPerSec > 0) snprintf(tail, sizeof tail, " !%d", stallsPerSec);
  if (sdPerSec >= 0) snprintf(fpsText, sizeof fpsText, "%d/%d s%d%s", drawnPerSec, emuPerSec, sdPerSec, tail);
  else snprintf(fpsText, sizeof fpsText, "%d/%d%s", drawnPerSec, emuPerSec, tail);
}

static inline uint16_t blend565(uint16_t a, uint16_t b) {
  return (uint16_t)((((a ^ b) & 0xF7DE) >> 1) + (a & b));
}

static inline void scaleRow(const uint16_t *src, uint16_t *dst, bool smooth) {
  for (int x = 0; x < 80; x++) {
    uint16_t a = src[0], b = src[1];
    dst[0] = a;
    dst[1] = smooth ? blend565(a, b) : a;
    dst[2] = b;
    src += 2; dst += 3;
  }
}

static inline void swapCopy(const uint16_t *s, uint16_t *d, int n) {
  for (int i = 0; i < n; i++) d[i] = __builtin_bswap16(s[i]);
}

// No frame buffer: the emulator hands finished lines over through gb_ring
// (16 lines). Wait for a line, expand it to RGB565, then release it.
static inline void waitLine(int line) {
  uint32_t t0 = 0;
  while (gb_lines_out <= line) {
    delayMicroseconds(10);
    if (++t0 > 20000) break;   // ~200 ms: emulator stopped mid-frame, don't hang
  }
}

// The other half of the handshake: the emulator calls this while it waits for the
// display to free a line. The emulator and the menu are the same task on this core,
// so it has to let the core breathe, or the board stops answering its buttons.
extern "C" void gb_ring_yield(void) { taskYIELD(); }

// Which emulator is feeding the display
enum { SYS_GB = 0, SYS_NES = 1 };
static volatile int vSys = SYS_GB;
static int srcW = 160, srcH = 144;            // source image size of the running system

static inline void expandLine(int line, uint16_t *out) {
  waitLine(line);
  if (vSys == SYS_NES) {
    const uint8_t *s = nes_ring + (line % nes_ring_lines) * NES_W;
    for (int x = 0; x < NES_W; x++) out[x] = nes_pal565[s[x]];
  } else {
    gb_ring_line_rgb(line, out);               // 1-byte colors -> RGB565 with that line's palette
  }
}

// Where the game image goes on screen (set by layoutGame(), read by the display task)
static int gX = 0, gY = 0, gW = 240, gH = 216;
static bool gControls = true;   // on-screen touch controls visible

static void drawFrame() {
  static uint16_t l0[NES_W], l1[NES_W], r0[240], r1[240];
  static uint8_t xmap[DMA_W];
  lcd.startWrite();
  if (vSys == SYS_GB && gW == 240 && gH == 216 && curScale() < 2) {
    // fast exact 1.5x path (sharp or smooth)
    bool smooth = curScale() == 1;
    lcd.setAddrWindow(gX, gY, 240, 216);
    // every 2 source lines -> 3 output lines; DMA_LINES / 3 such groups per chunk
    const int groups = DMA_LINES / 3;
    for (int c = 0; c < 216 / DMA_LINES; c++) {
      uint16_t *out = dmaBuf[c & 1];
      for (int p = 0; p < groups; p++) {
        int line = (c * groups + p) * 2;
        expandLine(line, l0);
        expandLine(line + 1, l1);
        gb_lines_need = line + 2;   // both lines copied out: the emulator may reuse them
        scaleRow(l0, r0, smooth);
        scaleRow(l1, r1, smooth);
        uint16_t *o = out + p * 3 * 240;
        swapCopy(r0, o, 240);
        if (smooth) {
          for (int i = 0; i < 240; i++) o[240 + i] = __builtin_bswap16(blend565(r0[i], r1[i]));
        } else {
          memcpy(o + 240, o, 480);
        }
        swapCopy(r1, o + 480, 240);
        int oy = (c * groups + p) * 3;
        if (oy < FPS_BOX_H) { fpsOverlay(o, oy, 240); fpsOverlay(o + 240, oy + 1, 240); fpsOverlay(o + 480, oy + 2, 240); }
      }
      lcd.writePixelsDMA(out, 240 * DMA_LINES, false);
    }
  } else {
    // generic nearest-neighbour scaler (1x, fit, stretch)
    for (int x = 0; x < gW; x++) xmap[x] = (uint8_t)((x * srcW) / gW);
    lcd.setAddrWindow(gX, gY, gW, gH);
    int lastLine = -1, chunk = 0;
    for (int y0 = 0; y0 < gH; y0 += DMA_LINES) {
      uint16_t *out = dmaBuf[chunk++ & 1];
      int rows = min(DMA_LINES, gH - y0);
      for (int k = 0; k < rows; k++) {
        int line = ((y0 + k) * srcH) / gH;
        uint16_t *o = out + k * gW;
        if (line == lastLine && k > 0) { memcpy(o, o - gW, gW * 2); fpsOverlay(o, y0 + k, gW); continue; }
        if (line != lastLine) {
          expandLine(line, l0);       // l0 keeps the line for repeated rows
          gb_lines_need = line + 1;
          lastLine = line;
        }
        for (int x = 0; x < gW; x++) o[x] = __builtin_bswap16(l0[xmap[x]]);
        fpsOverlay(o, y0 + k, gW);
      }
      lcd.writePixelsDMA(out, gW * rows, false);
    }
  }
  lcd.waitDMA();
  lcd.endWrite();
}

static void displayTask(void *) {
  for (;;) {
    xSemaphoreTake(frameReady, portMAX_DELAY);
    xSemaphoreTake(lcdMutex, portMAX_DELAY);
    drawFrame();
    xSemaphoreGive(lcdMutex);
    dispBusy = false;
  }
}

static void waitDisplayIdle() {
  while (dispBusy) vTaskDelay(1);
}

// ----------------------------------------------------------------------------
// On-screen controls (bottom 104 px)
// ----------------------------------------------------------------------------
#define PAD_RIGHT  0x01
#define PAD_LEFT   0x02
#define PAD_UP     0x04
#define PAD_DOWN   0x08
#define PAD_A      0x10
#define PAD_B      0x20
#define PAD_SELECT 0x40
#define PAD_START  0x80

// Where the pad sits. Portrait gives it a panel of its own below the game; landscape
// floats it over the picture, because a stretched game leaves no margin to put it in.
// Both layouts feed the same hit test and the same press handling.
static bool ctrlOverlay = false;
static int DPAD_CX = 52, DPAD_CY = 268, DPAD_ARM = 46;
static int BTN_A_X = 207, BTN_A_Y = 250;
static int BTN_B_X = 162, BTN_B_Y = 284;
static int BTN_R = 20;
static int MISC_X0 = 102, MISC_W = 32, MISC_GAP = 0, MISC_Y0 = 222;

static void layoutControls() {
  ctrlOverlay = (cfg.rotation & 1) != 0;
  if (!ctrlOverlay) {
    DPAD_CX = 52; DPAD_CY = 268; DPAD_ARM = 46;
    BTN_A_X = 207; BTN_A_Y = 250; BTN_B_X = 162; BTN_B_Y = 284; BTN_R = 20;
    MISC_X0 = 104; MISC_W = 32; MISC_GAP = 0; MISC_Y0 = 222;
  } else {
    // Held off the edges. A resistive panel reads worst in its last few millimetres,
    // and a fingertip that lands half over the rim is half outside the zone, so the
    // shapes sit well inboard and are drawn bigger than the portrait pad's.
    DPAD_ARM = 46; DPAD_CX = 64; DPAD_CY = SCR_H - 76;
    BTN_R = 20; BTN_A_X = SCR_W - 46; BTN_A_Y = SCR_H - 84;
    BTN_B_X = SCR_W - 80; BTN_B_Y = SCR_H - 48;
    MISC_W = 32; MISC_GAP = 4; MISC_Y0 = SCR_H - 28;
    MISC_X0 = SCR_W / 2 - (3 * MISC_W + 2 * MISC_GAP) / 2;
  }
}

// The pad drawn over a running game: outlines only, filled while held, so as little of
// the picture is covered as the shapes allow.
static void drawControlsOverlay(uint8_t pressed, bool menuDown) {
  const int a = DPAD_ARM / 3, len = DPAD_ARM;
  auto arm = [&](int x, int y, int w, int h, bool on) {
    if (on) lcd.fillRoundRect(x, y, w, h, 3, C_ACCENT);
    else    lcd.drawRoundRect(x, y, w, h, 3, C_TEXT);
  };
  arm(DPAD_CX - a, DPAD_CY - len, a * 2, len - a, pressed & PAD_UP);
  arm(DPAD_CX - a, DPAD_CY + a, a * 2, len - a, pressed & PAD_DOWN);
  arm(DPAD_CX - len, DPAD_CY - a, len - a, a * 2, pressed & PAD_LEFT);
  arm(DPAD_CX + a, DPAD_CY - a, len - a, a * 2, pressed & PAD_RIGHT);
  if (pressed & PAD_A) lcd.fillCircle(BTN_A_X, BTN_A_Y, BTN_R, C_ACCENT);
  else                 lcd.drawCircle(BTN_A_X, BTN_A_Y, BTN_R, C_TEXT);
  if (pressed & PAD_B) lcd.fillCircle(BTN_B_X, BTN_B_Y, BTN_R, C_ACCENT);
  else                 lcd.drawCircle(BTN_B_X, BTN_B_Y, BTN_R, C_TEXT);
  lcd.setFont(&fonts::Font0);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(C_TEXT);
  lcd.drawString("A", BTN_A_X, BTN_A_Y);
  lcd.drawString("B", BTN_B_X, BTN_B_Y);
  static const char *LBL[3] = { "MENU", "SEL", "START" };
  const bool on[3] = { menuDown, (pressed & PAD_SELECT) != 0, (pressed & PAD_START) != 0 };
  for (int i = 0; i < 3; i++) {
    int x = MISC_X0 + i * (MISC_W + MISC_GAP);
    if (on[i]) lcd.fillRoundRect(x, MISC_Y0, MISC_W, 14, 5, C_ACCENT);
    else       lcd.drawRoundRect(x, MISC_Y0, MISC_W, 14, 5, C_TEXT);
    lcd.drawString(LBL[i], x + MISC_W / 2, MISC_Y0 + 7);
  }
  lcd.setTextDatum(top_left);
}

static void drawControls(uint8_t pressed, bool menuDown) {
  if (ctrlOverlay && cfg.padLand == 1) return;   // zones only: the pad is felt, not seen
  xSemaphoreTake(lcdMutex, portMAX_DELAY);
  lcd.startWrite();
  if (ctrlOverlay) {
    drawControlsOverlay(pressed, menuDown);
    lcd.endWrite();
    xSemaphoreGive(lcdMutex);
    return;
  }
  lcd.fillRect(0, CTRL_Y, SCR_W, SCR_H - CTRL_Y, C_BG);
  lcd.drawFastHLine(0, CTRL_Y, SCR_W, C_PANEL);
  // D-pad
  const int a = 15, len = 46;
  auto seg = [&](int x, int y, int w, int h, bool on) { lcd.fillRoundRect(x, y, w, h, 4, on ? C_ACCENT : C_PANEL); };
  seg(DPAD_CX - a, DPAD_CY - len, a * 2, len - a + 1, pressed & PAD_UP);
  seg(DPAD_CX - a, DPAD_CY + a - 1, a * 2, len - a + 1, pressed & PAD_DOWN);
  seg(DPAD_CX - len, DPAD_CY - a, len - a + 1, a * 2, pressed & PAD_LEFT);
  seg(DPAD_CX + a - 1, DPAD_CY - a, len - a + 1, a * 2, pressed & PAD_RIGHT);
  lcd.fillRect(DPAD_CX - a, DPAD_CY - a, a * 2, a * 2, C_PANEL);
  // A / B
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(middle_center);
  lcd.fillCircle(BTN_A_X, BTN_A_Y, BTN_R, (pressed & PAD_A) ? C_ACCENT : RGB565(0x5A, 0x48, 0x34));
  lcd.fillCircle(BTN_B_X, BTN_B_Y, BTN_R, (pressed & PAD_B) ? C_ACCENT : RGB565(0x5A, 0x48, 0x34));
  lcd.setTextColor(C_TEXT);
  lcd.drawString("A", BTN_A_X, BTN_A_Y);
  lcd.drawString("B", BTN_B_X, BTN_B_Y);
  // center column: MENU / SELECT / START
  lcd.setFont(&fonts::Font0);
  lcd.fillRoundRect(104, 222, 32, 18, 5, menuDown ? C_ACCENT : C_PANEL);
  lcd.drawString("MENU", 120, 231);
  lcd.fillRoundRect(104, 248, 32, 18, 5, (pressed & PAD_SELECT) ? C_ACCENT : C_PANEL);
  lcd.drawString("SEL", 120, 257);
  lcd.fillRoundRect(104, 274, 32, 18, 5, (pressed & PAD_START) ? C_ACCENT : C_PANEL);
  lcd.drawString("START", 120, 283);
  lcd.setTextDatum(top_left);
  lcd.endWrite();
  xSemaphoreGive(lcdMutex);
}

enum { HIT_NONE = 0, HIT_MENU = 0x100 };

// Maps a touch point to pad bits (8-way d-pad, A+B on the seam between buttons)
static int touchToPad(int x, int y) {
  if (ctrlOverlay) {
    // Only the three corners answer; the rest of the picture stays a plain tap, so
    // tapping the middle of a game still opens the menu as it always did.
    if (y >= MISC_Y0 - 5) {
      int i = (x - MISC_X0) / (MISC_W + MISC_GAP);
      if (x >= MISC_X0 && i >= 0 && i < 3) return i == 0 ? HIT_MENU : (i == 1 ? PAD_SELECT : PAD_START);
    }
    if (x <= DPAD_CX + DPAD_ARM + 10 && y >= DPAD_CY - DPAD_ARM - 10) {
      int dx = x - DPAD_CX, dy = y - DPAD_CY;
      if (dx * dx + dy * dy < 7 * 7) return HIT_NONE;
      int p = 0;
      if (dy < 0 && -dy * 5 > abs(dx) * 2) p |= PAD_UP;
      if (dy > 0 && dy * 5 > abs(dx) * 2) p |= PAD_DOWN;
      if (dx < 0 && -dx * 5 > abs(dy) * 2) p |= PAD_LEFT;
      if (dx > 0 && dx * 5 > abs(dy) * 2) p |= PAD_RIGHT;
      return p;
    }
    if (x >= BTN_B_X - BTN_R - 10 && y >= BTN_A_Y - BTN_R - 10) {
      float fa = sqrtf((float)((x - BTN_A_X) * (x - BTN_A_X) + (y - BTN_A_Y) * (y - BTN_A_Y)));
      float fb = sqrtf((float)((x - BTN_B_X) * (x - BTN_B_X) + (y - BTN_B_Y) * (y - BTN_B_Y)));
      if (fabsf(fa - fb) < 8.0f && fa < BTN_R * 2 && fb < BTN_R * 2) return PAD_A | PAD_B;
      return fa < fb ? PAD_A : PAD_B;
    }
    return HIT_NONE;
  }
  if (y < CTRL_Y - 4) return HIT_NONE;
  if (x < 102) {
    int dx = x - DPAD_CX, dy = y - DPAD_CY;
    if (dx * dx + dy * dy < 8 * 8) return HIT_NONE;
    int p = 0;
    // 8 sectors: a direction is active when within ~67 deg of its axis
    if (dy < 0 && -dy * 5 > abs(dx) * 2) p |= PAD_UP;
    if (dy > 0 && dy * 5 > abs(dx) * 2) p |= PAD_DOWN;
    if (dx < 0 && -dx * 5 > abs(dy) * 2) p |= PAD_LEFT;
    if (dx > 0 && dx * 5 > abs(dy) * 2) p |= PAD_RIGHT;
    return p;
  }
  if (x < 140) {
    if (y < 244) return HIT_MENU;
    if (y < 270) return PAD_SELECT;
    return PAD_START;
  }
  int da = (x - BTN_A_X) * (x - BTN_A_X) + (y - BTN_A_Y) * (y - BTN_A_Y);
  int db = (x - BTN_B_X) * (x - BTN_B_X) + (y - BTN_B_Y) * (y - BTN_B_Y);
  float fa = sqrtf((float)da), fb = sqrtf((float)db);
  if (fabsf(fa - fb) < 9.0f && fa < 40 && fb < 40) return PAD_A | PAD_B;
  return fa < fb ? PAD_A : PAD_B;
}

// ----------------------------------------------------------------------------
// SD card / ROM list
// ----------------------------------------------------------------------------
static SPIClass sdSPI(VSPI);
static bool sdOk = false;
// Folder browser. Games live in /Roms (any sub-folders, e.g. /Roms/GBC/RPG);
// without a /Roms folder the browser starts at the card's root.
// Only the names are kept (packed in 4 KB blocks, ~8 bytes + the name per entry);
// paths are built from the current folder and file sizes are read only for the
// rows on screen. A folder lists until RAM gets low (thousands of games).
enum { K_GB = 0, K_NES = 1, K_DIR = 2, K_UP = 3, K_MORE = 4,
       K_CONT = 5,        // "Continue: <last game>" (name = the game's full path)
       K_VFAV = 6, K_VRECENT = 7, K_VMOST = 8 };   // the Favorites / Recent / Most played lists
struct RomEntry {            // the game being launched
  String path;
  uint32_t size;
  uint8_t kind;
  const char *name() const { return path.c_str() + path.lastIndexOf('/') + 1; }
};
#define SIZE_UNKNOWN 0xFFFFFFFFu
struct Ent {                 // one row of the folder list
  const char *name;          // in the Favorites / Recent / Most played lists: the full path
  uint32_t size;             // SIZE_UNKNOWN until the row is shown; Most played: seconds played
  uint8_t kind;
  uint8_t fav;               // marked as a favorite (shows a star); 0 when left out
};
static std::vector<Ent> roms;          // entries of the current folder
static std::vector<char *> nameBlocks; // packed names
static size_t nameUsed = 0;            // bytes used in the last block
#define NAME_BLOCK 4096
static String rootDir = "/";
static String curDir = "/";
static int romFiles = 0;               // games (not folders) in the current folder
static int hiddenCount = 0;            // entries left out because RAM ran low
#define LIST_MIN_FREE (40 * 1024)      // stop storing names below this much free RAM

static void clearList() {
  roms.clear();
  roms.shrink_to_fit();
  for (char *b : nameBlocks) free(b);
  nameBlocks.clear();
  nameUsed = NAME_BLOCK;
  romFiles = 0;
  hiddenCount = 0;
}

static const char *storeName(const String &n) {
  size_t len = n.length() + 1;
  if (len > NAME_BLOCK) return nullptr;
  if (nameBlocks.empty() || nameUsed + len > NAME_BLOCK) {
    char *b = (char *)malloc(NAME_BLOCK);
    if (!b) return nullptr;
    nameBlocks.push_back(b);
    nameUsed = 0;
  }
  char *d = nameBlocks.back() + nameUsed;
  memcpy(d, n.c_str(), len);
  nameUsed += len;
  return d;
}

// -1 = not a game; otherwise K_GB / K_NES
static int romKind(const char *n) {
  const char *dot = strrchr(n, '.');
  if (!dot) return -1;
  if (!strcasecmp(dot, ".gbc") || !strcasecmp(dot, ".gb") || !strcasecmp(dot, ".cgb")) return K_GB;
  if (!strcasecmp(dot, ".nes")) return K_NES;
  return -1;
}

static bool isCgbName(const char *n) {
  const char *dot = strrchr(n, '.');
  return dot && (!strcasecmp(dot, ".gbc") || !strcasecmp(dot, ".cgb"));
}

static bool hiddenDir(const String &n) {
  String l = n; l.toLowerCase();
  return l.startsWith(".") || l == "system volume information" || l == "saves" ||
         l == "cheats";   // /CHEATS: read by the emulator, never listed
}

#define VDIR_PREFIX "/:"   // the Favorites / Recent / Most played lists aren't real folders
static bool isVirtualDir(const String &d) { return d.startsWith(VDIR_PREFIX); }
static String parentDir(const String &d) {
  if (isVirtualDir(d)) return rootDir;
  int sl = d.lastIndexOf('/');
  return sl <= 0 ? String("/") : d.substring(0, sl);
}

static String childPath(const char *name) {
  String p = curDir;
  if (!p.endsWith("/")) p += "/";
  return p + name;
}

static String entryPath(const Ent &e) {
  if (e.kind == K_UP) return parentDir(curDir);
  if (e.kind >= K_VFAV) return String(VDIR_PREFIX) + e.name;
  if (e.kind == K_CONT || isVirtualDir(curDir)) return String(e.name);   // already a full path
  return childPath(e.name);
}

// Size of a game file. Sizes come with the folder listing; if one is missing it is NOT
// looked up (opening a file by name in a big folder is very slow on SD), just not shown.
static uint32_t entrySize(Ent &e) {
  return e.size;
}

// Adds one folder entry; returns false once RAM is too low to store more names.
static bool addEntry(const char *n, bool isDir, uint32_t size) {
  if (!n[0] || n[0] == '.') return true;
  int k = isDir ? (hiddenDir(String(n)) ? -1 : K_DIR) : romKind(n);
  if (k < 0) return true;
  if (!hiddenCount && heap_caps_get_free_size(MALLOC_CAP_8BIT) < LIST_MIN_FREE) hiddenCount = -1;
  const char *nm = hiddenCount ? nullptr : storeName(String(n));
  if (!nm) { hiddenCount = hiddenCount < 0 ? 1 : hiddenCount + 1; return false; }   // keep counting
  roms.push_back({ nm, isDir ? 0u : size, (uint8_t)k });
  if (k != K_DIR) romFiles++;
  return true;
}

static void listProgress(int seen, uint32_t t0) {
  if ((seen % 200) == 0 && millis() - t0 > 400) {
    char b[40];
    snprintf(b, sizeof b, "%d entries", seen);
    uiMessage("Reading folder...", b);
  }
}

// The SD library mounts the card as FatFs drive "N:"; found once by trying 0..3.
static int fatDrive = -1;

// Reads the folder straight from FatFs: names and sizes come from the directory
// entries in one pass, with no file opened. Returns false if FatFs isn't reachable.
static bool listDirFat(uint32_t t0) {
  FF_DIR dir;
  FILINFO fno;
  if (fatDrive < 0) {
    for (int d = 0; d < 4 && fatDrive < 0; d++) {
      String p = String(d) + ":" + curDir;
      if (f_opendir(&dir, p.c_str()) == FR_OK) { fatDrive = d; f_closedir(&dir); }
    }
    if (fatDrive < 0) return false;
  }
  String p = String(fatDrive) + ":" + curDir;
  if (f_opendir(&dir, p.c_str()) != FR_OK) return false;
  int seen = 0;
  while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
    if (fno.fattrib & (AM_HID | AM_SYS)) continue;   // e.g. System Volume Information
    addEntry(fno.fname, (fno.fattrib & AM_DIR) != 0, (uint32_t)fno.fsize);
    listProgress(++seen, t0);
  }
  f_closedir(&dir);
  return true;
}

// ----------------------------------------------------------------------------
// Play history and favorites: /gbc/history.txt on the SD card (survives reflashing).
// First line "#HNX history 1 <counter>", then one line per game:
//   <favorite 0/1> TAB <seconds played> TAB <counter when last started> TAB <full path>
// It's read and rewritten one line at a time, so it never needs much RAM, even mid-game.
// ----------------------------------------------------------------------------
#define HIST_PATH "/gbc/history.txt"
#define HIST_KEEP 300   // non-favorites played longer ago than the last 300 starts are dropped
struct HistLine { uint8_t fav; uint32_t secs, last; const char *path; };

// Calls fn for each game in the history. Returns the counter (0 if there's no file).
template <typename F> static uint32_t histForEach(F fn) {
  File f = SD.open(HIST_PATH);
  if (!f) return 0;
  static char line[300];
  uint32_t seq = 0;
  while (f.available()) {
    int n = f.readBytesUntil('\n', line, sizeof line - 1);
    line[n] = 0;
    if (line[0] == '#') { const char *c = strrchr(line, ' '); if (c) seq = strtoul(c + 1, nullptr, 10); continue; }
    char *a = strchr(line, '\t'), *b = a ? strchr(a + 1, '\t') : nullptr, *c = b ? strchr(b + 1, '\t') : nullptr;
    if (!c) continue;
    HistLine h = { (uint8_t)(line[0] == '1'), (uint32_t)strtoul(a + 1, nullptr, 10), (uint32_t)strtoul(b + 1, nullptr, 10), c + 1 };
    fn(h);
  }
  f.close();
  return seq;
}

enum { HIST_START, HIST_TIME, HIST_FAV };
// Updates one game: HIST_START (it's being played now), HIST_TIME (add secs played),
// HIST_FAV (switch favorite on/off). Returns the game's favorite state afterwards.
static bool histUpdate(const String &path, int op, uint32_t secs = 0) {
  SD.mkdir("/gbc");
  uint32_t seq = histForEach([](const HistLine &) {});
  if (op == HIST_START) seq++;
  File out = SD.open(HIST_PATH ".tmp", FILE_WRITE);
  if (!out) return false;
  out.printf("#HNX history 1 %u\n", (unsigned)seq);
  bool found = false, fav = false;
  histForEach([&](const HistLine &h) {
    HistLine e = h;
    if (!found && path == h.path) {
      found = true;
      if (op == HIST_START) e.last = seq;
      if (op == HIST_TIME) e.secs += secs;
      if (op == HIST_FAV) e.fav ^= 1;
      fav = e.fav;
    } else if (!e.fav && e.last + HIST_KEEP < seq) {
      return;   // not played for a long time: forget it
    }
    out.printf("%d\t%u\t%u\t%s\n", e.fav, (unsigned)e.secs, (unsigned)e.last, e.path);
  });
  if (!found) {
    fav = op == HIST_FAV;
    out.printf("%d\t%u\t%u\t%s\n", fav ? 1 : 0, (unsigned)(op == HIST_TIME ? secs : 0),
               (unsigned)(op == HIST_START ? seq : 0), path.c_str());
  }
  out.close();
  SD.remove(HIST_PATH);
  SD.rename(HIST_PATH ".tmp", HIST_PATH);
  return fav;
}

static String curGamePath;   // the game being played (for the pause menu's Favorite)

static String fileName(const char *path) { const char *s = strrchr(path, '/'); return String(s ? s + 1 : path); }

// Rows for the top of the main list: Continue, Favorites, Recent, Most played.
static void histRootRows() {
  uint32_t nFav = 0, nRec = 0, nMost = 0;
  struct R { String path; uint32_t last; };
  std::vector<R> recent;   // the 5 most recent, in case the latest one was deleted
  histForEach([&](const HistLine &h) {
    if (h.fav) nFav++;
    if (h.secs) nMost++;
    if (!h.last) return;
    nRec++;
    auto at = std::find_if(recent.begin(), recent.end(), [&](const R &r) { return h.last > r.last; });
    recent.insert(at, R{ h.path, h.last });
    if (recent.size() > 5) recent.pop_back();
  });
  std::vector<Ent> top;
  for (auto &r : recent)
    if (SD.exists(r.path)) {
      if (const char *n = storeName(r.path)) top.push_back({ n, SIZE_UNKNOWN, K_CONT });
      break;
    }
  if (nFav) top.push_back({ "Favorites", nFav, K_VFAV });
  if (nRec) top.push_back({ "Recent", nRec, K_VRECENT });
  if (nMost) top.push_back({ "Most played", nMost, K_VMOST });
  roms.insert(roms.begin(), top.begin(), top.end());
}

// Fills the Favorites / Recent / Most played list (curDir is "/:<name>").
static void histList() {
  const String which = curDir.substring(2);
  const int K = 15;
  struct Pick { String path; uint32_t key; uint8_t fav; };
  std::vector<Pick> picks;
  histForEach([&](const HistLine &h) {
    if (which == "Favorites") { if (h.fav) picks.push_back({ h.path, 0, 1 }); return; }
    uint32_t key = which == "Recent" ? h.last : h.secs;
    if (!key) return;
    if ((int)picks.size() == K && key <= picks.back().key) return;   // keep only the top K
    Pick p = { h.path, key, h.fav };
    auto at = std::find_if(picks.begin(), picks.end(), [&](const Pick &q) { return key > q.key; });
    picks.insert(at, p);
    if ((int)picks.size() > K) picks.pop_back();
  });
  if (which == "Favorites")
    std::sort(picks.begin(), picks.end(), [](const Pick &a, const Pick &b) {
      return strcasecmp(fileName(a.path.c_str()).c_str(), fileName(b.path.c_str()).c_str()) < 0; });
  for (auto &p : picks) {
    int k = romKind(p.path.c_str());
    if (k < 0 || !SD.exists(p.path)) continue;   // moved or deleted: skip it
    const char *n = storeName(p.path);
    if (!n) break;
    roms.push_back({ n, which == "Most played" ? p.key : SIZE_UNKNOWN, (uint8_t)k, p.fav });
    romFiles++;
  }
}

// Stars on the favorites in a normal folder.
static void histMarkFavorites() {
  histForEach([&](const HistLine &h) {
    if (!h.fav || parentDir(h.path) != curDir) return;
    String n = fileName(h.path);
    for (auto &e : roms) if (e.kind <= K_NES && n == e.name) { e.fav = 1; break; }
  });
}

static void listDir() {
  clearList();
  if (isVirtualDir(curDir)) {   // Favorites / Recent / Most played
    roms.push_back({ "..", 0, K_UP });
    histList();
    return;
  }
  if (curDir != rootDir) roms.push_back({ "..", 0, K_UP });
  if (!SD.exists(curDir)) curDir = rootDir;   // folder vanished (card swapped): back to the top
  if (curDir == rootDir) roms.clear();
  uint32_t t0 = millis();
  if (!listDirFat(t0)) {
    // fallback: names only, through the Arduino SD library (sizes not shown)
    File d = SD.open(curDir);
    if (!d) return;
    int seen = 0;
    while (true) {
      bool isDir = false;
      String n = d.getNextFileName(&isDir);
      if (!n.length()) break;
      int sl = n.lastIndexOf('/');
      if (sl >= 0) n = n.substring(sl + 1);
      addEntry(n.c_str(), isDir, SIZE_UNKNOWN);
      listProgress(++seen, t0);
    }
    d.close();
  }
  if (hiddenCount < 0) hiddenCount = 0;
  // "back" first, then folders, then games, each A-Z
  std::sort(roms.begin(), roms.end(), [](const Ent &a, const Ent &b) {
    int ga = a.kind == K_UP ? 0 : a.kind == K_DIR ? 1 : 2;
    int gb = b.kind == K_UP ? 0 : b.kind == K_DIR ? 1 : 2;
    if (ga != gb) return ga < gb;
    return strcasecmp(a.name, b.name) < 0;
  });
  if (hiddenCount) roms.push_back({ "", 0, K_MORE });
  if (curDir == rootDir) histRootRows();   // Continue / Favorites / Recent / Most played on top
  else histMarkFavorites();
  Serial.printf("Folder %s: %d entries (%d not shown) in %u ms, heap free %u\n", curDir.c_str(), (int)roms.size(),
                hiddenCount, (unsigned)(millis() - t0), heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

static bool sdInit() {
  sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  // 40 MHz roughly halves SD read time (matters for big ROMs streamed from the card);
  // cards that can't do it fall back to 25 / 10 MHz.
  const uint32_t speeds[] = { 40000000, 25000000, 10000000 };
  for (uint32_t hz : speeds) {
    sdOk = SD.begin(PIN_SD_CS, sdSPI, hz, "/sd", 3);
    if (sdOk) { Serial.printf("SD card at %u MHz\n", (unsigned)(hz / 1000000)); break; }
    SD.end();
  }
  return sdOk;
}

// True if a folder (or its subfolders, `depth` levels down) holds at least one game.
static bool fatDirHasGames(const String &path, int depth) {
  FF_DIR dir;
  FILINFO fno;
  String p = String(fatDrive) + ":" + path;
  if (f_opendir(&dir, p.c_str()) != FR_OK) return false;
  bool found = false;
  while (!found && f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
    if ((fno.fattrib & (AM_HID | AM_SYS)) || fno.fname[0] == '.') continue;
    if (fno.fattrib & AM_DIR) {
      if (depth > 0 && !hiddenDir(String(fno.fname)))
        found = fatDirHasGames(path + (path.endsWith("/") ? "" : "/") + fno.fname, depth - 1);
    } else if (romKind(fno.fname) >= 0) {
      found = true;
    }
  }
  f_closedir(&dir);
  return found;
}

// Games anywhere on the card outside /Roms (top-level game files, or folders other than
// /Roms with games in them, up to 3 levels deep)? The firmware's own /gbc folder only
// counts if someone put games in it.
static bool gamesOutsideRoms() {
  if (fatDrive < 0) {
    FF_DIR dir;
    for (int d = 0; d < 4 && fatDrive < 0; d++) {
      String p = String(d) + ":/";
      if (f_opendir(&dir, p.c_str()) == FR_OK) { fatDrive = d; f_closedir(&dir); }
    }
    if (fatDrive < 0) return false;
  }
  FF_DIR dir;
  FILINFO fno;
  String p = String(fatDrive) + ":/";
  if (f_opendir(&dir, p.c_str()) != FR_OK) return false;
  bool found = false;
  while (!found && f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
    if ((fno.fattrib & (AM_HID | AM_SYS)) || fno.fname[0] == '.') continue;
    if (fno.fattrib & AM_DIR) {
      if (strcasecmp(fno.fname, "Roms") && !hiddenDir(String(fno.fname)))
        found = fatDirHasGames(String("/") + fno.fname, 2);
    } else if (romKind(fno.fname) >= 0) {
      found = true;
    }
  }
  f_closedir(&dir);
  return found;
}

// (Re)reads the card: finds the games root and lists the current folder.
// The root is /Roms only when every game on the card is inside it; otherwise it is the
// top of the card, so games kept elsewhere never disappear when /Roms gets created
// (the web page's USB transfer makes /Roms/GB, /Roms/GBC and /Roms/NES).
static void scanRoms() {
  String root = (SD.exists("/Roms") && !gamesOutsideRoms()) ? String("/Roms") : String("/");
  if (root != rootDir) { rootDir = root; curDir = root; }
  if (!curDir.startsWith(rootDir)) curDir = rootDir;
  listDir();
}

static String baseName(const String &n) {
  int dot = n.lastIndexOf('.');
  return dot > 0 ? n.substring(0, dot) : n;
}

// ----------------------------------------------------------------------------
// ROM -> flash partition
// ----------------------------------------------------------------------------
static const esp_partition_t *romPart = nullptr;
static spi_flash_mmap_handle_t romMmap = 0;
static const uint8_t *romPtr = nullptr;
#define IOBUF_SIZE 4096
struct TempBuf {            // freed automatically on every return path
  uint8_t *p;
  TempBuf(size_t n) : p((uint8_t *)malloc(n)) {}
  ~TempBuf() { free(p); }
};

static uint32_t romHeaderSize(const uint8_t *hdr) {
  uint8_t c = hdr[0x148];
  if (c <= 8) return 32768u << c;
  if (c == 0x52) return 72 * 16384;
  if (c == 0x53) return 80 * 16384;
  if (c == 0x54) return 96 * 16384;
  return 0;
}

// Returns mapped length or 0 on failure. msg receives an error string.
// Bytes of the current ROM that are in flash; anything past this is streamed from SD.
static uint32_t romFlashBytes = 0;
static File romFile;   // kept open while a streamed ROM is running

extern "C" int gb_rom_read(unsigned offset, uint8_t *buf, int len) {
  if (!romFile) return -1;
  return (romFile.seek(offset) && romFile.read(buf, len) == len) ? 0 : -1;
}

// ---- big ROMs: learned flash layout ----
// For a ROM bigger than the flash partition, romLayout[slot] = the bank stored in
// that 16 KB flash slot (slot 0 is always bank 0). bankUse[] counts how busy each
// bank was (saved in /gbc/saves/<name>.hot); on the next launch the busiest banks
// are moved into flash, so fewer reads come from the SD card.
static short *romLayout = nullptr;
static int romLayoutSlots = 0;
static uint32_t *bankUse = nullptr;
static uint8_t **bankMap = nullptr;
static int romBanks = 0;
static String hotPath;

static void freeBigRom() {
  free(romLayout); romLayout = nullptr; romLayoutSlots = 0;
  free(bankUse); bankUse = nullptr;
  free(bankMap); bankMap = nullptr;
  romBanks = 0;
}

static void saveBankUse() {
  if (!bankUse || !hotPath.length()) return;
  File f = SD.open(hotPath, FILE_WRITE);
  if (f) { f.write((const uint8_t *)bankUse, romBanks * 4); f.close(); }
}

// Copy one 16 KB bank from the ROM file into a flash slot and verify it.
static bool writeSlot(File &f, int slot, int bank, uint8_t *ioBuf, String &msg) {
  static uint8_t vbuf[512];
  uint32_t dst = (uint32_t)slot * 16384;
  if (esp_partition_erase_range(romPart, dst, 16384) != ESP_OK) { msg = "Flash erase failed"; return false; }
  for (uint32_t o = 0; o < 16384; o += IOBUF_SIZE) {
    memset(ioBuf, 0xFF, IOBUF_SIZE);
    if (!f.seek((uint32_t)bank * 16384 + o)) { msg = "SD seek error"; return false; }
    f.read(ioBuf, IOBUF_SIZE);
    if (esp_partition_write(romPart, dst + o, ioBuf, IOBUF_SIZE) != ESP_OK) { msg = "Flash write failed"; return false; }
    loaderTick();
    for (uint32_t v = 0; v < IOBUF_SIZE; v += sizeof vbuf) {
      esp_partition_read(romPart, dst + o + v, vbuf, sizeof vbuf);
      if (memcmp(vbuf, ioBuf + v, sizeof vbuf) != 0) { msg = "Flash verify failed"; return false; }
    }
  }
  return true;
}

static uint32_t loadRomToFlash(const RomEntry &r, String &msg) {
  if (!romPart) { msg = "No 'rom' flash partition"; return 0; }
  TempBuf tmp(IOBUF_SIZE);
  uint8_t *ioBuf = tmp.p;
  if (!ioBuf) { msg = "Out of memory"; return 0; }
  File f = SD.open(r.path);
  if (!f) { msg = "Cannot open file"; return 0; }
  uint32_t size = f.size();
  if (size < 0x150) { msg = "File too small"; f.close(); return 0; }
  f.read(ioBuf, 0x150);
  bool nes = r.kind == K_NES;
  uint32_t hsize = nes ? 0 : romHeaderSize(ioBuf);
  uint16_t gchk;
  uint8_t hchk;
  if (nes) {
    // NES: the whole file goes to flash (no streaming); a hash of the start is the cache check
    if (memcmp(ioBuf, "NES\x1a", 4) != 0) { msg = "Not an iNES (.nes) file"; f.close(); return 0; }
    if (size > romPart->size) {
      msg = "ROM too big (" + String(size / 1024) + "KB > " + String(romPart->size / 1024) + "KB)";
      f.close(); return 0;
    }
    uint16_t h = 0; uint8_t x = 0;
    for (int i = 0; i < 0x150; i++) { h = (uint16_t)(h * 31 + ioBuf[i]); x ^= ioBuf[i]; }
    gchk = h; hchk = x;
  } else {
    gchk = (ioBuf[0x14E] << 8) | ioBuf[0x14F];
    hchk = ioBuf[0x14D];
  }
  uint32_t need = max(size, hsize);
  if (need > 8u * 1024 * 1024) {
    msg = "ROM too big (" + String(need / 1024) + "KB > 8192KB)";
    f.close(); return 0;
  }
  // ROMs bigger than the flash partition: the first part goes to flash, the
  // remaining banks are streamed from the SD card while playing.
  uint32_t partBanks = romPart->size / 16384;
  romFlashBytes = need > romPart->size ? partBanks * 16384 : need;
  uint32_t copyLen = min(size, romFlashBytes);
  uint32_t mapLen = (romFlashBytes + 0xFFFF) & ~0xFFFFu;
  if (mapLen > romPart->size) mapLen = romPart->size;
  bool big = romFlashBytes < need;

  prefs.begin("gbc", false);
  bool cached = prefs.getString("rpath", "") == r.path && prefs.getUInt("rsize", 0) == size &&
                prefs.getUInt("roff", 0) == romPart->address &&
                prefs.getUShort("rgchk", 0xFFFF) == gchk && prefs.getUChar("rhchk", 0) == hchk;
  prefs.end();

  if (big) {
    // ---- big ROM: choose which banks go to flash ----
    freeBigRom();
    romBanks = (int)((need + 16383) / 16384);
    romLayoutSlots = (int)partBanks;
    romLayout = (short *)malloc(romLayoutSlots * sizeof(short));
    short *cur = (short *)malloc(romLayoutSlots * sizeof(short));
    bankUse = (uint32_t *)calloc(romBanks, 4);
    if (!romLayout || !cur || !bankUse) { free(cur); freeBigRom(); msg = "Out of memory"; f.close(); return 0; }
    String base = String(r.name());
    int dot = base.lastIndexOf('.');
    if (dot > 0) base = base.substring(0, dot);
    // Learned layouts (v2.0) are switched off: they made big games slower on real
    // hardware. Big ROMs always keep their first banks in flash, as in v1.9.
    hotPath = "";
    bool haveUse = false;
    // current layout of the cached copy (older versions: first banks in order)
    bool haveCur = false;
    if (cached) {
      prefs.begin("gbc", true);
      haveCur = prefs.getBytesLength("rlay") == (size_t)romLayoutSlots * sizeof(short) &&
                prefs.getBytes("rlay", cur, romLayoutSlots * sizeof(short)) == (size_t)romLayoutSlots * sizeof(short);
      prefs.end();
      if (!haveCur) { for (int i = 0; i < romLayoutSlots; i++) cur[i] = (short)i; haveCur = true; }
      // a layout rearranged by v2.0's learning: copy the ROM again in plain order
      for (int i = 0; i < romLayoutSlots; i++)
        if (cur[i] != i) { cached = false; haveCur = false; break; }
    }
    int changes;
    if (haveCur) changes = haveUse ? gb_plan_layout(bankUse, romBanks, romLayoutSlots, cur, romLayout) : 0;
    else if (haveUse) changes = gb_plan_layout(bankUse, romBanks, romLayoutSlots, nullptr, romLayout);
    else { for (int i = 0; i < romLayoutSlots; i++) romLayout[i] = (short)i; changes = romLayoutSlots; }
    if (haveCur && changes <= 0) memcpy(romLayout, cur, romLayoutSlots * sizeof(short));

    if (!cached || changes > 0) {
      prefs.begin("gbc", false);
      prefs.remove("rpath");          // an interrupted copy forces a full one next time
      prefs.end();
      loaderBegin(cached ? "Optimizing game" : "Loading game", baseName(String(r.name())),
                  cached ? "Moving the busiest parts into flash" : "Big ROM: part stays on the SD card",
                  cached ? "(learned from your last session)" : "(gets smoother as you play)");
      int todo = 0, done = 0;
      for (int s2 = 0; s2 < romLayoutSlots; s2++) if (!cached || romLayout[s2] != cur[s2]) todo++;
      for (int s2 = 0; s2 < romLayoutSlots; s2++) {
        if (cached && romLayout[s2] == cur[s2]) continue;
        if (!writeSlot(f, s2, romLayout[s2], ioBuf, msg)) { free(cur); freeBigRom(); f.close(); loaderEnd(); return 0; }
        done++;
        if ((done & 3) == 0 || done == todo) loaderProgress(cached ? "Optimizing" : "Copying", done * 100 / max(1, todo));
      }
      loaderEnd();
      prefs.begin("gbc", false);
      prefs.putBytes("rlay", romLayout, romLayoutSlots * sizeof(short));
      prefs.putString("rpath", r.path);
      prefs.putUInt("rsize", size);
      prefs.putUShort("rgchk", gchk);
      prefs.putUChar("rhchk", hchk);
      prefs.putUInt("roff", romPart->address);
      prefs.end();
      Serial.printf("Big ROM layout: %d slots rewritten\n", todo);
    }
    free(cur);
    cached = true;   // flash now holds romLayout
  } else {
    freeBigRom();
  }

  if (!cached) {
    prefs.begin("gbc", false);
    prefs.remove("rpath");
    prefs.end();
    if (romFlashBytes < need)
      loaderBegin("Loading game", baseName(String(r.name())), "Big ROM: part stays on the SD card", "(may pause briefly while playing)");
    else
      loaderBegin("Loading game", baseName(String(r.name())), "First launch copies the game to flash", "(next launch is instant)");
    loaderProgress("Copying", 0);

    f.seek(0);
    const uint32_t BLK = 65536;
    uint32_t total = (copyLen + 4095) & ~4095u;
    for (uint32_t off = 0; off < total; off += BLK) {
      uint32_t blk = min(BLK, total - off);
      if (esp_partition_erase_range(romPart, off, (blk + 4095) & ~4095u) != ESP_OK) {
        msg = "Flash erase failed"; f.close(); loaderEnd(); return 0;
      }
      for (uint32_t o = 0; o < blk; o += IOBUF_SIZE) {
        memset(ioBuf, 0xFF, IOBUF_SIZE);
        int n = f.read(ioBuf, IOBUF_SIZE);
        if (n <= 0 && off + o < copyLen) { msg = "SD read error"; f.close(); loaderEnd(); return 0; }
        if (esp_partition_write(romPart, off + o, ioBuf, IOBUF_SIZE) != ESP_OK) {
          msg = "Flash write failed"; f.close(); loaderEnd(); return 0;
        }
        loaderTick();
      }
      loaderProgress("Copying", (int)((uint64_t)(off + blk) * 100 / total));
    }
    // verify a few spots against the file
    static uint8_t vbuf[512];
    for (uint32_t off = 0; off < copyLen; off += copyLen / 8 + 1) {
      f.seek(off);
      int n = f.read(ioBuf, 512);
      esp_partition_read(romPart, off, vbuf, 512);
      if (n > 0 && memcmp(ioBuf, vbuf, n) != 0) { msg = "Flash verify failed"; f.close(); loaderEnd(); return 0; }
    }
    loaderEnd();
    prefs.begin("gbc", false);
    prefs.putString("rpath", r.path);
    prefs.putUInt("rsize", size);
    prefs.putUShort("rgchk", gchk);
    prefs.putUChar("rhchk", hchk);
    prefs.putUInt("roff", romPart->address);
    prefs.end();
  }
  f.close();

  const void *p;
  if (esp_partition_mmap(romPart, 0, mapLen, SPI_FLASH_MMAP_DATA, &p, &romMmap) != ESP_OK) {
    msg = "Flash mmap failed (" + String(mapLen / 1024) + "KB)";
    return 0;
  }
  romPtr = (const uint8_t *)p;
  return size;
}

static void unmapRom() {
  if (romMmap) { spi_flash_munmap(romMmap); romMmap = 0; romPtr = nullptr; }
}

// ----------------------------------------------------------------------------
// Battery saves
// ----------------------------------------------------------------------------
static String savePath, rtcPath;
static String swapPath;               // file the cartridge RAM pages live in
#define SWAP_TMP "/gbc/saves/_swap.tmp" // games without a battery: scratch file

// Cartridge RAM page I/O (called from the emulator core, see gb_sram_fault)
extern "C" int gb_swap_read(int page, uint8_t *buf) {
  File f = SD.open(swapPath, FILE_READ);
  if (!f) return -1;
  int ok = f.size() >= (size_t)(page + 1) * 4096 && f.seek((size_t)page * 4096) && f.read(buf, 4096) == 4096;
  f.close();
  return ok ? 0 : -1;
}

extern "C" int gb_swap_write(int page, const uint8_t *buf) {
  File f = SD.open(swapPath, "r+");
  if (!f) return -1;
  int ok = f.seek((size_t)page * 4096) && f.write(buf, 4096) == 4096;
  f.close();
  return ok ? 0 : -1;
}

// Make sure the file exists and is at least `bytes` long (pads with 0xFF).
static bool ensureSwapFile(const String &path, size_t bytes, bool fresh) {
  size_t sz = 0;
  if (!fresh) {
    File f = SD.open(path, FILE_READ);
    if (f) { sz = f.size(); f.close(); }
  }
  if (!fresh && sz >= bytes) return true;
  File w = SD.open(path, (fresh || sz == 0) ? FILE_WRITE : FILE_APPEND);
  if (!w) return false;
  uint8_t pad[256];
  memset(pad, 0xFF, sizeof pad);
  if (fresh) sz = 0;
  while (sz < bytes) {
    size_t n = min(sizeof pad, bytes - sz);
    if (w.write(pad, n) != n) { w.close(); return false; }
    sz += n;
  }
  w.close();
  return true;
}

static void sramLoad() {
  // cartridge RAM pages are loaded by gb_sram_setup(); only the clock is read here
  if (gb_has_rtc()) {
    File f = SD.open(rtcPath, FILE_READ);
    if (f) { if ((int)f.size() == gb_rtc_size()) f.read((uint8_t *)gb_rtc_ptr(), gb_rtc_size()); f.close(); }
  }
  gb_sram_clear_dirty();
}

static bool sramSave() {
  bool ok = true;
  if (gb_sram_size() > 0) ok = gb_sram_flush() == 0;   // writes only changed 4 KB pages
  if (gb_has_rtc()) {
    File f = SD.open(rtcPath, FILE_WRITE);
    if (f) { f.write((uint8_t *)gb_rtc_ptr(), gb_rtc_size()); f.close(); }
  }
  if (ok) gb_sram_clear_dirty();
  if (ok && (gb_sram_size() > 0 || gb_has_rtc())) ledFlash(LED_FLASH_SAVE);
  saveBankUse();   // big ROMs: remember which banks were busy
  return ok;
}

// Sets up cartridge RAM: as many 4 KB pages in RAM as fit, the rest paged from SD.
// Returns false if not even one page fits.
// RAM kept free while playing: opening the save file (~4.5 KB file cache),
// the pause menu and Bluetooth reconnects.
#define PLAY_RESERVE (8 * 1024)

static bool setupCartRam(int capSlots, size_t reserve) {
  int pages = gb_sram_pages();
  if (pages <= 0) return true;
  const size_t RESERVE = reserve;
  size_t freeB = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  int maxSlots = freeB > RESERVE ? (int)((freeB - RESERVE) / (4096 + 32)) : 0;
  if (maxSlots > pages) maxSlots = pages;
  if (maxSlots > capSlots) maxSlots = capSlots;
  bool paged = maxSlots < pages;
  bool fromFile;
  if (gb_sram_has_battery()) {
    swapPath = savePath;
    if (!ensureSwapFile(savePath, (size_t)pages * 4096, false)) return false;
    fromFile = true;
  } else {
    swapPath = SWAP_TMP;
    if (paged && !ensureSwapFile(swapPath, (size_t)pages * 4096, true)) return false;
    fromFile = false;   // no battery: RAM starts blank
  }
  int need = min(capSlots < 2 ? 1 : 2, pages);
  if (maxSlots < need) maxSlots = need;
  int got = gb_sram_setup(maxSlots, fromFile);
  Serial.printf("Cart RAM: %d pages, %d in RAM%s\n", pages, got, got < pages ? " (rest paged from SD)" : "");
  return got >= need;
}

// ----------------------------------------------------------------------------
// Menus
// ----------------------------------------------------------------------------
struct MenuItem { String label; String value; };
static bool menuByPad = false;   // the menus were last used with the controller (not touch)

// Draws a list of items; returns the index tapped (or -1 if header button tapped)
// Draws a list; returns the index chosen, -1 for the header button (or B on a
// controller), -3 for the controller's system button (or B when there is no header button).
// selState keeps the controller highlight between calls.
static int runList(const char *title, const char *headerBtn, std::vector<MenuItem> &items, int *selState = nullptr) {
  if (!headerBtn) headerBtn = "Back";   // every list gets a touch Back button
  const int y0 = 36;
  const int n = (int)items.size();
  const int avail = SCR_H - y0;
  // rows shrink to fit; below 26 px the list scrolls instead (drag, or the d-pad)
  int pitch = min(34, avail / max(1, n));
  const bool scroll = pitch < 26;
  if (scroll) pitch = 28;
  const int visible = scroll ? avail / pitch : n;
  const int rowH = pitch - 2;
  int sel = selState ? *selState : -1;
  if (sel >= n) sel = n - 1;
  int top = 0;
  auto keepVisible = [&]() {
    if (!scroll || sel < 0) return false;
    int t = top;
    if (sel < top) top = sel;
    if (sel >= top + visible) top = sel - visible + 1;
    return t != top;
  };
  keepVisible();
  auto drawRow = [&](int i) {
    if (i < top || i >= top + visible || i >= n) return;
    int y = y0 + (i - top) * pitch;
    bool hi = i == sel;
    uint16_t bg = hi ? C_HILITE : C_PANEL;
    const int rw = SCR_W - (scroll ? 14 : 12), yc = y + rowH / 2;
    if (!hi && selGlow.on && selGlow.y == y) { glowOff(); marqueeOff(); }
    if (!hi && marq.on && marq.yc == yc) marqueeOff();
    lcd.fillRoundRect(6, y, rw, rowH, 6, bg);
    lcd.drawRoundRect(5, y - 1, rw + 2, rowH + 2, 6, C_BG);   // clears an old halo
    if (hi) { lcd.drawRoundRect(6, y, rw, rowH, 6, C_ACCENT); glowSet(6, y, rw, rowH, bg); }
    lcd.setFont(&fonts::Font2);
    int lx1 = SCR_W - (scroll ? 16 : 14) - 4;
    if (items[i].value.length()) {
      lcd.setTextColor(C_ACCENT, bg);
      lcd.setTextDatum(middle_right);
      lcd.drawString(items[i].value, SCR_W - (scroll ? 16 : 14), yc);
      lx1 -= lcd.textWidth(items[i].value) + 6;
    }
    // a label too long for the row is cut off; the selected one scrolls
    if (hi && menuByPad && marqueeSet(14, lx1, yc, items[i].label, C_TEXT, bg)) {
      marqueeDraw(0);
    } else {
      String l = items[i].label;
      if (lcd.textWidth(l) > lx1 - 14) {   // cut off: end it with "~"
        while (l.length() > 1 && lcd.textWidth(l + "~") > lx1 - 14) l.remove(l.length() - 1);
        l += "~";
      }
      lcd.setTextColor(C_TEXT, bg);
      lcd.setTextDatum(middle_left);
      lcd.drawString(l, 14, yc);
    }
    lcd.setTextDatum(top_left);
  };
  // with touch (or no row selected), cut-off labels take turns scrolling
  int marqNext = 0, marqRow = -1;
  auto marqueeRotate = [&]() {
    if ((sel >= 0 && menuByPad) || (marq.on && marq.loops == 0)) return;
    for (int k = 0; k < visible; k++) {
      int i = top + (marqNext + k) % visible;
      if (i >= n || i == marqRow) continue;
      int y = y0 + (i - top) * pitch, lx1 = SCR_W - (scroll ? 16 : 14) - 4;
      lcd.setFont(&fonts::Font2);
      if (items[i].value.length()) lx1 -= lcd.textWidth(items[i].value) + 6;
      if (lcd.textWidth(items[i].label) <= lx1 - 14) continue;
      if (marqRow >= 0) { int r = marqRow; marqRow = -1; marqueeOff(); drawRow(r); }   // back to its "~" form
      marqueeSet(14, lx1, y + rowH / 2, items[i].label, C_TEXT, i == sel ? C_HILITE : C_PANEL);
      marqRow = i;
      marqNext = (marqNext + k + 1) % visible;
      return;
    }
    if (marqRow >= 0 && marq.on) marq.loops = 0;   // the only long label: it scrolls again
  };
  auto drawAll = [&]() {
    glowOff(); marqueeOff(); marqRow = -1;
    lcd.fillRect(0, y0, SCR_W, avail, C_BG);
    for (int i = top; i < top + visible && i < n; i++) drawRow(i);
    if (scroll) {
      int h = max(10, avail * visible / n);
      int yb = y0 + (avail - h) * top / max(1, n - visible);
      lcd.fillRect(SCR_W - 4, y0, 3, avail, C_BG);
      lcd.fillRect(SCR_W - 4, yb, 3, h, C_ACCENT);
    }
  };
  lcd.fillScreen(C_BG);
  glowOff(); marqueeOff();
  uiHeader(title, headerBtn);
  headerHints(title, SCR_W - 68, "OK", nullptr);   // Back has its own button (with its symbol)
  drawAll();
  btNavReset();
  int x, y;
  while (readTouch(x, y)) delay(10);
  auto finish = [&](int r) { glowOff(); marqueeOff(); hdrOn = false; if (selState) *selState = sel; return r; };
  while (true) {
    if (readTouch(x, y)) {
      int sx = x, sy = y, moved = 0, topAtStart = top;
      // Light the row under the finger while it is down, so what a release would choose
      // is visible before it happens. Sliding away - which turns the press into a scroll
      // - puts the light back where it was, so the way out of a wrong row is to move off
      // it rather than to have picked it already.
      int selWas = sel, press = -1;
      if (sy >= y0) {
        int i = top + (sy - y0) / pitch;
        if (i >= 0 && i < n && i < top + visible) { press = i; sel = i; drawRow(i); }
      }
      Drag dg; dragBegin(dg, y);
      while (dragNext(dg, x, y)) {
        moved = max(moved, abs(y - sy));
        if (press >= 0 && moved > 10) {        // it became a scroll: let the row go
          int was = press; press = -1; sel = selWas;
          drawRow(was);
          if (selWas >= 0 && selWas != was) drawRow(selWas);
        }
        if (scroll && moved > 10 && sy >= y0) {
          int nt = constrain(topAtStart - (y - sy) / (pitch / 2), 0, max(0, n - visible));
          if (nt != top) { top = nt; drawAll(); }
        }
        delay(15);
      }
      menuByPad = false;
      if (moved > 10) continue;
      if (sy < 32 && sx > SCR_W - 70) return finish(-1);
      if (sy >= y0) {
        int i = top + (sy - y0) / pitch;
        if (i >= 0 && i < n && i < top + visible) { sel = i; return finish(i); }
      }
      continue;
    }
    int navEv = btNav();
    if (navEv != NAV_NONE) { menuByPad = true; idlePoke(); }
    switch (navEv) {
      case NAV_UP:   { int o = sel; sel = sel <= 0 ? n - 1 : sel - 1; if (keepVisible()) drawAll(); else { drawRow(o); drawRow(sel); } break; }
      case NAV_DOWN: { int o = sel; sel = (sel < 0 || sel >= n - 1) ? 0 : sel + 1; if (keepVisible()) drawAll(); else { drawRow(o); drawRow(sel); } break; }
      case NAV_OK:   if (sel < 0) { sel = top; drawRow(sel); } else return finish(sel); break;
      case NAV_BACK: return finish(-1);
      case NAV_SYS:  return finish(-3);
      default: break;
    }
    marqueeRotate();
    uiTick();
    if (screensaverIfIdle()) {
      lcd.fillScreen(C_BG);
      uiHeader(title, headerBtn);
      headerHints(title, SCR_W - 68, "OK", nullptr);
      drawAll();
    }
    delay(10);
  }
}

#if TOUCH_KIND == TOUCH_XPT_BUS
// ---- touch wiring probe (diagnostic) --------------------------------------
// GPIO36 going low under a press proved the XPT2046 is on this board and watching the
// panel: that pin is its pen-interrupt output, and the chip is there on the back of a
// Solo Miner board, below the SD slot. The connectors account for the rest of the pins -
// GPIO_D has IO25/IO32, I2C has IO21/IO22, UART1 has IO16/IO17, SPK has IO26 - so the
// chip shares the screen's SPI bus, which is what this build already assumes.
//
// An earlier version of this screen bit-banged that bus looking for the chip select, and
// it hung the board: the display task draws from the other core over the same bus, and
// taking its pins away mid-transfer wedges the SPI peripheral with no way back. So
// nothing here touches the screen's pins. Reading the pins below is safe because the
// screen, the SD card, the speaker and the backlight all use different ones.
//
// The pins a touch interrupt could sit on: the input-only ones, and the two the I2C
// connector brings out.
static const int WATCH[] = { 36, 39, 34, 35, 21, 22 };

static void xptProbeScan() {
  for (unsigned k = 0; k < sizeof WATCH / sizeof *WATCH; k++) pinMode(WATCH[k], INPUT);
}

// Read the touch chip exactly the way LovyanGFX does - same SPI host, same frequency,
// same 57-byte burst - but show the bytes instead of the verdict.
//
// The driver discards any sample outside 128..3968 and reports nothing unless three of
// seven survive, so "no touch" covers two different faults: a chip saying nothing, and a
// chip whose answers are being thrown away. Those need opposite fixes and guessing
// between them has cost a whole evening. The bytes tell them apart: all 00 or all FF is
// a chip that is not replying, anything else is a chip that is.
//
// This uses the library's own SPI calls on the bus it already owns - no pin is taken
// away from the display, which is what wedged the board in 2.19.11.
// The caller holds lcdMutex where it needs to; taking it again here would deadlock.
static bool xptReadWith(int cs, uint8_t *d) {
  memset(d, 0, 8);
  d[0] = 0x91; d[2] = 0xB1; d[4] = 0xD1; d[6] = 0xC1; d[56] = 0x80;
  memcpy(&d[8], d, 8); memcpy(&d[16], d, 16); memcpy(&d[32], d, 24);
  busDmaIdle();
  lgfx::spi::beginTransaction(HSPI_HOST, TOUCH_SPI_HZ, 0);
  digitalWrite(cs, LOW);
  lgfx::spi::readBytes(HSPI_HOST, d, 57);
  digitalWrite(cs, HIGH);
  lgfx::spi::endTransaction(HSPI_HOST);
  for (int i = 0; i < 8; i++)
    if (d[i] != 0x00 && d[i] != 0xFF) return true;   // something answered
  return false;
}

// All zeros on the data line means the chip is never selected, so chip select is the
// thing to vary. The bus itself stays exactly as the library set it up - only these
// candidate pins are driven, and none of them belongs to the screen, the SD card, the
// speaker or the backlight, so nothing can be taken away from the display. That is what
// wedged the board in 2.19.11, and it does not happen here.
// Point the screen bus's data input at a given pin, without disturbing the bus itself.
//
// Re-routing one input signal is all that is needed to ask "does the answer arrive on
// this pin?", and it is the safe way to do it. lgfx::spi::init would also re-point the
// pin, but it re-initialises the entire bus and in doing so resets its DMA channel to
// zero, and that - not the act of restoring - is what froze this board in 2.19.11,
// 2.19.20 and 2.19.23. This touches no bus register, no clock and no DMA.
static void xptPointDataAt(int pin) {
  pinMode(pin, INPUT_PULLUP);   // what LovyanGFX's own software-SPI path uses for miso
  esp_rom_gpio_connect_in_signal(pin, spi_periph_signal[HSPI_HOST].spiq_in, false);
}

// Which pin is the touch chip actually answering on?
//
// Every earlier sweep varied the chip select and left the data pin alone, so a wrong
// data pin would have looked exactly like a dead chip: all zeros, on every chip select,
// through every driver. This varies the data pin instead and reads the chip the same way
// the driver does, so the log names the pin that carries the answer rather than leaving
// it to be inferred.
static const int MISO_TRY[] = { 4, 12, 39, 35, 34 };

static void xptDataSweep(void) {
  uint8_t d[57];
  Serial.println("  which pin carries the answer (bus data input re-pointed each try):");
  if (lcdMutex) xSemaphoreTake(lcdMutex, portMAX_DELAY);
  lcd.endWrite();
  pinMode(PIN_LCD_CS, OUTPUT); digitalWrite(PIN_LCD_CS, HIGH);  // screen cannot answer
  pinMode(PIN_TP_CS,  OUTPUT); digitalWrite(PIN_TP_CS,  HIGH);
  for (unsigned k = 0; k < sizeof MISO_TRY / sizeof *MISO_TRY; k++) {
    int pin = MISO_TRY[k];
    xptPointDataAt(pin);
    bool got = xptReadWith(PIN_TP_CS, d);
    Serial.printf("    in%-3d %02X %02X %02X %02X %02X %02X %02X %02X  %s\n", pin,
                  d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
                  got ? "<-- the answer arrives here" : "");
  }
  xptPointDataAt(PIN_LCD_MISO);   // leave it on the pin this board actually uses
  if (lcdMutex) xSemaphoreGive(lcdMutex);
}

// GPIO4 is deliberately absent. On the Solo Miner that pin is the touch chip's own data
// output, so driving it as a candidate chip select pushed against the chip we were
// trying to read.
static const int CS_TRY[] = { 33, 16, 17, 21, 22, 25, 32 };

static void xptCsSweep(char *out, size_t n) {
  uint8_t d[57];
  int hit = -1;
  if (lcdMutex) xSemaphoreTake(lcdMutex, portMAX_DELAY);
  lcd.endWrite();
  for (unsigned k = 0; k < sizeof CS_TRY / sizeof *CS_TRY; k++) {
    pinMode(CS_TRY[k], OUTPUT);
    digitalWrite(CS_TRY[k], HIGH);
    if (xptReadWith(CS_TRY[k], d) && hit < 0) hit = (int)k;
    if (CS_TRY[k] != PIN_TP_CS) pinMode(CS_TRY[k], INPUT);   // give the pin back
  }
  pinMode(PIN_TP_CS, OUTPUT);
  digitalWrite(PIN_TP_CS, HIGH);
  if (lcdMutex) xSemaphoreGive(lcdMutex);
  if (hit >= 0)
    snprintf(out, n, "cs%d ANSWERS %02X %02X %02X %02X", CS_TRY[hit], d[0], d[1], d[2], d[3]);
  else
    snprintf(out, n, "no cs answers (tried %d)", (int)(sizeof CS_TRY / sizeof *CS_TRY));
}

// Is anything actually wired to this pin?
//
// Pull it up, read it, pull it down, read it. A pin with nothing attached follows
// whichever way it is pulled; a pin wired to something that is driving it does not.
// That says whether the touch chip's output reaches GPIO12 at all - which eight dead
// chip selects cannot. Input-only pins (34-39) have no internal pulls on an ESP32, so
// they can only be reported as they read.
static const char *pinHeld(int pin) {
  if (pin >= 34) return digitalRead(pin) ? "high (no pull available)" : "low (no pull available)";
  pinMode(pin, INPUT_PULLUP);   delayMicroseconds(400); int up = digitalRead(pin);
  pinMode(pin, INPUT_PULLDOWN); delayMicroseconds(400); int dn = digitalRead(pin);
  pinMode(pin, INPUT);
  if (up && !dn) return "floating - nothing attached";
  if (up && dn)  return "held HIGH by something";
  if (!up && !dn) return "held LOW by something";
  return "inverted - odd";
}

// The same findings on the serial log, where they can be copied instead of
// photographed. One line per candidate, with the interrupt state alongside, so a
// report says what the chip did rather than what the screen managed to show.
static void xptReportSerial() {
  uint8_t d[57];
  Serial.printf("touch probe: cs=%d miso=%d sclk=%d mosi=%d %u Hz, int pin %d reads %d\n",
                PIN_TP_CS, PIN_LCD_MISO, PIN_LCD_SCLK, PIN_LCD_MOSI,
                (unsigned)TOUCH_SPI_HZ, PIN_TP_INT, digitalRead(36));
  Serial.printf("  screen bus DMA channel: %d\n", BUS_DMA_CH);
  if (lcdMutex) xSemaphoreTake(lcdMutex, portMAX_DELAY);
  lcd.endWrite();
  for (unsigned k = 0; k < sizeof CS_TRY / sizeof *CS_TRY; k++) {
    pinMode(CS_TRY[k], OUTPUT);
    digitalWrite(CS_TRY[k], HIGH);
    bool got = xptReadWith(CS_TRY[k], d);
    Serial.printf("  cs%-3d %02X %02X %02X %02X %02X %02X %02X %02X  %s\n", CS_TRY[k],
                  d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], got ? "<-- answers" : "");
    if (CS_TRY[k] != PIN_TP_CS) pinMode(CS_TRY[k], INPUT);
  }
  pinMode(PIN_TP_CS, OUTPUT);
  digitalWrite(PIN_TP_CS, HIGH);
  if (lcdMutex) xSemaphoreGive(lcdMutex);
  for (unsigned k = 0; k < sizeof WATCH / sizeof *WATCH; k++)
    Serial.printf("  pin %d = %d\n", WATCH[k], digitalRead(WATCH[k]));

  // Nothing here borrows the screen's SPI bus any more.
  //
  // Three separate attempts to take it, measure, and hand it back have frozen the board.
  // The DMA channel explained two of them and was fixed; this one was not that, and the
  // honest reading is that this bus cannot be borrowed safely from inside this firmware.
  // LovyanGFX's Bus_SPI keeps state that a raw spi::init does not restore, and guessing
  // at the rest costs a reset every time.
  //
  // What is left below reads pins only, which has never caused a problem.

  // The one real difference left: read the chip the way the stock firmware does.
  //
  // Its disassembly settled the configuration - it hardcodes pinMode(33, OUTPUT) for the
  // chip select and a pulled-up input for the interrupt, which is exactly what we use,
  // and it drives SPI through Arduino's register code with no DMA, as we now do. Every
  // setting matches. What differs is whose code runs the transfer: theirs is the Arduino
  // SPI library, ours is the graphics library's own.
  //
  // So try theirs. Sharing the bus is safe enough here: Bus_SPI rewrites SPI_USER,
  // SPI_PIN and SPI_CLOCK at the start of every transaction, so it re-establishes itself
  // afterwards, and the pins are identical so the routing does not change. The screen is
  // deselected and lcdMutex held throughout.
  //
  // Each step is printed before it runs. Three attempts at sharing this bus have frozen
  // the board, and if this is a fourth the log will say exactly which line did it.
  {
    static SPIClass tpSPI(HSPI);
    static bool begun = false;
    uint8_t d[8];
    static const uint8_t seq[8] = { 0x91, 0, 0xB1, 0, 0xD1, 0, 0xC1, 0 };
    Serial.println("  arduino SPI on the screen bus:");
    if (lcdMutex) xSemaphoreTake(lcdMutex, portMAX_DELAY);
    lcd.endWrite();
    pinMode(PIN_LCD_CS, OUTPUT); digitalWrite(PIN_LCD_CS, HIGH);
    Serial.println("    screen deselected");
    if (!begun) { tpSPI.begin(PIN_LCD_SCLK, PIN_LCD_MISO, PIN_LCD_MOSI, -1); begun = true; }
    Serial.println("    bus opened");
    pinMode(PIN_TP_CS, OUTPUT); digitalWrite(PIN_TP_CS, HIGH);
    tpSPI.beginTransaction(SPISettings(TOUCH_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_TP_CS, LOW);
    for (int i = 0; i < 8; i++) d[i] = tpSPI.transfer(seq[i]);
    digitalWrite(PIN_TP_CS, HIGH);
    tpSPI.endTransaction();
    Serial.println("    transfer done");
    if (lcdMutex) xSemaphoreGive(lcdMutex);
    bool got = false;
    for (int i = 0; i < 8; i++) if (d[i] != 0x00 && d[i] != 0xFF) got = true;
    Serial.printf("    cs%d %02X %02X %02X %02X %02X %02X %02X %02X  %s\n", PIN_TP_CS,
                  d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
                  got ? "<-- THE CHIP ANSWERS" : "(nothing)");
  }

  // Can the data line go high at all?
  //
  // It reads low with the screen deselected, through a pull-up, on every read. A pin
  // that cannot go high can never carry an answer, so before blaming any chip, find out
  // whether this one can. The ESP32's own driver is push-pull and beats any sane
  // pull-down resistor: drive it high, read it back, drive it low, read it back.
  //
  // High then low means the line is free and a pull-down was all we were seeing, and the
  // touch chip simply is not driving it. Low both ways means the line is tied down hard,
  // and no firmware can fix that.
  //
  // Safe on this pin: the screen only ever writes, so it never reads GPIO12.
  {
    if (lcdMutex) xSemaphoreTake(lcdMutex, portMAX_DELAY);
    lcd.endWrite();
    pinMode(PIN_LCD_CS, OUTPUT); digitalWrite(PIN_LCD_CS, HIGH);
    pinMode(PIN_LCD_MISO, OUTPUT);
    digitalWrite(PIN_LCD_MISO, HIGH); delayMicroseconds(400);
    int hi = digitalRead(PIN_LCD_MISO);
    digitalWrite(PIN_LCD_MISO, LOW);  delayMicroseconds(400);
    int lo = digitalRead(PIN_LCD_MISO);
    // Hand the pin back to the SPI peripheral's data input. Every previous attempt to
    // restore this bus called lgfx::spi::init, which re-initialises the whole bus and
    // resets its DMA channel - that is what froze the board, not the restoring itself.
    // Pointing one input signal back at one pin touches no bus state and no DMA.
    xptPointDataAt(PIN_LCD_MISO);
    if (lcdMutex) xSemaphoreGive(lcdMutex);
    Serial.printf("  driving GPIO%d: high reads %d, low reads %d  -> %s\n",
                  PIN_LCD_MISO, hi, lo,
                  hi ? "the line is free; nothing is driving it"
                     : "the line is tied low in hardware");
  }

  // One arrangement never tested: the touch chip on the SD card's bus.
  //
  // Every read so far has been on the screen's bus, because that is where this board's
  // documentation puts the touch chip. The SD card has a bus of its own, driven by the
  // Arduino SPI library rather than by the graphics library, and nothing has ever asked
  // whether the chip answers there. It is worth ruling out, and it is free to try: this
  // borrows no pins and touches nothing the screen owns, so it cannot freeze anything.
  {
    static const uint8_t seq[8] = { 0x91, 0, 0xB1, 0, 0xD1, 0, 0xC1, 0 };
    uint8_t d[8];
    Serial.println("  on the SD card's bus (sclk 18, data 23/19):");
    for (unsigned k = 0; k < sizeof CS_TRY / sizeof *CS_TRY; k++) {
      int cs = CS_TRY[k];
      pinMode(cs, OUTPUT); digitalWrite(cs, HIGH);
      sdSPI.beginTransaction(SPISettings(TOUCH_SPI_HZ, MSBFIRST, SPI_MODE0));
      digitalWrite(cs, LOW);
      for (int i = 0; i < 8; i++) d[i] = sdSPI.transfer(seq[i]);
      digitalWrite(cs, HIGH);
      sdSPI.endTransaction();
      bool got = false;
      for (int i = 0; i < 8; i++) if (d[i] != 0x00 && d[i] != 0xFF) got = true;
      Serial.printf("    cs%-3d %02X %02X %02X %02X %02X %02X %02X %02X  %s\n", cs,
                    d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], got ? "<-- ANSWERS" : "");
      if (cs != PIN_TP_CS) pinMode(cs, INPUT);
    }
    pinMode(PIN_TP_CS, OUTPUT); digitalWrite(PIN_TP_CS, HIGH);
  }

  xptDataSweep();

  // Now the pull test, with the screen deselected so it cannot be the thing holding a
  // line down. Last time it ran mid-transaction, which is very likely why GPIO12 looked
  // held low: the screen itself was driving it.
  if (lcdMutex) xSemaphoreTake(lcdMutex, portMAX_DELAY);
  lcd.endWrite();
  pinMode(PIN_LCD_CS, OUTPUT); digitalWrite(PIN_LCD_CS, HIGH);
  Serial.printf("  data line GPIO%d: %s\n", PIN_LCD_MISO, pinHeld(PIN_LCD_MISO));
  for (unsigned k = 0; k < sizeof WATCH / sizeof *WATCH; k++)
    Serial.printf("  spare GPIO%d: %s\n", WATCH[k], pinHeld(WATCH[k]));
  // No bus hand-back here either, for the same reason: the screen writes and never
  // reads this pin, so leaving it an input costs nothing, and handing the bus back is
  // what has frozen this board every time.
  if (lcdMutex) xSemaphoreGive(lcdMutex);
}

static void xptRawDump(char *out, size_t n) {
  uint8_t d[57];
  if (lcdMutex) xSemaphoreTake(lcdMutex, portMAX_DELAY);
  lcd.endWrite();
  xptReadWith(PIN_TP_CS, d);
  if (lcdMutex) xSemaphoreGive(lcdMutex);
  snprintf(out, n, "cs%d %02X %02X %02X %02X %02X %02X %02X %02X", PIN_TP_CS,
           d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
}

// Live levels of those pins. One changing when the panel is pressed means the touch
// circuit is alive, which is how GPIO36 was found.
static void xptWatchDraw(int y) {
  char line[48];
  int n = 0;
  n += snprintf(line + n, sizeof line - n, "press:");
  for (unsigned k = 0; k < sizeof WATCH / sizeof *WATCH; k++)
    n += snprintf(line + n, sizeof line - n, " %d=%d", WATCH[k], digitalRead(WATCH[k]));
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.drawString(line, 8, y);
}
#endif

static void touchTest() {
  lcd.fillScreen(C_BG);
  uiHeader("Touch test", "Done");
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_DIM, C_BG);
  lcd.drawString("Draw with your finger. Dots should", 8, 40);
  lcd.drawString("appear under it. If not, run", 8, 58);
  lcd.drawString("Calibrate touch. BOOT/B = exit.", 8, 76);
#if TOUCH_KIND == TOUCH_XPT_BUS
  xptProbeScan();
  xptReportSerial();   // also on the serial log, for copying into a bug report
  lcd.drawString("36 should go 1 -> 0 when pressed:", 8, 114);
  lcd.drawString("full report on the serial monitor", 8, 186);
#endif
  for (int i = 0; i < 4; i++) {
    int cx = (i & 1) ? SCR_W - 20 : 20, cy = (i & 2) ? SCR_H - 20 : 110;
    lcd.drawCircle(cx, cy, 8, C_GREEN);
  }
  while (true) {
    int x, y;
    if (bootPressed()) { while (bootPressed()) delay(10); return; }
    int ev = btNav();
    if (ev == NAV_BACK || ev == NAV_OK || ev == NAV_SYS) return;
    if (readTouch(x, y)) {
      if (y < 32 && x > SCR_W - 70) { while (readTouch(x, y)) delay(10); return; }
      lcd.fillCircle(x, y, 3, C_ACCENT);
      char b[24];
      snprintf(b, sizeof b, "x=%3d y=%3d  ", x, y);
      lcd.setTextColor(C_TEXT, C_BG);
      lcd.drawString(b, 8, 96);
    }
#if TOUCH_KIND == TOUCH_XPT_BUS
    {
      char b[44];
      int bx = -1, by = -1;
      readTouchRaw(bx, by);
      lcd.setTextColor(C_TEXT, C_BG);
      snprintf(b, sizeof b, "bus  x=%4d y=%4d    ", bx, by);
      lcd.drawString(b, 8, 268);
      xptWatchDraw(132);
      {
        char raw[64];
        xptRawDump(raw, sizeof raw);
        lcd.setTextColor(C_TEXT, C_BG);
        lcd.drawString(raw, 8, 150);
        xptCsSweep(raw, sizeof raw);
        lcd.setTextColor(strstr(raw, "ANSWERS") ? C_GREEN : C_DIM, C_BG);
        lcd.drawString(raw, 8, 168);
      }
    }
#endif
    delay(10);
  }
}

#define NUM_SCALES 5
static const char *scaleName() {
  static const char *n[] = { "1.5x sharp", "1.5x smooth", "1x", "Fit", "Stretch" };
  int i = curScale() % NUM_SCALES;
  if (vSys == SYS_NES && i < 2) return "Fit";   // NES has no 1.5x mode (256 px is already wide)
  return n[i];
}

static const char *orientName() {
  static const char *n[] = { "Portrait flip", "Landscape R", "Portrait", "Landscape L" };
  return n[cfg.rotation & 3];
}

static const char *touchPadName() {
  static const char *n[] = { "Auto", "Always", "Never" };
  return n[cfg.touchPad % 3];
}

static const char *padLandName() {
  static const char *n[] = { "Off", "Touch zones", "Outlines" };
  return n[cfg.padLand % 3];
}

// Next orientation in the order the user would turn the board
static void cycleOrientation() {
#if HAVE_BT
  static const uint8_t order[] = { 2, 1, 0, 3 };  // portrait, landscape R, portrait flip, landscape L
  int i = 0;
  while (i < 4 && order[i] != (cfg.rotation & 3)) i++;
  cfg.rotation = order[(i + 1) % 4];
#else
  cfg.rotation = (cfg.rotation & 3) == 2 ? 0 : 2;
#endif
}

// ---- touch calibration ----------------------------------------------------
static void drawCross(int x, int y, uint16_t c) {
  lcd.drawFastHLine(x - 12, y, 25, c);
  lcd.drawFastVLine(x, y - 12, 25, c);
  lcd.drawCircle(x, y, 6, c);
}

// Waits for a press, averages the raw reading while held. false = aborted (BOOT).
static bool sampleRaw(float &rx, float &ry) {
  int x, y;
  while (readTouchRaw(x, y)) delay(10);
  while (!readTouchRaw(x, y)) { if (bootPressed()) return false; delay(10); }
  delay(80);  // let the finger settle
  float sx = 0, sy = 0; int n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 400) {
    if (readTouchRaw(x, y)) { sx += x; sy += y; n++; }
    delay(15);
  }
  while (readTouchRaw(x, y)) delay(10);
  if (!n) return false;
  rx = sx / n; ry = sy / n;
  return true;
}

// Works out the calibration from the raw readings at four targets (TL, TR, BL, BR, in
// panel coordinates). Returns false if the taps don't make a consistent picture.
struct CalResult { uint8_t swap; float ax, bx, ay, by; };
static bool calSolve(const float rx[4], const float ry[4], const float PX[4], const float PY[4], CalResult &r) {
  // Least-squares line through the 4 points: out = a * in + b, with corr = how well it fits.
  // Works in every orientation (in landscape the screen's left/right is the panel's up/down).
  auto fit = [](const float *in, const float *out, float &a, float &b, float &corr) {
    float mi = 0, mo = 0;
    for (int i = 0; i < 4; i++) { mi += in[i] / 4; mo += out[i] / 4; }
    float sio = 0, sii = 0, soo = 0;
    for (int i = 0; i < 4; i++) {
      sio += (in[i] - mi) * (out[i] - mo); sii += (in[i] - mi) * (in[i] - mi); soo += (out[i] - mo) * (out[i] - mo);
    }
    if (sii < 1e-6f || soo < 1e-6f) { a = 1; b = 0; corr = 0; return; }
    a = sio / sii; b = mo - a * mi; corr = sio / sqrtf(sii * soo);
  };
  float axX, bxX, cX, axY, bxY, cY;
  fit(rx, PX, axX, bxX, cX);        // panel x from raw x
  fit(ry, PX, axY, bxY, cY);        // panel x from raw y
  uint8_t swap = fabsf(cY) > fabsf(cX) ? 1 : 0;   // which raw axis follows the panel's x
  const float *U = swap ? ry : rx, *V = swap ? rx : ry;
  float ax, bx, ay, by, c1, c2;
  fit(U, PX, ax, bx, c1);
  fit(V, PY, ay, by, c2);
  // pixels per raw unit: about 1 for the capacitive touch (it reports pixels), about
  // 0.07 for the resistive one (it reports 0..4095)
  const float lo = TOUCH_RESISTIVE ? 0.02f : 0.4f, hi = TOUCH_RESISTIVE ? 0.3f : 2.5f;
  bool ok = fabsf(c1) > 0.95f && fabsf(c2) > 0.95f &&
            fabsf(ax) > lo && fabsf(ax) < hi && fabsf(ay) > lo && fabsf(ay) < hi;
  // every point must land within 20 px of its target
  for (int i = 0; ok && i < 4; i++) {
    float ex = ax * U[i] + bx - PX[i], ey = ay * V[i] + by - PY[i];
    if (ex * ex + ey * ey > 20 * 20) ok = false;
  }
  r = { swap, ax, bx, ay, by };
  return ok;
}

static void calibrateTouch() {
  // targets in screen coords: TL, TR, BL, BR
  const int TX[4] = { 30, SCR_W - 30, 30, SCR_W - 30 };
  const int TY[4] = { 50, 50, SCR_H - 30, SCR_H - 30 };
  float rx[4], ry[4];
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.setTextDatum(middle_center);
  lcd.drawString("Touch calibration", SCR_W / 2, SCR_H / 2 - 20);
  lcd.setTextColor(C_DIM, C_BG);
  lcd.drawString("Tap the center of each cross", SCR_W / 2, SCR_H / 2 + 5);
  lcd.drawString("(BOOT button = skip)", SCR_W / 2, SCR_H / 2 + 25);
  lcd.drawString("Skip if the taps do nothing: BOOT", SCR_W / 2, SCR_H / 2 + 45);
  lcd.drawString("then works the menus on its own.", SCR_W / 2, SCR_H / 2 + 62);
  lcd.setTextDatum(top_left);
  touchUnusable = false;   // while this screen is up, BOOT cancels rather than navigates
  for (int i = 0; i < 4; i++) {
    drawCross(TX[i], TY[i], C_ACCENT);
    // Cancelled: the touch screen is not usable, so hand the menus to BOOT.
    if (!sampleRaw(rx[i], ry[i])) { while (bootPressed()) delay(10); touchUnusable = true; return; }
    drawCross(TX[i], TY[i], C_BG);
    lcd.fillCircle(TX[i], TY[i], 5, C_GREEN);
  }

  // targets in panel coords (orientation removed)
  float PX[4], PY[4];
  for (int i = 0; i < 4; i++) screenToPanel(TX[i], TY[i], PX[i], PY[i]);

  CalResult cr;
  bool ok = calSolve(rx, ry, PX, PY, cr);
  uint8_t swap = cr.swap;
  float ax = cr.ax, bx = cr.bx, ay = cr.ay, by = cr.by;
  if (!ok) {
    lcd.fillScreen(C_BG);
    uiHeader("Touch calibration");
    uiMessage("Calibration failed", "Tap to go back and try again", C_RED);
    int x, y; waitTap(x, y);
    return;
  }
  cfg.calValid = 1; cfg.calSwap = swap;
  cfg.calAx = ax; cfg.calBx = bx; cfg.calAy = ay; cfg.calBy = by;
  saveSettings();
  touchTest();  // let the user check the result straight away
}

static void displayTouchMenu() {
  enum { DT_ORIENT, DT_CAL, DT_CALCLR, DT_TEST, DT_FLIP, DT_MIRROR, DT_INVERT, DT_SWAP, DT_CHIP };
  while (true) {
    std::vector<MenuItem> it;
    std::vector<int> id;
    auto add = [&](int k, const char *label, String value) { it.push_back({ label, value }); id.push_back(k); };
    add(DT_ORIENT, "Orientation", orientName());
#if TOUCH_KIND != TOUCH_NONE
    add(DT_CAL, "Calibrate touch", cfg.calValid ? "calibrated" : "default");
    add(DT_CALCLR, "Clear calibration", "");
    add(DT_TEST, "Touch test", "");
    add(DT_FLIP, "Touch flip X / Y", String(cfg.touchFlipX ? "X" : "-") + " / " + (cfg.touchFlipY ? "Y" : "-"));
#endif
    add(DT_MIRROR, "Mirror fix", cfg.mirrorFix ? "On" : "Off");
    add(DT_INVERT, "Invert colors", cfg.invert ? "On" : "Off");
    add(DT_SWAP, "Swap red/blue", cfg.swapRB ? "On" : "Off");
#if PANEL_CHOICE
    add(DT_CHIP, "Screen chip", cfg.panelType ? "ST7789" : "ILI9341");   // if the colors or picture look wrong
#endif
    static int selDT = -1;
    int i = runList(TOUCH_KIND == TOUCH_NONE ? "Display" : "Display & touch", "Back", it, &selDT);
    if (i < 0) { saveSettings(); return; }
    bool reinit = false;
    switch (id[i]) {
      case DT_ORIENT: cycleOrientation(); break;
      case DT_CAL: calibrateTouch(); break;
      case DT_CALCLR: cfg.calValid = 0; break;
      case DT_TEST: touchTest(); break;
      case DT_FLIP: {
        int st = (cfg.touchFlipX ? 1 : 0) | (cfg.touchFlipY ? 2 : 0);
        st = (st + 1) & 3; cfg.touchFlipX = st & 1; cfg.touchFlipY = (st >> 1) & 1; break;
      }
      case DT_MIRROR: cfg.mirrorFix ^= 1; break;
      case DT_INVERT: cfg.invert ^= 1; break;
      case DT_SWAP: cfg.swapRB ^= 1; reinit = true; break;
      case DT_CHIP: cfg.panelType ^= 1; reinit = true; break;
    }
    applyDisplaySettings(reinit);
    saveSettings();
  }
}

// ---- Bluetooth controller page ----------------------------------------------
static void pairScreen() {
  btPairing = true;
  btUpdateScanning();
  int startCount = btCount();
  lcd.fillScreen(C_BG);
  uiHeader("Pair controller", "Done");
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_TEXT, C_BG);
  int y = 44;
  auto line = [&](const char *t, uint16_t c = C_TEXT) { lcd.setTextColor(c, C_BG); lcd.drawString(t, 8, y); y += 19; };
  line("Put the controller in pairing mode:", C_ACCENT);
  line("PS4: hold SHARE + PS");
  line("PS5: hold CREATE + PS");
  line("Xbox: hold the pair button");
  line("Switch: hold the sync button");
  line("Stadia: hold Stadia + Y");
  line("Others: see the manual", C_DIM);
  uint32_t t0 = millis();
  bool done = false;
  btNavReset();
  while (!done) {
    int x, ty;
    if (readTouch(x, ty) && ty < 32 && x > SCR_W - 70) done = true;
    if (bootPressed()) done = true;
    int ev = btNav();
    if (ev == NAV_BACK || ev == NAV_SYS) done = true;
    if (btCount() > startCount) {
      lcd.fillRect(0, SCR_H - 50, SCR_W, 40, C_BG);
      lcd.setTextColor(C_GREEN, C_BG);
      lcd.setTextDatum(middle_center);
      lcd.drawString(("Connected: " + btStatus()).substring(0, 34), SCR_W / 2, SCR_H - 35);
      lcd.setTextDatum(top_left);
      delay(1500);
      done = true;
    }
    if (millis() - t0 > 90000) done = true;
    if (!done && ((millis() - t0) / 500) % 2 == 0) {
      lcd.setTextColor(C_DIM, C_BG);
      lcd.setTextDatum(middle_center);
      lcd.drawString("Searching...", SCR_W / 2, SCR_H - 30);
      lcd.setTextDatum(top_left);
    } else if (!done) {
      lcd.fillRect(0, SCR_H - 40, SCR_W, 20, C_BG);
    }
    delay(20);
  }
  { int x, ty; while (readTouch(x, ty)) delay(10); }
  btPairing = false;
  btUpdateScanning();
}

static void controllerMenu() {
  enum { CM_BT, CM_CONN, CM_PAIR, CM_LAYOUT, CM_TOUCHPAD, CM_PADLAND, CM_FORGET };
  while (true) {
    String st = btStatus();
    std::vector<MenuItem> it;
    std::vector<int> id;
    auto add = [&](int k, const char *label, String value) { it.push_back({ label, value }); id.push_back(k); };
#if TOUCH_KIND != TOUCH_NONE   // no touch screen: Bluetooth is the only way to play, so it stays on
    add(CM_BT, "Bluetooth", cfg.btEnabled ? "On" : "Off");
#endif
    add(CM_CONN, "Connected", st.length() ? st.substring(0, 18) : String("none"));
    add(CM_PAIR, "Pair new controller", "");
    add(CM_LAYOUT, "Button layout", cfg.btLayout ? "Xbox (A=bottom)" : "Game Boy");
#if TOUCH_KIND != TOUCH_NONE
    add(CM_TOUCHPAD, "On-screen controls", touchPadName());
    add(CM_PADLAND, "Landscape pad", padLandName());
#endif
    add(CM_FORGET, "Forget all controllers", "");
    static int selCtl = -1;
    int i = runList("Controller", "Back", it, &selCtl);
    if (i < 0) { saveSettings(); return; }
    switch (id[i]) {
      case CM_BT:
        cfg.btEnabled ^= 1;
#if HAVE_BT
        if (cfg.btEnabled) btInit();
        else {
          for (auto c : btCtl) if (c && c->isConnected()) c->disconnect();
          if (btReady) {
            saveSettings();
            lcd.fillScreen(C_BG);
            uiHeader("Controller");
            uiMessage("Bluetooth off", "Restart to free its memory for games", C_DIM);
            int x, y; waitTap(x, y, 4000);
          }
        }
#endif
        btUpdateScanning();
        break;
      case CM_PAIR:
        if (!cfg.btEnabled) cfg.btEnabled = 1;
        btInit();
        pairScreen();
        break;
      case CM_LAYOUT: cfg.btLayout ^= 1; break;
      case CM_TOUCHPAD: cfg.touchPad = (cfg.touchPad + 1) % 3; break;
      case CM_PADLAND: cfg.padLand = (cfg.padLand + 1) % 3; break;
      case CM_FORGET:
        btForgetAll();
        lcd.fillScreen(C_BG);
        uiHeader("Controller");
        uiMessage("All pairings removed", "Pair again from this page", C_GREEN);
        delay(1200);
        break;
    }
    saveSettings();
  }
}

// ----------------------------------------------------------------------------
// USB transfer: the "Send games" page (hackatnight.com/HAN-CYD-GBC) sends games over the
// board's USB serial port. Opened from Setup, or by itself when the page says hello while
// the game list is showing. Text commands, one per line, all starting with "HNX ":
//   HELLO                    -> OK <version> <BT|TOUCH> <free MB on the card>
//   LIST <folder>            -> D <name> for each sub-folder, then END
//   STAT <path>              -> YES <size> or NO
//   MKDIR <folder>           -> OK or ERR <why>   (makes the parent folders too)
//   PUT <path>\t<size>       -> GO <chunk>, then for each chunk the computer sends the bytes
//                               and their CRC-32 (4 bytes, little-endian) and the board answers
//                               A (written) or N (send it again). Last: DONE or ERR <why>.
//   BYE                      -> BYE, back to the game list
// A file is written as <path>.part and renamed when complete, so nothing on the card is
// erased or left half-written; an existing file is only replaced if the page sent it under
// that same name (the page asks you first).
// ----------------------------------------------------------------------------
#define XFER_CHUNK 4096
static char xferBuf[320];
static int xferLen = 0;
static int xferReceived = 0;   // files received in the last transfer session

// Collects serial input into lines. Returns a line (without "HNX ") once one is complete.
static const char *xferPollLine() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c == '\n') {
      xferBuf[xferLen] = 0;
      if (xferLen && xferBuf[xferLen - 1] == '\r') xferBuf[--xferLen] = 0;
      int n = xferLen;
      xferLen = 0;
      if (n > 4 && !strncmp(xferBuf, "HNX ", 4)) return xferBuf + 4;
    } else if (xferLen < (int)sizeof xferBuf - 1) {
      xferBuf[xferLen++] = (char)c;
    } else {
      xferLen = 0;   // too long: not ours
    }
  }
  return nullptr;
}

static uint32_t xferCrc(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

static bool xferPathOk(const String &p) {
  return p.startsWith("/") && p.indexOf("..") < 0 && p.length() < 250;
}
static bool xferMkdirs(const String &dir) {
  for (int i = 1; i <= (int)dir.length(); i++)
    if (i == (int)dir.length() || dir[i] == '/') {
      String d = dir.substring(0, i);
      if (!SD.exists(d) && !SD.mkdir(d)) return false;
    }
  return true;
}

static void xferScreen(const char *status, uint16_t color, const String &file, int pct) {
  lcd.startWrite();
  lcd.fillRect(0, 40, SCR_W, SCR_H - 40, C_BG);
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(color, C_BG);
  lcd.drawString(status, SCR_W / 2, 64);
  lcd.setTextColor(C_DIM, C_BG);
  lcd.drawString("On a computer, open in Chrome or Edge:", SCR_W / 2, 96);
  lcd.setTextColor(C_ACCENT, C_BG);
  lcd.drawString("hackatnight.com/HAN-CYD-GBC", SCR_W / 2, 116);
  if (file.length()) {
    String n = file.substring(file.lastIndexOf('/') + 1);
    if (lcd.textWidth(n) > SCR_W - 16) {
      while (n.length() > 4 && lcd.textWidth(n + "~") > SCR_W - 16) n.remove(n.length() - 1);
      n += "~";
    }
    lcd.setTextColor(C_TEXT, C_BG);
    lcd.drawString(n, SCR_W / 2, SCR_H - 78);
  }
  if (pct >= 0) {
    int bw = min(240, SCR_W - 40), bx = (SCR_W - bw) / 2, y = SCR_H - 60;
    lcd.drawRoundRect(bx, y, bw, 10, 4, C_DIM);
    if (pct > 0) lcd.fillRoundRect(bx + 2, y + 2, (bw - 4) * pct / 100, 6, 3, C_ACCENT);
  }
  char b[48];
  snprintf(b, sizeof b, xferReceived == 1 ? "1 file received" : "%d files received", xferReceived);
  lcd.setTextColor(C_DIM, C_BG);
  lcd.drawString(b, SCR_W / 2, SCR_H - 24);
  lcd.setTextDatum(top_left);
  lcd.endWrite();
}

static void xferPut(const char *args) {
  const char *tab = strchr(args, '\t');
  if (!tab) { Serial.print("HNX ERR bad PUT\n"); return; }
  String path = String(args).substring(0, tab - args);
  uint32_t size = strtoul(tab + 1, nullptr, 10);
  if (!xferPathOk(path)) { Serial.print("HNX ERR bad path\n"); return; }
  int sl = path.lastIndexOf('/');
  if (sl > 0 && !xferMkdirs(path.substring(0, sl))) { Serial.print("HNX ERR can't make the folder\n"); return; }
  String tmp = path + ".part";
  if (SD.exists(tmp)) SD.remove(tmp);
  File f = SD.open(tmp, FILE_WRITE);
  uint8_t *buf = (uint8_t *)malloc(XFER_CHUNK + 4);
  if (!f || !buf) { if (f) f.close(); free(buf); Serial.print("HNX ERR can't write to the SD card\n"); return; }
  xferScreen("Receiving...", C_TEXT, path, 0);
  Serial.printf("HNX GO %d\n", XFER_CHUNK);
  uint32_t got = 0;
  int lastPct = 0, tries = 0;
  bool ok = true;
  Serial.setTimeout(3000);
  while (got < size) {
    int n = (int)min((uint32_t)XFER_CHUNK, size - got);
    size_t r = Serial.readBytes(buf, n + 4);
    uint32_t want = buf[n] | (buf[n + 1] << 8) | (buf[n + 2] << 16) | ((uint32_t)buf[n + 3] << 24);
    if (r == (size_t)n + 4 && xferCrc(buf, n) == want) {
      if (f.write(buf, n) != (size_t)n) { ok = false; break; }
      got += n;
      tries = 0;
      Serial.print("HNX A\n");
      int pct = (int)((uint64_t)got * 100 / size);
      if (pct - lastPct >= 4 || got == size) {   // redraw the bar now and then
        lastPct = pct;
        int bw = min(240, SCR_W - 40), bx = (SCR_W - bw) / 2;
        lcd.fillRoundRect(bx + 2, SCR_H - 58, (bw - 4) * pct / 100, 6, 3, C_ACCENT);
      }
    } else {
      // lost or damaged: drop whatever is still arriving, then ask for the chunk again
      uint32_t t0 = millis();
      while (millis() - t0 < 60) { while (Serial.available()) Serial.read(); delay(2); }
      if (++tries > 5) { ok = false; break; }
      Serial.print("HNX N\n");
    }
  }
  f.close();
  free(buf);
  if (!ok) {
    SD.remove(tmp);
    Serial.print("HNX ERR transfer failed\n");
    xferScreen("Transfer failed", C_RED, path, -1);
    return;
  }
  if (SD.exists(path)) SD.remove(path);   // the page asked before replacing it
  if (!SD.rename(tmp, path)) { SD.remove(tmp); Serial.print("HNX ERR can't rename\n"); return; }
  xferReceived++;
  ledFlash(LED_FLASH_SAVE);
  Serial.print("HNX DONE\n");
  xferScreen("Received", C_GREEN, path, 100);
}

// Runs one command. Returns false on BYE.
// Maintenance commands, for the tools on the flasher page.
//
// They are here rather than on a menu because the cases that need them are the ones
// where the screen cannot be trusted: a display setting flipped to something unreadable,
// a touch calibration that lands every tap in the wrong place. A cable and a browser
// still work when the screen does not.
static void sysInfoCollect(std::vector<MenuItem> &it);
static void sysInfoSerial(const std::vector<MenuItem> &it);

// Every copy of the bundled demo games, wherever they were filed.
static int xferRemoveDemos(void) {
  std::vector<String> doomed;
  File root = SD.open("/Roms");
  if (root && root.isDirectory()) {
    while (true) {
      bool isDir = false;
      String d = root.getNextFileName(&isDir);
      if (!d.length()) break;
      if (!isDir) continue;
      File sub = SD.open(d);
      if (!sub) continue;
      while (true) {
        bool sawDir = false;
        String f = sub.getNextFileName(&sawDir);
        if (!f.length()) break;
        if (sawDir) continue;
        String base = f.substring(f.lastIndexOf('/') + 1);
        if (base.startsWith("HAN-DEMO")) doomed.push_back(f);   // collect, then delete
      }
      sub.close();
    }
  }
  if (root) root.close();
  int n = 0;
  for (const String &f : doomed) if (SD.remove(f)) n++;
  return n;
}

static bool xferCommand(const char *cmd) {
  String c(cmd);
  if (c == "HELLO") {
    uint64_t freeMB = (SD.totalBytes() - SD.usedBytes()) >> 20;
    Serial.printf("HNX OK %s %s %u %s\n", APP_VERSION, HAVE_BT ? "BT" : "TOUCH", (unsigned)freeMB, BOARD_NAME);
  } else if (c.startsWith("LIST ")) {
    File d = SD.open(c.substring(5));
    if (d && d.isDirectory()) {
      while (true) {
        bool isDir = false;
        String n = d.getNextFileName(&isDir);
        if (!n.length()) break;
        if (isDir) Serial.printf("HNX D %s\n", n.substring(n.lastIndexOf('/') + 1).c_str());
      }
    }
    if (d) d.close();
    Serial.print("HNX END\n");
  } else if (c.startsWith("STAT ")) {
    File f = SD.open(c.substring(5));
    if (f) { Serial.printf("HNX YES %u\n", (unsigned)f.size()); f.close(); }
    else Serial.print("HNX NO\n");
  } else if (c.startsWith("MKDIR ")) {
    String d = c.substring(6);
    Serial.print(xferPathOk(d) && xferMkdirs(d) ? "HNX OK\n" : "HNX ERR can't make the folder\n");
  } else if (c.startsWith("PUT ")) {
    xferPut(cmd + 4);
  } else if (c == "INFO") {
    std::vector<MenuItem> it;
    sysInfoCollect(it);
    sysInfoSerial(it);          // plain lines: the page shows them in its serial monitor
    Serial.print("HNX OK\n");
  } else if (c == "DEFAULTS") {
    prefs.begin("gbc", false);
    prefs.clear();
    prefs.end();
    Serial.print("HNX OK settings cleared, restarting\n");
    Serial.flush();
    delay(150);
    ESP.restart();
  } else if (c == "CALCLR") {
    cfg.calValid = 0;
    saveSettings();
    Serial.print("HNX OK touch calibration cleared\n");
  } else if (c == "RMDEMOS") {
    Serial.printf("HNX OK %d\n", xferRemoveDemos());
  } else if (c == "REBOOT") {
    Serial.print("HNX OK restarting\n");
    Serial.flush();
    delay(150);
    ESP.restart();
  } else if (c == "BYE") {
    Serial.print("HNX BYE\n");
    return false;
  } else {
    Serial.print("HNX ERR unknown command\n");
  }
  return true;
}

// The transfer screen. greeted: the page already said hello (from the game list).
static void xferMode(bool greeted) {
  xferReceived = 0;
  Serial.flush();
  Serial.end();
  Serial.setRxBufferSize(XFER_CHUNK * 2 + 64);   // a whole chunk fits while the SD card writes
  Serial.begin(XFER_BAUD);
  lcd.fillScreen(C_BG);
  uiHeader("USB transfer", "Back");
  headerHints("USB transfer", SCR_W - 68, nullptr, nullptr);
  bool connected = greeted;
  if (greeted) xferCommand("HELLO");
  xferScreen(connected ? "Connected" : "Waiting for the computer...", connected ? C_GREEN : C_TEXT, String(), -1);
  btNavReset();
  int x, y;
  while (readTouch(x, y)) delay(10);
  while (true) {
    if (const char *cmd = xferPollLine()) {
      if (!connected) { connected = true; xferScreen("Connected", C_GREEN, String(), -1); }
      if (!xferCommand(cmd)) break;
      continue;
    }
    int ev = btNav();
    if (ev == NAV_BACK || ev == NAV_SYS) break;
    if (bootPressed()) { while (bootPressed()) delay(10); break; }
    if (readTouch(x, y)) {
      while (readTouch(x, y)) delay(10);
      if (y < 32 && x > SCR_W - 70) break;
    }
    delay(2);
  }
  Serial.flush();
  Serial.end();
  Serial.setRxBufferSize(256);
  Serial.begin(XFER_BAUD);
}

// Menu music: on/off, which tune, and its own volume (the games keep theirs).
static void musicMenu() {
  while (true) {
    std::vector<MenuItem> it = {
      { "Music", cfg.musicOn ? "On" : "Off" },
      { "Tune", TUNES[cfg.musicTune % NUM_TUNES].name },
      { "Music volume", String(cfg.musicVol) + "/4" },
    };
    static int selMus = -1;
    int i = runList("Music", "Back", it, &selMus);
    if (i < 0) break;
    if (i == 0) {
      cfg.musicOn ^= 1;
      if (cfg.musicOn) musicStart(); else musicStop();
    } else if (i == 1) {
      cfg.musicTune = (cfg.musicTune + 1) % NUM_TUNES;
      musicNewTune = true;
      musicStart();
    } else if (i == 2) {
      cfg.musicVol = cfg.musicVol % 4 + 1;
      musicStart();
    }
    saveSettings();
  }
}

// Board LED. Opened from Setup and from the pause menu.
static void ledMenu() {
  while (true) {
    std::vector<MenuItem> it = {
      { "Mode", LED_MODES[cfg.ledMode % 3] },
      { "Color", LED_COLORS[cfg.ledColor % NUM_LED_COLORS].name },
      { "Brightness", String((cfg.ledBright % 4 + 1) * 25) + "%" },
      { "During games", LED_GAME[cfg.ledGame % 3] },
      { "Flash on save", cfg.ledFlashSave ? "On" : "Off" },
      { "Flash on cheat", cfg.ledFlashCheat ? "On" : "Off" },
    };
    static int selLed = -1;
    int i = runList("LED", "Back", it, &selLed);
    switch (i) {
      case 0: cfg.ledMode = (cfg.ledMode + 1) % 3; break;
      case 1: cfg.ledColor = (cfg.ledColor + 1) % NUM_LED_COLORS; break;
      case 2: cfg.ledBright = (cfg.ledBright + 1) % 4; break;
      case 3: cfg.ledGame = (cfg.ledGame + 1) % 3; break;
      case 4: cfg.ledFlashSave ^= 1; if (cfg.ledFlashSave) ledFlash(LED_FLASH_SAVE); break;   // shows it once
      case 5: cfg.ledFlashCheat ^= 1; if (cfg.ledFlashCheat) ledFlash(LED_FLASH_CHEAT); break;
      default: saveSettings(); return;
    }
    saveSettings();
  }
}

// ----------------------------------------------------------------------------
// System info: what this board is, and how this build has it wired.
//
// A board that can describe itself saves a great deal of guesswork. The touch chip on
// the Elecrow Solo Miner answers on GPIO4 where every other board of its family uses
// GPIO12, and establishing that took a schematic and a disassembly of the stock
// firmware; a screen stating which pins this build actually drives would have cut that
// to a glance. The same lines go to the USB serial port, so they can be pasted into a
// bug report without copying them off a photograph of the screen.
// ----------------------------------------------------------------------------
static void sysInfoCollect(std::vector<MenuItem> &it) {
  auto add = [&](const char *k, String v) { it.push_back({ k, v }); };
  char mac[20];
  uint64_t m = ESP.getEfuseMac();
  snprintf(mac, sizeof mac, "%02X:%02X:%02X:%02X:%02X:%02X", (uint8_t)m, (uint8_t)(m >> 8),
           (uint8_t)(m >> 16), (uint8_t)(m >> 24), (uint8_t)(m >> 32), (uint8_t)(m >> 40));

  add("Firmware", String(APP_VERSION) + (HAVE_BT ? " BT" : " touch"));
  add("Board", BOARD_NAME);
  add("Chip", String(ESP.getChipModel()) + " rev" + ESP.getChipRevision());
  add("CPU", String(ESP.getCpuFreqMHz()) + " MHz, " + ESP.getChipCores() + " cores");
  add("Flash", String((unsigned)(ESP.getFlashChipSize() / (1024 * 1024))) + " MB");
  add("Free RAM", String(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024) + " KB, " +
                  String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024) + " largest");
  add("MAC", mac);

  add("Screen", String(SCR_W) + "x" + SCR_H);
#if PANEL_CHOICE
  add("Screen chip", cfg.panelType ? "ST7789" : "ILI9341");
#endif
  add("Screen bus", String("CLK") + PIN_LCD_SCLK + " out" + PIN_LCD_MOSI + " in" + PIN_LCD_MISO);
  add("Screen CS/DC", String("CS") + PIN_LCD_CS + " DC" + PIN_LCD_DC);
  add("Screen DMA", BUS_DMA_CH ? String("channel ") + BUS_DMA_CH : String("off (shared bus)"));
  add("Backlight", String("GPIO") + PIN_LCD_BL);

  add("Touch chip", TOUCH_KIND == TOUCH_CST ? "CST820 / GT911 (I2C)"
                  : TOUCH_KIND == TOUCH_XPT_OWN ? "XPT2046 (own pins)"
                  : TOUCH_KIND == TOUCH_XPT_BUS ? "XPT2046 (screen bus)"
                  : TOUCH_KIND == TOUCH_NONE ? "none" : "I2C");
#if TOUCH_KIND == TOUCH_XPT_BUS
  // The data pin is the one worth stating plainly: it is the difference between the
  // CrowPanel (GPIO12) and the Solo Miner (GPIO4), and a wrong one looks like a dead chip.
  add("Touch data", String("GPIO") + PIN_LCD_MISO + " (shared bus)");
  add("Touch CS/INT", String("CS") + PIN_TP_CS + " INT" + PIN_TP_INT);
  add("Touch clock", String(TOUCH_SPI_HZ / 1000) + " kHz");
#elif TOUCH_KIND == TOUCH_XPT_OWN
  add("Touch bus", String("CLK") + PIN_TP_SCLK + " out" + PIN_TP_MOSI + " in" + PIN_TP_MISO);
  add("Touch CS/INT", String("CS") + PIN_TP_CS + " INT" + PIN_TP_INT);
#elif TOUCH_KIND != TOUCH_NONE
  add("Touch bus", String("SDA") + PIN_TP_SDA + " SCL" + PIN_TP_SCL + " INT" + PIN_TP_INT);
#endif
#if TOUCH_KIND != TOUCH_NONE
  add("Touch cal", cfg.calValid ? "calibrated" : "default");
#endif

  add("SD bus", String("CLK") + PIN_SD_SCK + " out" + PIN_SD_MOSI + " in" + PIN_SD_MISO + " CS" + PIN_SD_CS);
  add("SD card", sdOk ? String((unsigned)(SD.cardSize() / (1024ULL * 1024 * 1024))) + " GB" : String("not mounted"));
  add("Speaker", "GPIO26 (DAC2)");
  add("RGB LED", HAS_LED ? "yes" : "none");
  add("Bluetooth", HAVE_BT ? "yes" : "not in this build");
}

static void sysInfoSerial(const std::vector<MenuItem> &it) {
  Serial.printf("\n--- %s system info ---\n", APP_NAME);
  for (const MenuItem &m : it) Serial.printf("  %-14s %s\n", m.label.c_str(), m.value.c_str());
  Serial.println("  Docs           github.com/hackatnight/HAN-CYD-GBC");
  Serial.println("--- end ---\n");
}

static void sysInfoScreen() {
  bool printed = false;
  static int selInfo = -1;
  while (true) {
    std::vector<MenuItem> it;
    sysInfoCollect(it);
    if (!printed) { sysInfoSerial(it); printed = true; }   // once, on opening
    it.push_back({ "Print to USB serial", "again" });
    int i = runList("System info", "Back", it, &selInfo);
    if (i < 0) return;
    if (i == (int)it.size() - 1) {
      std::vector<MenuItem> fresh;        // without the button itself
      sysInfoCollect(fresh);
      sysInfoSerial(fresh);
    }
  }
}

static void settingsMenu() {
  while (true) {
    std::vector<MenuItem> it = {
      { "Game volume", cfg.volume ? String(cfg.volume) + "/" + String(VOL_MAX) : String("Mute") },
      { "Music", cfg.musicOn ? String(TUNES[cfg.musicTune % NUM_TUNES].name) : String("Off") },
      { "Scaling", scaleName() },
      { "DMG palette", DMG_PALETTE_NAMES[cfg.dmgPalette % NUM_DMG_PALETTES] },
      { "NES palette", NES_PALETTE_NAMES[cfg.nesPalette % NES_NUM_PALETTES] },
      { "Brightness", String(cfg.brightness * 100 / 255) + "%" },
      { "Show FPS", cfg.showFps ? "On" : "Off" },
      { "Theme", theme().name },
      { "LED", ">" },   // index 8: removed below on boards without an LED
      { "Display & touch", ">" },
      { "Transfer games (USB)", ">" },
      { "System info", ">" },   // board, chip, pins - and the same list over serial
      { "Screensaver", idleName() },
#if HAVE_BT
      { "Controller (Bluetooth)", btCount() ? String(btCount()) + " connected" : String(">") },
#endif
    };
#if !HAS_LED
    it.erase(it.begin() + 8);
#endif
    static int selSet = -1;
    int i = runList("Settings", "Back", it, &selSet);
#if !HAS_LED
    if (i >= 8) i++;
#endif
    switch (i) {
      case -3:
      case -1: saveSettings(); return;
      case 0: cfg.volume = (cfg.volume + 1) % (VOL_MAX + 1); break;
      case 1: musicMenu(); break;
      case 2: curScale() = (curScale() + 1) % NUM_SCALES; break;
      case 3: cfg.dmgPalette = (cfg.dmgPalette + 1) % NUM_DMG_PALETTES; break;
      case 4: cfg.nesPalette = (cfg.nesPalette + 1) % NES_NUM_PALETTES; break;
      case 5: cfg.brightness = cfg.brightness >= 255 ? 64 : min(255, cfg.brightness + 64); break;
      case 6: cfg.showFps ^= 1; break;
      case 7: cfg.theme = (cfg.theme + 1) % NUM_THEMES; applyTheme(); break;
      case 8: ledMenu(); break;
      case 9: displayTouchMenu(); break;
      case 10: xferMode(false); break;
      case 11: sysInfoScreen(); break;
      case 12: idleCycle(); break;
      case 13: controllerMenu(); break;
    }
    applyDisplaySettings(false);
    saveSettings();
  }
}

// ----------------------------------------------------------------------------
// Cheats: a small memory changer. A cheat keeps game-memory addresses at fixed
// values, written once per frame (infinite lives, health, money, time...). Codes come
// from a RetroArch-style .cht file on the SD card or from the built-in cheat finder.
//   Game Boy / GBC: GameShark "ttVVLLHH" (tt = 01, 80-8F cartridge RAM bank, 90-97 GBC
//                   work RAM bank),
//                   or "AAAA=VV"
//   NES:            Pro Action Replay "AAAA:VV", "AAAAVV" or "00AAAAVV"
// Game Genie codes patch the ROM instead; they're listed but can't be switched on.
// Files: /CHEATS/GB, /CHEATS/GBC and /CHEATS/NES, one <game name>.cht per game. GB and
// GBC games look in both GB folders, .gbc files trying GBC first.
// ----------------------------------------------------------------------------
struct CheatCode { uint8_t bank; uint16_t addr; uint8_t val; };
struct Cheat {
  String desc, code;
  bool on = false, ok = false;
  uint8_t n = 0;
  CheatCode c[4];
};
static std::vector<Cheat> cheats;
static String cheatPath;
static bool cheatsDirty = false, cheatsActive = false;
#define MAX_CHEATS 48

static bool isHexStr(const String &s) {
  if (!s.length()) return false;
  for (size_t i = 0; i < s.length(); i++) if (!isxdigit((unsigned char)s[i])) return false;
  return true;
}
static int hexAt(const String &s, int from, int len) { return (int)strtol(s.substring(from, from + len).c_str(), nullptr, 16); }

static bool parseCheat(Cheat &ch, bool nes) {
  ch.n = 0;
  String all = ch.code;
  int start = 0;
  while (start <= (int)all.length() && ch.n < 4) {
    int plus = all.indexOf('+', start);
    String part = all.substring(start, plus < 0 ? all.length() : plus);
    start = plus < 0 ? all.length() + 1 : plus + 1;
    part.trim();
    part.toUpperCase();
    part.replace(" ", "");
    if (!part.length()) continue;
    CheatCode cc = { 0xFF, 0, 0 };
    int sep = part.indexOf(':') >= 0 ? part.indexOf(':') : part.indexOf('=');
    if (sep > 0) {                                         // AAAA:VV / AAAA=VV
      String a = part.substring(0, sep), v = part.substring(sep + 1);
      if (!isHexStr(a) || !isHexStr(v) || a.length() > 4 || v.length() > 2) return false;
      cc.addr = (uint16_t)strtol(a.c_str(), nullptr, 16);
      cc.val = (uint8_t)strtol(v.c_str(), nullptr, 16);
    } else if (!isHexStr(part)) {
      return false;                                        // Game Genie letters etc.
    } else if (!nes && part.length() == 8) {               // GameShark ttVVLLHH
      int tt = hexAt(part, 0, 2);
      cc.val = (uint8_t)hexAt(part, 2, 2);
      cc.addr = (uint16_t)(hexAt(part, 6, 2) << 8 | hexAt(part, 4, 2));
      if (tt >= 0x90 && tt <= 0x97) cc.bank = tt & 7;                  // GBC work RAM bank
      else if (tt >= 0x80 && tt <= 0x8F) cc.bank = 0x80 | (tt & 0x0F);  // cartridge RAM bank
    } else if (nes && part.length() == 6) {                // AAAAVV
      cc.addr = (uint16_t)hexAt(part, 0, 4);
      cc.val = (uint8_t)hexAt(part, 4, 2);
    } else if (nes && part.length() == 8) {                // 00AAAAVV
      cc.addr = (uint16_t)hexAt(part, 2, 4);
      cc.val = (uint8_t)hexAt(part, 6, 2);
    } else {
      return false;
    }
    ch.c[ch.n++] = cc;
  }
  return ch.n > 0;
}

static void cheatsUpdateActive() {
  cheatsActive = false;
  for (auto &c : cheats) if (c.on && c.ok) cheatsActive = true;
}

static String unquote(String v) {
  v.trim();
  if (v.length() >= 2 && v[0] == '"' && v[v.length() - 1] == '"') v = v.substring(1, v.length() - 1);
  return v;
}

// "Super Mario Land 2 - 6 Golden Coins (USA, Europe) [!]" -> "supermarioland26goldencoins"
static String normName(const char *n) {
  String o;
  int depth = 0;
  for (const char *p = n; *p; p++) {
    char c = *p;
    if (c == '(' || c == '[') depth++;
    else if ((c == ')' || c == ']') && depth) depth--;
    else if (!depth && isalnum((unsigned char)c)) o += (char)tolower((unsigned char)c);
  }
  return o;
}

// Cheat files use the standard (No-Intro) game names, which may not match the ROM's file
// name. Exact name first; then the same name ignoring (region) [tags], case and punctuation;
// then a single file whose name starts like the ROM's ("Super Mario Land 2" ->
// "Super Mario Land 2 - 6 Golden Coins (USA, Europe)").
static String findCheatFile(const String &dir, const String &base) {
  String exact = dir + base + ".cht";
  if (SD.exists(exact)) return exact;
  String want = normName(base.c_str());
  if (want.length() < 4) return exact;
  File d = SD.open(dir.substring(0, dir.length() - 1));
  if (!d) return exact;
  String prefixHit;
  int prefixCount = 0;
  while (true) {
    bool isDir = false;
    String n = d.getNextFileName(&isDir);
    if (!n.length()) break;
    if (isDir || !n.endsWith(".cht")) continue;
    String fn = n.substring(n.lastIndexOf('/') + 1);
    String got = normName(fn.substring(0, fn.length() - 4).c_str());
    if (got == want) { d.close(); return dir + fn; }
    if (want.length() >= 8 && got.startsWith(want)) { prefixHit = dir + fn; prefixCount++; }
  }
  d.close();
  return prefixCount == 1 ? prefixHit : exact;
}

#define CHEAT_ROOT "/CHEATS"

// sys: 0 = GB, 1 = GBC, 2 = NES. The path is where cheats are read from and saved to.
static void cheatsLoad(int sys, const String &base) {
  const bool nes = sys == 2;
  cheats.clear();
  cheatsDirty = false;
  static const char *const dirs[] = { CHEAT_ROOT "/GB/", CHEAT_ROOT "/GBC/", CHEAT_ROOT "/NES/" };
  cheatPath = findCheatFile(dirs[sys], base);
  if (sys < 2 && !SD.exists(cheatPath)) {
    String other = findCheatFile(dirs[sys ^ 1], base);
    if (SD.exists(other)) cheatPath = other;
  }
  File f = SD.open(cheatPath, FILE_READ);
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (!line.length() || line[0] == '#') continue;
      int eq = line.indexOf('=');
      if (eq < 0) {                                       // a bare code
        if (cheats.size() < MAX_CHEATS) { Cheat c; c.desc = line; c.code = line; cheats.push_back(c); }
        continue;
      }
      String key = line.substring(0, eq), val = unquote(line.substring(eq + 1));
      key.trim();
      int us = key.indexOf('_');
      if (key.startsWith("cheat") && us > 5) {            // RetroArch: cheatN_desc / _code / _enable
        int idx = key.substring(5, us).toInt();
        String field = key.substring(us + 1);
        if (idx < 0 || idx >= MAX_CHEATS) continue;
        while ((int)cheats.size() <= idx) cheats.push_back(Cheat());
        if (field == "desc") cheats[idx].desc = val;
        else if (field == "code") cheats[idx].code = val;
        else if (field == "enable") cheats[idx].on = val == "true";
      } else if (key != "cheats" && cheats.size() < MAX_CHEATS) {   // "Name = CODE"
        Cheat c; c.desc = key; c.code = val; cheats.push_back(c);
      }
    }
    f.close();
  }
  for (size_t i = 0; i < cheats.size();) {
    if (!cheats[i].code.length()) { cheats.erase(cheats.begin() + i); continue; }
    if (!cheats[i].desc.length()) cheats[i].desc = cheats[i].code;
    cheats[i].ok = parseCheat(cheats[i], nes);
    if (!cheats[i].ok) cheats[i].on = false;
    i++;
  }
  cheatsUpdateActive();
  if (cheats.size()) Serial.printf("Cheats: %d from %s\n", (int)cheats.size(), cheatPath.c_str());
}

static bool cheatsSave() {
  SD.mkdir(CHEAT_ROOT);
  SD.mkdir(cheatPath.substring(0, cheatPath.lastIndexOf('/')));
  File f = SD.open(cheatPath, FILE_WRITE);
  if (!f) return false;
  f.printf("cheats = %d\n\n", (int)cheats.size());
  for (size_t i = 0; i < cheats.size(); i++) {
    f.printf("cheat%d_desc = \"%s\"\n", (int)i, cheats[i].desc.c_str());
    f.printf("cheat%d_code = \"%s\"\n", (int)i, cheats[i].code.c_str());
    f.printf("cheat%d_enable = %s\n\n", (int)i, cheats[i].on ? "true" : "false");
  }
  f.close();
  cheatsDirty = false;
  return true;
}

// once per frame, before the frame runs
static void cheatsApply() {
  if (!cheatsActive) return;
  for (auto &ch : cheats) {
    if (!ch.on || !ch.ok) continue;
    for (int k = 0; k < ch.n; k++) {
      if (vSys == SYS_NES) nes_cheat_poke(ch.c[k].addr, ch.c[k].val);
      else gb_cheat_poke(ch.c[k].bank, ch.c[k].addr, ch.c[k].val);
    }
  }
}

// ---- cheat finder: narrow down which memory byte holds a value seen on screen ----
// One bit per searchable byte (NES: 256 B to 1.3 KB, GB: 1 KB, GBC: 4 KB), only while searching.
static uint8_t *findMask = nullptr;
static int findSpace = 0, findCount = 0;

static int cheatSpace() { return vSys == SYS_NES ? nes_cheat_space() : gb_cheat_space(); }
static int cheatPeek(int i) { return vSys == SYS_NES ? nes_cheat_peek(i) : gb_cheat_peek(i); }

static void findFree() { free(findMask); findMask = nullptr; findSpace = findCount = 0; }

static bool findStart(int v) {
  findFree();
  findSpace = cheatSpace();
  findMask = (uint8_t *)calloc((findSpace + 7) / 8, 1);
  if (!findMask) return false;
  for (int i = 0; i < findSpace; i++)
    if (cheatPeek(i) == v) { findMask[i >> 3] |= 1 << (i & 7); findCount++; }
  return true;
}

static void findRefine(int v) {
  findCount = 0;
  for (int i = 0; i < findSpace; i++)
    if (findMask[i >> 3] & (1 << (i & 7))) {
      if (cheatPeek(i) == v) findCount++;
      else findMask[i >> 3] &= ~(1 << (i & 7));
    }
}

static void cheatsReset() {   // game ends
  if (cheatsDirty) cheatsSave();
  cheats.clear();
  cheats.shrink_to_fit();
  cheatsActive = false;
  findFree();
}

// Number entry 0-255 with touch buttons (-10 -1 +1 +10, OK) or the d-pad
// (left/right = 1, up/down = 10). Returns -1 if cancelled.
static int pickNumber(const char *title, const char *prompt, int value) {
  value = constrain(value, 0, 255);
  const int bw = (SCR_W - 5 * 8) / 4, bh = 40, by = SCR_H / 2 + 6, oky = SCR_H - 48;
  static const char *lab[4] = { "-10", "-1", "+1", "+10" };
  static const int step[4] = { -10, -1, 1, 10 };
  lcd.fillScreen(C_BG);
  uiHeader(title, "Back");
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_DIM, C_BG);
  lcd.setTextDatum(middle_center);
  lcd.drawString(prompt, SCR_W / 2, 52);
  for (int i = 0; i < 4; i++) {
    int x = 8 + i * (bw + 8);
    lcd.fillRoundRect(x, by, bw, bh, 6, C_PANEL);
    lcd.setTextColor(C_TEXT, C_PANEL);
    lcd.drawString(lab[i], x + bw / 2, by + bh / 2);
  }
  lcd.fillRoundRect(8, oky, SCR_W - 16, 38, 6, C_HILITE);
  lcd.drawRoundRect(8, oky, SCR_W - 16, 38, 6, C_ACCENT);
#if HAVE_BT
  drawPadIcon(SCR_W / 2 - 22, oky + 19, okIcon());
  lcd.setTextColor(C_TEXT, C_HILITE);
  lcd.drawString("OK", SCR_W / 2 + 4, oky + 19);
#else
  lcd.setTextColor(C_TEXT, C_HILITE);
  lcd.drawString("OK", SCR_W / 2, oky + 19);
#endif
  auto drawValue = [&]() {
    lcd.fillRect(0, 70, SCR_W, by - 74, C_BG);
    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(3);
    lcd.setTextColor(C_TEXT, C_BG);
    lcd.setTextDatum(middle_center);
    char b[16];
    snprintf(b, sizeof b, "%d", value);
    lcd.drawString(b, SCR_W / 2, (70 + by) / 2);
    lcd.setTextSize(1);
    snprintf(b, sizeof b, "(hex %02X)", value);
    lcd.setTextColor(C_DIM, C_BG);
    lcd.drawString(b, SCR_W / 2, by - 12);
  };
  drawValue();
  btNavReset();
  int x, y;
  while (readTouch(x, y)) delay(10);
  while (true) {
    int nv = value;
    if (readTouch(x, y)) {
      while (readTouch(x, y)) delay(10);
      if (y < 32 && x > SCR_W - 70) { lcd.setTextDatum(top_left); return -1; }
      if (y >= oky && y < oky + 38) { lcd.setTextDatum(top_left); return value; }
      if (y >= by && y < by + bh) {
        int i = (x - 8) / (bw + 8);
        if (i >= 0 && i < 4) nv = value + step[i];
      }
    }
    switch (btNav()) {
      case NAV_LEFT: nv = value - 1; break;
      case NAV_RIGHT: nv = value + 1; break;
      case NAV_UP: nv = value + 10; break;
      case NAV_DOWN: nv = value - 10; break;
      case NAV_OK: lcd.setTextDatum(top_left); return value;
      case NAV_BACK: case NAV_SYS: lcd.setTextDatum(top_left); return -1;
      default: break;
    }
    nv = constrain(nv, 0, 255);
    if (nv != value) { value = nv; drawValue(); }
    delay(10);
  }
}

static void cheatNote(const char *l1, const char *l2, uint16_t c = C_TEXT) {
  lcd.fillScreen(C_BG);
  uiHeader("Cheats");
  uiMessage(l1, l2, c);
  int x, y; waitTap(x, y, 6000);
}

static String hex4(unsigned a) { char b[8]; snprintf(b, sizeof b, "%04X", a & 0xFFFF); return String(b); }

// Turns finder match i into a saved, switched-on cheat.
static void findLock(int i) {
  int cur = cheatPeek(i);
  int v = pickNumber("Lock value", "Keep it at this value", cur);
  if (v < 0) return;
  unsigned a = vSys == SYS_NES ? nes_cheat_addr(i) : gb_cheat_addr(i);
  unsigned addr = a & 0xFFFF, bank = a >> 16;
  Cheat c;
  char code[16];
  if (vSys == SYS_NES) {
    snprintf(code, sizeof code, "%04X:%02X", addr, v);
  } else {
    int tt = (gb_is_cgb() && addr >= 0xD000 && addr < 0xE000) ? 0x90 | bank : 0x01;
    snprintf(code, sizeof code, "%02X%02X%02X%02X", tt, v, addr & 0xFF, addr >> 8);   // GameShark
  }
  c.code = code;
  c.desc = "Found " + hex4(addr) + " = " + String(v);
  c.on = true;
  c.ok = parseCheat(c, vSys == SYS_NES);
  if (cheats.size() >= MAX_CHEATS) { cheatNote("Cheat list is full", "Remove some from the .cht file", C_RED); return; }
  cheats.push_back(c);
  cheatsUpdateActive();
  bool ok = cheatsSave();
  findFree();
  cheatNote(ok ? "Cheat added and switched on" : "Cheat on (couldn't save file)", c.desc.c_str(), ok ? C_GREEN : C_RED);
}

static void cheatFinder() {
  if (!findMask) {
    int v = pickNumber("Find a cheat", "Value on screen now (e.g. lives)", 3);
    if (v < 0) return;
    if (!findStart(v)) { cheatNote("Not enough memory to search", nullptr, C_RED); return; }
    char b[48];
    snprintf(b, sizeof b, "%d places hold %d", findCount, v);
    cheatNote(b, "Play until it changes, then search again");
    return;
  }
  while (findMask) {
    std::vector<MenuItem> it = {
      { "Search the new value", String(findCount) + " left" },
      { "Show the matches", findCount <= 16 ? String(">") : String("too many") },
      { "Start over", "" },
    };
    static int selF = -1;
    int i = runList("Find a cheat", "Back", it, &selF);
    if (i < 0) return;
    if (i == 0) {
      int v = pickNumber("Find a cheat", "Value on screen now", 0);
      if (v < 0) continue;
      findRefine(v);
      if (findCount == 0) { findFree(); cheatNote("No matches left", "Start over with a new search", C_RED); return; }
      char b[48];
      snprintf(b, sizeof b, "%d place%s left", findCount, findCount == 1 ? "" : "s");
      cheatNote(b, findCount <= 16 ? "Show the matches to lock one" : "Play until it changes again");
      if (findCount > 16) return;
    } else if (i == 1) {
      if (findCount > 16) { cheatNote("Too many matches to list", "Search again after it changes"); continue; }
      std::vector<MenuItem> m;
      std::vector<int> idx;
      for (int k = 0; k < findSpace; k++)
        if (findMask[k >> 3] & (1 << (k & 7))) {
          unsigned a = vSys == SYS_NES ? nes_cheat_addr(k) : gb_cheat_addr(k);
          String lab = hex4(a & 0xFFFF);
          if (a >> 16) lab += " (bank " + String(a >> 16) + ")";
          m.push_back({ lab, "= " + String(cheatPeek(k)) });
          idx.push_back(k);
        }
      static int selM = -1;
      int j = runList("Matches: pick one", "Back", m, &selM);
      if (j >= 0 && j < (int)idx.size()) { findLock(idx[j]); return; }
    } else {
      findFree();
      cheatFinder();
      return;
    }
  }
}

static String cheatsLabel() {
  int on = 0;
  for (auto &c : cheats) if (c.on && c.ok) on++;
  return on ? String(on) + " on" : String(">");
}

static void cheatsMenu() {
  while (true) {
    std::vector<MenuItem> it;
    for (auto &c : cheats)   // full names: the list cuts them to fit and scrolls the long ones
      it.push_back({ c.desc, c.ok ? String(c.on ? "On" : "Off") : String("n/a") });
    int nc = (int)cheats.size();
    it.push_back({ "Find a cheat...", findMask ? String(findCount) + " left" : String("") });
    bool anyOn = false;
    for (auto &c : cheats) anyOn |= c.on;
    if (anyOn) it.push_back({ "All cheats off", "" });
    static int selC = -1;
    int i = runList(nc ? "Cheats" : "Cheats (no file yet)", "Back", it, &selC);
    if (i < 0) break;
    if (i < nc) {
      if (!cheats[i].ok) {
        cheatNote("This code type can't be used", "Game Genie codes patch the ROM", C_DIM);
      } else {
        cheats[i].on = !cheats[i].on;
        cheatsDirty = true;
        if (cheats[i].on) ledFlash(LED_FLASH_CHEAT);
      }
    } else if (i == nc) {
      cheatFinder();
    } else {
      for (auto &c : cheats) c.on = false;
      cheatsDirty = true;
    }
    cheatsUpdateActive();
  }
  if (cheatsDirty) cheatsSave();
}

enum PauseResult { P_RESUME, P_RESET, P_QUIT };

// ---- save states (one slot per game, next to the battery saves) ----
static String statePath;    // e.g. /gbc/saves/<name>.st0 (GB) or /gbc/saves/nes/<name>.st0 (NES)

static int stWrite(void *ctx, void *buf, int len) {
  return ((File *)ctx)->write((const uint8_t *)buf, len) == (size_t)len ? 0 : -1;
}
static int stRead(void *ctx, void *buf, int len) {
  return ((File *)ctx)->read((uint8_t *)buf, len) == len ? 0 : -1;
}

// Written to <path>.tmp first and then renamed, so a failed save never damages the old one.
static bool gbStateSave() {
  String tmp = statePath + ".tmp";
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) return false;
  int r = gb_state_save(stWrite, &f);
  f.close();
  if (r != 0) { SD.remove(tmp); return false; }
  SD.remove(statePath);
  return SD.rename(tmp, statePath);
}

static int gbStateLoad() {
  File f = SD.open(statePath, FILE_READ);
  if (!f) return -4;
  if ((long)f.size() != gb_state_size()) { f.close(); return -2; }   // other game/version, or cut short
  int r = gb_state_load(stRead, &f);
  f.close();
  return r;
}

static bool nesStateSave() {
  String tmp = statePath + ".tmp";
  int r = nes_state_save(("/sd" + tmp).c_str());   // stdio path: the card is mounted at /sd
  if (r != 0) { SD.remove(tmp); return false; }
  SD.remove(statePath);
  return SD.rename(tmp, statePath);
}

static void stateMessage(const char *l1, const char *l2, uint16_t c, int ms) {
  uiMessage(l1, l2, c);
  delay(ms);
}

static PauseResult pauseMenu() {
  LedMenuScope ledScope;
  while (true) {
    int sram = gb_sram_size();
    bool haveState = SD.exists(statePath);
    bool isFav = false;
    histForEach([&](const HistLine &h) { if (curGamePath == h.path) isFav = h.fav; });
    std::vector<MenuItem> it = {
      { "Resume", "" },
      { "Save state", haveState ? "replace" : "" },
      { "Load state", haveState ? "" : "empty" },
      { "Cheats", cheatsLabel() },
      { "Favorite", isFav ? "Yes" : "No" },
      { "Write battery save", sram ? String(sram / 1024) + "KB" + (gb_sram_slots() < gb_sram_pages() ? " (paged)" : "")
                                   : String("n/a") },
      { "Volume", cfg.volume ? String(cfg.volume) + "/" + String(VOL_MAX) : String("Mute") },
      { "Scaling", scaleName() },
      { "DMG palette", gb_is_cgb() ? "GBC game" : DMG_PALETTE_NAMES[cfg.dmgPalette % NUM_DMG_PALETTES] },
      { "Show FPS", cfg.showFps ? "On" : "Off" },
      { "LED", ">" },
      { "Reset game", "" },
      { "Save & quit to list", "" },
    };
#if !HAS_LED
    it.erase(it.begin() + 10);   // "LED"
#endif
    static int selPause = -1;
    int i = runList(gb_rom_title(), nullptr, it, &selPause);
#if !HAS_LED
    if (i >= 10) i++;
#endif
    switch (i) {
      case -1:
      case -3:
      case 0: return P_RESUME;
      case 1: {
        uiMessage("Saving state...");
        bool ok = gbStateSave();
        if (ok) ledFlash(LED_FLASH_SAVE);
        stateMessage(ok ? "State saved" : "Save state FAILED", ok ? nullptr : "Check the SD card", ok ? C_GREEN : C_RED, ok ? 600 : 1500);
        if (ok) return P_RESUME;
        break;
      }
      case 2: {
        if (!haveState) { stateMessage("No saved state yet", "Use Save state first", C_DIM, 1200); break; }
        uiMessage("Loading state...");
        int r = gbStateLoad();
        if (r == 0) {
          if (!gb_is_cgb()) lcd_set_dmg_palette(DMG_PALETTES[cfg.dmgPalette % NUM_DMG_PALETTES]);
          stateMessage("State loaded", nullptr, C_GREEN, 500);
          return P_RESUME;
        }
        if (r == -2 || r == -4) { stateMessage("Can't load this state", "It's for another game or version", C_RED, 1800); break; }
        // read error part-way: drop the half-loaded save RAM and restart from the battery save
        gb_sram_reload();
        stateMessage("State file damaged", "The game was restarted", C_RED, 1800);
        return P_RESET;
      }
      case 3: cheatsMenu(); break;
      case 4: histUpdate(curGamePath, HIST_FAV); break;
      case 5:
        if (sram) { bool ok = sramSave(); uiMessage(ok ? "Saved" : "Save FAILED", savePath.c_str(), ok ? C_GREEN : C_RED); delay(900); }
        else {
          // the original cartridge had no save memory: the game uses passwords or doesn't save
          uiMessage("This game has no save memory", "It uses passwords or can't save", C_DIM);
          int x, y; waitTap(x, y, 4000);
        }
        break;
      case 6: cfg.volume = (cfg.volume + 1) % (VOL_MAX + 1); saveSettings(); break;
      case 7: curScale() = (curScale() + 1) % NUM_SCALES; saveSettings(); break;
      case 8:
        if (!gb_is_cgb()) { cfg.dmgPalette = (cfg.dmgPalette + 1) % NUM_DMG_PALETTES; lcd_set_dmg_palette(DMG_PALETTES[cfg.dmgPalette]); saveSettings(); }
        break;
      case 9: cfg.showFps ^= 1; saveSettings(); break;
      case 10: ledMenu(); break;
      case 11: return P_RESET;
      case 12: return P_QUIT;
    }
  }
}

// ----------------------------------------------------------------------------
// Game loop
// ----------------------------------------------------------------------------
// On-screen controls: portrait only; in the BT build hidden when a controller is
// connected (Auto) or always (Never). With them hidden, tapping the screen opens the menu.
static bool wantControls() {
  if ((cfg.rotation & 1) && cfg.padLand == 0) return false;   // landscape pad switched off
#if HAVE_BT
  if (cfg.touchPad == 2) return false;
  if (cfg.touchPad == 0 && btCount() > 0) return false;
#endif
  return true;
}

static void layoutGame() {
  gControls = wantControls();
  layoutControls();
  // the overlay sits on the picture, so landscape keeps the whole screen for the game
  int aw = SCR_W, ah = (gControls && !ctrlOverlay) ? GAME_H : SCR_H;
  int w, h;
  int mode = curScale() % NUM_SCALES;
  if (vSys == SYS_NES && mode < 2) mode = 3;      // NES: 1.5x modes show as Fit
  switch (mode) {
    case 0: case 1: w = 240; h = 216; break;      // 1.5x (Game Boy)
    case 2: w = srcW; h = srcH; break;            // 1x
    case 3:                                       // fit, keep aspect
      if (aw * srcH <= ah * srcW) { w = aw; h = aw * srcH / srcW; } else { h = ah; w = ah * srcW / srcH; }
      break;
    default: w = aw; h = ah; break;               // stretch
  }
  w = min(min(w, aw), DMA_W);
  h = min(h, ah);
  gX = (aw - w) / 2;
  gY = (ah - h) / 2;
  gW = w;
  gH = h;
}

static void enterGameScreen(uint8_t pad) {
  layoutGame();
  xSemaphoreTake(lcdMutex, portMAX_DELAY);
  lcd.fillScreen(TFT_BLACK);
  xSemaphoreGive(lcdMutex);
  if (gControls) drawControls(pad, false);
}

static void runGame(const RomEntry &r) {
  musicStop();   // the game needs the sound output
  String err;
  uint32_t len = loadRomToFlash(r, err);
  if (!len) {
    lcd.fillScreen(C_BG);
    uiHeader("Can't load ROM");
    uiMessage(err.c_str(), "Tap to go back", C_RED);
    int x, y; waitTap(x, y);
    return;
  }
  SD.mkdir("/gbc");
  SD.mkdir("/gbc/saves");
  String b = baseName(String(r.name()));
  savePath = "/gbc/saves/" + b + ".sav";
  rtcPath = "/gbc/saves/" + b + ".rtc";
  statePath = "/gbc/saves/" + b + ".st0";
  String ext = String(r.name()); ext.toLowerCase();
  cheatsLoad(ext.endsWith(".gbc") ? 1 : 0, b);
  LedGameScope ledScope;   // the LED follows "During games" until the game ends

  vSys = SYS_GB; srcW = 160; srcH = 144;
  gb_glue_init(AUDIO_RATE);
  // display handoff: 48 lines (smooth even in Stretch) unless a big ROM needs the RAM
  gb_ring_alloc(romFlashBytes < len ? 32 : 48);
  int loadErr = rom_load(romPtr, len);
  bool streaming = romFlashBytes < (uint32_t)gb_rom_banks() * 16384u;
  // what it takes to start: work RAM + resident cart RAM pages
  // (+ 3 ROM pages and the open ROM file when streaming) + the play reserve
  int needKB = gb_wram_kb() + (gb_sram_pages() ? (streaming ? 4 : 8) : 0) + (streaming ? 12 + 5 : 0) +
               PLAY_RESERVE / 1024;
  bool memOk = loadErr == 0 && gb_alloc_wram() == 0;
  if (memOk && streaming) {
    romFile = SD.open(r.path, FILE_READ);
    memOk = (bool)romFile;
  }
  // streaming: ROM pages matter more, so keep just 1 cart RAM page resident
  if (memOk && !streaming) {
    // Full-frame handoff (144 lines): the emulator never waits for the display,
    // as before 4 MB support. Only if cart RAM + the play reserve still fit after it.
    size_t extra = (size_t)(144 - gb_ring_lines) * 160;
    size_t freeB = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest >= 144 * 160 + 1024 && freeB > extra + gb_sram_size() + PLAY_RESERVE + 8192)
      gb_ring_alloc(144);
    Serial.printf("Display ring: %d lines\n", gb_ring_lines);
  }
  // Cartridge RAM is touched far more often than any one ROM page, and a game whose save
  // RAM does not fit swaps 4 KB to the card on every hop between its halves - which is far
  // worse than losing two ROM pages. Keep it all resident (8 pages covers up to 32 KB).
  if (memOk) memOk = streaming ? setupCartRam(8, 0) : setupCartRam(32, PLAY_RESERVE);
  if (memOk && streaming) {
    const size_t RESERVE = PLAY_RESERVE;
    size_t freeB = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    int slots = freeB > RESERVE ? (int)((freeB - RESERVE) / (4096 + 32)) : 0;
    int got = 0;
    if (slots >= 3 && romLayout) {
      bankMap = (uint8_t **)calloc(gb_rom_banks(), sizeof(uint8_t *));
      if (bankMap) {
        for (int s2 = 0; s2 < romLayoutSlots; s2++)
          if (romLayout[s2] < gb_rom_banks()) bankMap[romLayout[s2]] = (uint8_t *)romPtr + (size_t)s2 * 16384;
        got = gb_rom_stream_setup(bankMap, bankUse, gb_rom_banks(), slots);
      }
    }
    Serial.printf("ROM streaming: %d of %d banks in flash, %d SD pages in RAM\n", romLayoutSlots, gb_rom_banks(), got);
    memOk = got >= 3;
  }
  if (!memOk) {
    if (romFile) romFile.close();
    freeBigRom();
    gb_free_game_memory();
    gb_ring_free();
    unmapRom();
    lcd.fillScreen(C_BG);
    uiHeader("Can't start game");
    char m[64];
    snprintf(m, sizeof m, "Needs %d KB, %u KB free", needKB,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
    uiMessage("Not enough memory for this game", m, C_RED);
    Serial.printf("Not enough memory: need %d KB, free %u, largest %u\n", needKB,
                  heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    int x, y; waitTap(x, y);
    return;
  }
  gb_reset();
  if (!gb_is_cgb()) lcd_set_dmg_palette(DMG_PALETTES[cfg.dmgPalette % NUM_DMG_PALETTES]);
  sramLoad();
  Serial.printf("Game '%s' cgb=%d sram=%d, heap free %u, largest %u\n", gb_rom_title(), gb_is_cgb(), gb_sram_size(),
                heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  uint8_t pad = 0, lastDrawnPad = 0xFF;
  bool menuDown = false, lastMenuDown = false;
  fpsText[0] = 0;
  enterGameScreen(pad);
  audioStart();

  bool renderThis = true;
  int64_t next = esp_timer_get_time();
  uint32_t frames = 0, drawn = 0, autosaveCounter = 0;
  int64_t fpsT0 = next;
  unsigned gbLastWrites = gb_sram_writes;
  int gbDirtyChecks = 0;
  unsigned lastRomFaults = gb_rom_faults + gb_sram_faults;
  unsigned lastRingStalls = gb_ring_stalls;

  while (true) {
    // ---- input ----
    int tx, ty;
    uint8_t newPad = 0;
    bool menuNow = false;
    if (readTouch(tx, ty)) {
      if (gControls) {
        int h = touchToPad(tx, ty);
        if (h == HIT_MENU) menuNow = true; else newPad = (uint8_t)h;
      } else {
        menuNow = true;   // no on-screen pad: tap anywhere = menu
      }
    }
    if (bootRaw()) menuNow = true;
    bool btSys = false;
    uint8_t btPad = btReadPad(&btSys);
    if (btSys) menuNow = true;

    bool openMenu = lastMenuDown && !menuNow;  // on release
    menuDown = menuNow;
    pad = newPad | btPad;
    gb_set_pad(pad);

    // controller (dis)connected -> show/hide the on-screen pad
    if (wantControls() != gControls) {
      waitDisplayIdle();
      enterGameScreen(newPad);
      lastDrawnPad = newPad;
    }
    // only touch presses are echoed on screen (redrawing costs SPI time)
    // the overlay is painted over the game, so every frame erases it and it goes back
    // on after each one; the panel in portrait only changes when a button does
    if (gControls && (ctrlOverlay ? cfg.padLand == 2
                                  : (newPad != lastDrawnPad || menuDown != lastMenuDown))) {
      waitDisplayIdle();
      drawControls(newPad, menuDown);
      lastDrawnPad = newPad;
    }
    lastMenuDown = menuDown;

    if (openMenu) {
      waitDisplayIdle();
      audioStop();
      gb_set_pad(0);
      sramSave();
      PauseResult pr = pauseMenu();
      if (pr == P_QUIT) break;
      if (pr == P_RESET) { gb_reset(); if (!gb_is_cgb()) lcd_set_dmg_palette(DMG_PALETTES[cfg.dmgPalette % NUM_DMG_PALETTES]); }
      {  // don't leak the button that closed the menu into the game
        uint32_t t0 = millis();
        bool sys = true;
        while (millis() - t0 < 1500) { btPoll(); if (!btReadPad(&sys) && !sys) break; delay(10); }
      }
      lastDrawnPad = 0xFF;
      lastMenuDown = false;
      enterGameScreen(0);
      audioStart();
      if (!cfg.volume) audioStop();
      next = esp_timer_get_time();
      continue;
    }

    // ---- emulate one frame ----
    cheatsApply();
    bool render = renderThis && !dispBusy;
    gb_set_framebuffer(nullptr, render);
    gb_run_vblank();              // for a drawn frame this resets the line handoff
    if (render) {                 // the display draws lines as the emulator finishes them
      dispBusy = true;
      xSemaphoreGive(frameReady);
      drawn++;
    }
    gb_run_visible();
    if (render) gb_frame_end();   // releases the display even if the LCD was off
    frames++;

    // ---- pacing ----
    next += FRAME_US;
    int64_t now = esp_timer_get_time();
    if (!audioRunning) {
      if (now < next) {
        int64_t d = next - now;
        if (d > 2000) vTaskDelay(pdMS_TO_TICKS((d - 1000) / 1000));
        while (esp_timer_get_time() < next) { }
      }
      now = esp_timer_get_time();
    }
    // more than a frame behind -> skip drawing the next frame (auto frameskip)
    audioWaitUs = 0;
    int64_t late = now - next;
    renderThis = late < FRAME_US;
    if (audioRunning) {
      // audio is the master clock: absorb drift/lag after one skipped frame
      if (late > FRAME_US) next = now;
    } else if (late > 8 * FRAME_US) {
      next = now;  // way behind (SD access etc.): resync
    }

    // ---- fps / autosave ----
    if (now - fpsT0 >= 1000000) {
      fpsValue = (int)drawn;
      emuFps = (int)frames;
      // every page fetched from the card: ROM banks and cartridge-RAM swaps alike
      sdPerSec = streaming ? (int)(gb_rom_faults + gb_sram_faults - lastRomFaults) : -1;
      lastRomFaults = gb_rom_faults + gb_sram_faults;
      stallsPerSec = (int)(gb_ring_stalls - lastRingStalls);
      lastRingStalls = gb_ring_stalls;
      if (stallsPerSec) Serial.printf("display stalled: %d lines dropped this second\n", stallsPerSec);
      fpsUpdate(fpsValue, emuFps);
      drawn = 0;
      frames = 0;
      fpsT0 = now;
    }
    if (++autosaveCounter >= 600) {  // every ~10 s: battery RAM autosave
      autosaveCounter = 0;
      // Many games (Super Mario Land 2, for one) also use their battery RAM as work RAM,
      // so it changes all the time; writing it on every change stalled the game every
      // ~10 s. Write once the game has stopped writing it (an in-game save is done),
      // or at least once a minute. Pausing and quitting always write it.
      if (gb_sram_dirty()) {
        unsigned w = gb_sram_writes;
        if (w == gbLastWrites || ++gbDirtyChecks >= 6) {
          waitDisplayIdle();
          sramSave();
          gbDirtyChecks = 0;
          next = esp_timer_get_time();
        }
        gbLastWrites = w;
      } else {
        gbDirtyChecks = 0;
      }
    }
  }

  audioStop();
  waitDisplayIdle();
  sramSave();
  cheatsReset();
  gb_free_game_memory();
  gb_ring_free();
  saveBankUse();
  freeBigRom();
  if (romFile) romFile.close();
  unmapRom();
}

// ----------------------------------------------------------------------------
// NES games (nofrendo)
// ----------------------------------------------------------------------------
static String nesSavePath;
static uint32_t nesSramSum = 0;

static void nesSramLoad() {
  uint8_t *p = nes_sram_ptr();
  int n = nes_sram_size();
  if (!p) return;
  File f = SD.open(nesSavePath, FILE_READ);
  if (f) { if ((int)f.size() >= n) f.read(p, n); f.close(); }
  nesSramSum = nes_sram_sum();
}

// Writes the battery RAM if it changed since the last save (or always with force).
static bool nesSramSave(bool force) {
  uint8_t *p = nes_sram_ptr();
  int n = nes_sram_size();
  if (!p) return true;
  uint32_t sum = nes_sram_sum();
  if (!force && sum == nesSramSum) return true;
  File f = SD.open(nesSavePath, FILE_WRITE);
  if (!f) return false;
  bool ok = f.write(p, n) == (size_t)n;
  f.close();
  if (ok) { nesSramSum = sum; ledFlash(LED_FLASH_SAVE); }
  return ok;
}

static void nesResetGame() {
  nesSramSave(false);   // a hard reset clears the battery RAM in memory...
  nes_reset_game();
  nesSramLoad();        // ...so read the saved copy back
}

static PauseResult nesPauseMenu(const char *title) {
  LedMenuScope ledScope;
  while (true) {
    int sram = nes_sram_size();
    bool haveState = SD.exists(statePath);
    bool isFav = false;
    histForEach([&](const HistLine &h) { if (curGamePath == h.path) isFav = h.fav; });
    std::vector<MenuItem> it = {
      { "Resume", "" },
      { "Save state", haveState ? "replace" : "" },
      { "Load state", haveState ? "" : "empty" },
      { "Cheats", cheatsLabel() },
      { "Favorite", isFav ? "Yes" : "No" },
      { "Write battery save", sram ? String(sram / 1024) + "KB" : String("n/a") },
      { "Volume", cfg.volume ? String(cfg.volume) + "/" + String(VOL_MAX) : String("Mute") },
      { "Scaling", scaleName() },
      { "NES palette", NES_PALETTE_NAMES[cfg.nesPalette % NES_NUM_PALETTES] },
      { "Show FPS", cfg.showFps ? "On" : "Off" },
      { "LED", ">" },
      { "Reset game", "" },
      { "Save & quit to list", "" },
    };
#if !HAS_LED
    it.erase(it.begin() + 10);   // "LED"
#endif
    static int selPause = -1;
    int i = runList(title, nullptr, it, &selPause);
#if !HAS_LED
    if (i >= 10) i++;
#endif
    switch (i) {
      case -1:
      case -3:
      case 0: return P_RESUME;
      case 1: {
        uiMessage("Saving state...");
        bool ok = nesStateSave();
        if (ok) ledFlash(LED_FLASH_SAVE);
        stateMessage(ok ? "State saved" : "Save state FAILED", ok ? nullptr : "Check the SD card", ok ? C_GREEN : C_RED, ok ? 600 : 1500);
        if (ok) return P_RESUME;
        break;
      }
      case 2: {
        if (!haveState) { stateMessage("No saved state yet", "Use Save state first", C_DIM, 1200); break; }
        uiMessage("Loading state...");
        int r = nes_state_load(("/sd" + statePath).c_str());
        if (r == 0) { stateMessage("State loaded", nullptr, C_GREEN, 500); return P_RESUME; }
        if (r == -2) { stateMessage("Can't load this state", "It's for another game", C_RED, 1800); break; }
        // read error part-way: restart from the battery save (not the half-loaded RAM)
        nes_reset_game();
        nesSramLoad();
        stateMessage("State file damaged", "The game was restarted", C_RED, 1800);
        return P_RESUME;
      }
      case 3: cheatsMenu(); break;
      case 4: histUpdate(curGamePath, HIST_FAV); break;
      case 5:
        if (sram) { bool ok = nesSramSave(true); uiMessage(ok ? "Saved" : "Save FAILED", nesSavePath.c_str(), ok ? C_GREEN : C_RED); delay(900); }
        else {
          uiMessage("This game has no save memory", "It uses passwords or can't save", C_DIM);
          int x, y; waitTap(x, y, 4000);
        }
        break;
      case 6: cfg.volume = (cfg.volume + 1) % (VOL_MAX + 1); saveSettings(); break;
      case 7: curScale() = (curScale() + 1) % NUM_SCALES; saveSettings(); break;
      case 8:
        cfg.nesPalette = (cfg.nesPalette + 1) % NES_NUM_PALETTES;
        nes_build_palette(cfg.nesPalette, nes_pal565);
        saveSettings();
        break;
      case 9: cfg.showFps ^= 1; saveSettings(); break;
      case 10: ledMenu(); break;
      case 11: return P_RESET;
      case 12: return P_QUIT;
    }
  }
}

static void runNes(const RomEntry &r) {
  musicStop();   // the game needs the sound output
  String err;
  uint32_t len = loadRomToFlash(r, err);
  if (!len) {
    lcd.fillScreen(C_BG);
    uiHeader("Can't load ROM");
    uiMessage(err.c_str(), "Tap to go back", C_RED);
    int x, y; waitTap(x, y);
    return;
  }
  SD.mkdir("/gbc");
  SD.mkdir("/gbc/saves");
  SD.mkdir("/gbc/saves/nes");
  String b = baseName(String(r.name()));
  nesSavePath = "/gbc/saves/nes/" + b + ".sav";
  statePath = "/gbc/saves/nes/" + b + ".st0";
  cheatsLoad(2, b);
  LedGameScope ledScope;
  bool pal = b.indexOf("(E)") >= 0 || b.indexOf("(Europe)") >= 0 || b.indexOf("(A)") >= 0 ||
             b.indexOf("(Australia)") >= 0 || b.indexOf("(PAL)") >= 0;
  int mapper = (romPtr[6] >> 4) | (romPtr[7] & 0xF0);

  int rc = nes_start(romPtr, len, AUDIO_RATE, pal);
  if (rc == 0) {
    // Display handoff: a whole frame (57 KB) if it fits, so the emulator never waits
    // for the display; otherwise smaller rings (the emulator waits a little).
    const int tries[] = { NES_H, 112, 56, 28 };
    for (int L : tries) {
      size_t need = (size_t)L * NES_W;
      size_t freeB = heap_caps_get_free_size(MALLOC_CAP_8BIT);
      size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
      if (L != 28 && (largest < need + 1024 || freeB < need + PLAY_RESERVE + 8192)) continue;
      if (nes_ring_alloc(L) == 0) break;
    }
    Serial.printf("NES: mapper %d, %s, sram %d, ring %d lines, heap free %u, largest %u\n", mapper,
                  nes_is_pal() ? "PAL" : "NTSC", nes_sram_size(), nes_ring_lines,
                  heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  }
  if (rc != 0 || !nes_ring) {
    nes_stop();
    unmapRom();
    lcd.fillScreen(C_BG);
    uiHeader("Can't start game");
    char m[64];
    const char *why = nullptr;
    if (rc == -12) {
      // Licensed games on chips nofrendo doesn't have; anything else is almost always
      // an unlicensed (bootleg) cart such as the NES "Street Fighter II/III" ports.
      static const int licensed[] = { 13, 26, 67, 68, 69, 80, 82, 86, 88, 89, 92, 95, 96, 97, 105,
                                      118, 140, 152, 153, 154, 157, 159, 180, 184, 207, 210 };
      bool lic = false;
      for (int v : licensed) if (v == mapper) lic = true;
      why = lic ? "Not supported yet" : "Probably a bootleg cart";
      snprintf(m, sizeof m, "Mapper %d isn't supported", mapper);
    }
    else if (rc == -13) snprintf(m, sizeof m, "Disk System games aren't supported");
    else if (rc == -3) snprintf(m, sizeof m, "Not a valid NES ROM");
    else snprintf(m, sizeof m, "Not enough memory (%u KB free)", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
    uiMessage(m, why ? why : "Tap to go back", C_RED);
    Serial.printf("NES start failed: rc %d, mapper %d\n", rc, mapper);
    int x, y; waitTap(x, y);
    return;
  }
  vSys = SYS_NES; srcW = NES_W; srcH = NES_H;
  nes_build_palette(cfg.nesPalette, nes_pal565);
  nesSramLoad();
  const int64_t frameUs = nes_is_pal() ? NES_FRAME_US_PAL : NES_FRAME_US;
  uint32_t nesLastSum = nesSramSum;
  int nesDirtyChecks = 0;

  uint8_t pad = 0, lastDrawnPad = 0xFF;
  bool menuDown = false, lastMenuDown = false;
  fpsText[0] = 0;
  enterGameScreen(pad);
  audioStart();

  bool renderThis = true;
  int64_t next = esp_timer_get_time();
  uint32_t frames = 0, drawn = 0, autosaveCounter = 0;
  int64_t fpsT0 = next;

  while (true) {
    // ---- input (same as the Game Boy loop) ----
    int tx, ty;
    uint8_t newPad = 0;
    bool menuNow = false;
    if (readTouch(tx, ty)) {
      if (gControls) {
        int h = touchToPad(tx, ty);
        if (h == HIT_MENU) menuNow = true; else newPad = (uint8_t)h;
      } else {
        menuNow = true;   // no on-screen pad: tap anywhere = menu
      }
    }
    if (bootRaw()) menuNow = true;
    bool btSys = false;
    uint8_t btPad = btReadPad(&btSys);
    if (btSys) menuNow = true;

    bool openMenu = lastMenuDown && !menuNow;  // on release
    menuDown = menuNow;
    pad = newPad | btPad;
    nes_set_pad(pad);

    if (wantControls() != gControls) {
      waitDisplayIdle();
      enterGameScreen(newPad);
      lastDrawnPad = newPad;
    }
    // the overlay is painted over the game, so every frame erases it and it goes back
    // on after each one; the panel in portrait only changes when a button does
    if (gControls && (ctrlOverlay ? cfg.padLand == 2
                                  : (newPad != lastDrawnPad || menuDown != lastMenuDown))) {
      waitDisplayIdle();
      drawControls(newPad, menuDown);
      lastDrawnPad = newPad;
    }
    lastMenuDown = menuDown;

    if (openMenu) {
      waitDisplayIdle();
      audioStop();
      nes_set_pad(0);
      nesSramSave(false);
      PauseResult pr = nesPauseMenu(b.c_str());
      if (pr == P_QUIT) break;
      if (pr == P_RESET) nesResetGame();
      {  // don't leak the button that closed the menu into the game
        uint32_t t0 = millis();
        bool sys = true;
        while (millis() - t0 < 1500) { btPoll(); if (!btReadPad(&sys) && !sys) break; delay(10); }
      }
      lastDrawnPad = 0xFF;
      lastMenuDown = false;
      enterGameScreen(0);
      audioStart();
      if (!cfg.volume) audioStop();
      next = esp_timer_get_time();
      continue;
    }

    // ---- emulate one frame ----
    cheatsApply();
    bool render = renderThis && !dispBusy;
    if (render) {                 // the display draws lines as the emulator finishes them
      nes_begin_frame();
      dispBusy = true;
      xSemaphoreGive(frameReady);
      drawn++;
    }
    nes_run_frame(render);        // also queues the frame's audio
    frames++;

    // ---- pacing (audio is the master clock while sound is on) ----
    next += frameUs;
    int64_t now = esp_timer_get_time();
    if (!audioRunning) {
      if (now < next) {
        int64_t d = next - now;
        if (d > 2000) vTaskDelay(pdMS_TO_TICKS((d - 1000) / 1000));
        while (esp_timer_get_time() < next) { }
      }
      now = esp_timer_get_time();
    }
    audioWaitUs = 0;
    int64_t late = now - next;
    renderThis = late < frameUs;
    if (audioRunning) {
      if (late > frameUs) next = now;
    } else if (late > 8 * frameUs) {
      next = now;
    }

    // ---- fps / autosave ----
    if (now - fpsT0 >= 1000000) {
      fpsValue = (int)drawn;
      emuFps = (int)frames;
      sdPerSec = -1;   // the NES core keeps the whole ROM in flash
      fpsUpdate(fpsValue, emuFps);
      drawn = 0;
      frames = 0;
      fpsT0 = now;
    }
    if (++autosaveCounter >= 600) {  // every ~10 s: battery RAM autosave
      autosaveCounter = 0;
      if (nes_sram_ptr()) {
        // Many NES games also use their battery RAM as everyday work RAM, so it changes
        // all the time; writing it on every change stalled the game (and the sound)
        // every 10 s. Write once it has stopped changing (an in-game save is done),
        // or at least once a minute. Pausing and quitting always write it.
        uint32_t sum = nes_sram_sum();
        if (sum == nesSramSum) {
          nesDirtyChecks = 0;
        } else if (sum == nesLastSum || ++nesDirtyChecks >= 6) {
          waitDisplayIdle();
          nesSramSave(false);
          nesDirtyChecks = 0;
          next = esp_timer_get_time();
        }
        nesLastSum = sum;
      }
    }
  }

  audioStop();
  waitDisplayIdle();
  nesSramSave(false);
  cheatsReset();
  nes_stop();
  unmapRom();
  vSys = SYS_GB; srcW = 160; srcH = 144;
}

// ----------------------------------------------------------------------------
// ROM browser
// ----------------------------------------------------------------------------
static int browserTop = 0;
static int browserSel = -1;   // controller highlight (-1 = none yet)

static bool browserByPad = false;  // last browser action came from the controller

static void drawFolderIcon(int x, int y, uint16_t c) {
  lcd.fillRoundRect(x, y + 2, 7, 4, 1, c);        // tab
  lcd.fillRoundRect(x, y + 4, 16, 11, 2, c);      // body
}

#define BROWSER_ROW_H 32
static int browserMarq = -1;   // entry whose name is scrolling (-1 = none)

// ---- A-Z jump: "#" (numbers and symbols) and A to Z. Landscape shows a strip of letters
// under the title bar; portrait has an "A-Z" button that opens a grid of letters.
// Controller: Select opens it. Only shown when the folder doesn't fit on one screen. ----
#define AZ_N 27
#define AZ_STRIP_H 14   // 34 + 14 + 6 rows x 32 = 240: six games in landscape
static uint32_t azMask = 0;   // letters that have at least one entry
static int azLetter(const char *n) {
  if (const char *sl = strrchr(n, '/')) n = sl + 1;   // full paths in the Favorites etc. lists
  int c = toupper((unsigned char)n[0]);
  return c >= 'A' && c <= 'Z' ? c - 'A' + 1 : 0;
}
static bool azEntry(const Ent &e) { return e.kind != K_UP && e.kind != K_MORE && e.kind < K_CONT; }
static bool azNeeded() { return (int)roms.size() > (SCR_H - 34) / BROWSER_ROW_H; }
static bool azStripOn() { return SCR_W > SCR_H && azNeeded(); }
static bool azButtonOn() { return SCR_W <= SCR_H && azNeeded(); }
static int browserY0() { return 34 + (azStripOn() ? AZ_STRIP_H : 0); }
// No touch screen and no controller yet: a pairing banner fills the bottom of the game list
#define PAIR_BANNER_H 52
static int pairBannerH() { return bootNavMode() ? PAIR_BANNER_H : 0; }
static int browserVisible() { return (SCR_H - browserY0() - pairBannerH()) / BROWSER_ROW_H; }
static void drawPairBanner() {
  int h = pairBannerH();
  if (!h) return;
  int y = SCR_H - h;
  lcd.fillRect(0, y, SCR_W, h, C_PANEL);
  lcd.drawFastHLine(0, y, SCR_W, C_ACCENT);
  lcd.setTextDatum(top_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_ACCENT, C_PANEL);
  lcd.drawString("Pair a controller to play", 6, y + 3);
  lcd.setFont(&fonts::Font0);
  lcd.setTextColor(C_TEXT, C_PANEL);
  lcd.drawString("PS4: SHARE+PS   PS5: CREATE+PS", 6, y + 21);
  lcd.drawString("Xbox: pair button   Switch: sync", 6, y + 31);
  lcd.setTextColor(C_DIM, C_PANEL);
  lcd.drawString("BOOT: tap = down, hold = select", 6, y + 41);
  lcd.setFont(&fonts::Font2);
}
static void azBuild() {
  azMask = 0;
  for (auto &e : roms) if (azEntry(e)) azMask |= 1u << azLetter(e.name);
}
static int azFirst(int L) {
  for (size_t i = 0; i < roms.size(); i++)
    if (azEntry(roms[i]) && azLetter(roms[i].name) == L) return (int)i;
  return -1;
}
static int azCurrent() {   // letter of the first game or folder on screen
  for (int i = browserTop; i < (int)roms.size(); i++) if (azEntry(roms[i])) return azLetter(roms[i].name);
  return -1;
}
static char azChar(int L) { return L ? (char)('A' + L - 1) : '#'; }
static uint16_t azDimColor() { return mixHex(theme().bg, theme().dim, 90); }

static void azDrawStrip(int focus) {
  const float cw = (SCR_W - 8) / (float)AZ_N;
  const int y = 34, cur = azCurrent();
  lcd.startWrite();
  lcd.fillRect(0, y, SCR_W, AZ_STRIP_H, C_BG);
  lcd.setFont(&fonts::Font0);   // small 6x8 letters
  lcd.setTextDatum(middle_center);
  for (int i = 0; i < AZ_N; i++) {
    int cx = 4 + (int)(cw * (i + 0.5f));
    bool have = azMask & (1u << i);
    uint16_t bg = C_BG;
    if (i == focus) {
      bg = C_HILITE;
      lcd.fillRoundRect(cx - 5, y, 11, AZ_STRIP_H - 1, 3, bg);
      lcd.drawRoundRect(cx - 5, y, 11, AZ_STRIP_H - 1, 3, C_ACCENT);
    }
    lcd.setTextColor(i == cur ? C_ACCENT : have ? C_TEXT : azDimColor(), bg);
    char t[2] = { azChar(i), 0 };
    lcd.drawString(t, cx + 1, y + 6);
    if (i == cur && focus < 0) lcd.drawFastHLine(cx - 3, y + AZ_STRIP_H - 3, 7, C_ACCENT);
  }
  lcd.setTextDatum(top_left);
  lcd.setFont(&fonts::Font2);
  lcd.endWrite();
}

// Right end of the name area of a row (the type and size sit to its right).
static String playTime(uint32_t s) {
  if (s < 3600) return String(max(1u, (unsigned)(s / 60))) + " min";
  return String((unsigned)(s / 3600)) + "h " + String((unsigned)(s / 60 % 60)) + "m";
}
static bool iconRow(const Ent &e) { return e.kind == K_DIR || e.kind >= K_CONT; }
static String browserTag(Ent &e) {
  if (e.kind == K_DIR || e.kind >= K_VFAV) return ">";
  if (curDir == VDIR_PREFIX "Most played" && e.size != SIZE_UNKNOWN) return playTime(e.size);
  const char *tag = e.kind == K_NES ? "NES" : isCgbName(e.name) ? "GBC" : "GB";
  uint32_t sz = entrySize(e);
  return sz == SIZE_UNKNOWN ? String(tag) : String(tag) + " " + String(sz / 1024) + "K";
}
static String browserName(Ent &e) {
  if (e.kind == K_DIR || e.kind >= K_VFAV) return String(e.name);
  String n = baseName(fileName(e.name));
  return e.kind == K_CONT ? "Continue: " + n : n;
}

// Small code-drawn icons for the special rows
static void drawStar(int cx, int cy, int r, uint16_t c) {
  float px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float a = -1.5708f + i * 0.6283f, rr = (i & 1) ? r * 0.42f : r;
    px[i] = cx + cosf(a) * rr; py[i] = cy + sinf(a) * rr;
  }
  for (int i = 0; i < 10; i++) lcd.fillTriangle(cx, cy, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], c);
}
static void drawRowIcon(const Ent &e, int x, int yc) {
  switch (e.kind) {
    case K_DIR: drawFolderIcon(x, yc - 9, C_BRONZE); break;
    case K_VFAV: drawStar(x + 8, yc, 8, C_ACCENT); break;
    case K_VRECENT:   // clock
      lcd.drawCircle(x + 8, yc, 7, C_ACCENT);
      lcd.drawFastVLine(x + 8, yc - 4, 5, C_ACCENT);
      lcd.drawLine(x + 8, yc, x + 11, yc + 2, C_ACCENT);
      break;
    case K_VMOST:     // bars
      for (int i = 0; i < 3; i++) lcd.fillRect(x + 2 + i * 5, yc + 6 - (i + 1) * 4, 4, (i + 1) * 4, C_ACCENT);
      break;
    case K_CONT:      // play
      lcd.fillTriangle(x + 3, yc - 7, x + 3, yc + 7, x + 15, yc, C_GREEN);
      break;
  }
}
static uint16_t browserFg(Ent &e) {
  return e.kind == K_DIR || e.kind >= K_CONT ? C_TEXT
       : e.kind == K_NES ? RGB565(0xE0, 0xA8, 0xA0)
       : isCgbName(e.name) ? C_TEXT : RGB565(0xD2, 0xBE, 0x96);
}

// scroll: the name of this row scrolls (it's selected, or its turn in touch mode)
static void drawBrowserRow(int idx, bool scroll) {
  const int rowH = BROWSER_ROW_H, y0 = browserY0();
  int i = idx - browserTop;
  if (i < 0 || i >= browserVisible() || idx >= (int)roms.size()) return;
  Ent &e = roms[idx];
  int y = y0 + i * rowH;
  bool hi = idx == browserSel;
  uint16_t bg = hi ? C_HILITE : C_PANEL;
  if (browserMarq == idx && !scroll) { marqueeOff(); browserMarq = -1; }
  if (!hi && selGlow.on && selGlow.y == y + 1) glowOff();
  lcd.fillRoundRect(4, y + 1, SCR_W - 8, rowH - 2, 5, bg);
  lcd.drawRoundRect(3, y, SCR_W - 6, rowH, 6, C_BG);   // clears an old halo
  if (hi) { lcd.drawRoundRect(4, y + 1, SCR_W - 8, rowH - 2, 5, C_ACCENT); glowSet(4, y + 1, SCR_W - 8, rowH - 2, bg); }
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(middle_left);
  const int yc = y + rowH / 2;
  if (e.kind == K_UP) {
    lcd.setTextColor(C_DIM, bg);
#if HAVE_BT
    drawPadIcon(16, yc, backIcon());
    lcd.drawString("Back", 28, yc);
#else
    lcd.drawString("< Back", 10, yc);
#endif
    lcd.setTextDatum(top_left);
    return;
  }
  if (e.kind == K_MORE) {
    lcd.setTextColor(C_RED, bg);
    lcd.drawString(String(hiddenCount) + " more not shown (low RAM)", 10, yc);
    lcd.setTextDatum(top_left);
    return;
  }
  int tx = 10;
  if (iconRow(e)) { drawRowIcon(e, 10, yc); tx = 32; }
  String tag = browserTag(e);
  lcd.setTextColor(C_DIM, bg);
  lcd.setTextDatum(middle_right);
  lcd.drawString(tag, SCR_W - (tag == ">" ? 12 : 10), yc);
  int nx1 = SCR_W - 10 - lcd.textWidth(tag) - 10;
  if (e.fav) { drawStar(nx1 - 4, yc, 5, C_ACCENT); nx1 -= 14; }   // a favorite
  String n = browserName(e);
  uint16_t fg = browserFg(e);
  lcd.setTextDatum(middle_left);
  if (scroll && marqueeSet(tx, nx1, yc, n, fg, bg)) {
    browserMarq = idx;
    marqueeDraw(0);
  } else {
    if (browserMarq == idx) { marqueeOff(); browserMarq = -1; }
    if (lcd.textWidth(n) > nx1 - tx) {   // cut off: end it with "~"
      while (n.length() > 1 && lcd.textWidth(n + "~") > nx1 - tx) n.remove(n.length() - 1);
      n += "~";
    }
    lcd.setTextColor(fg, bg);
    lcd.drawString(n, tx, yc);
  }
  lcd.setTextDatum(top_left);
}

static void drawBrowser() {
  const int rowH = BROWSER_ROW_H, y0 = browserY0();
  glowOff(); marqueeOff(); browserMarq = -1;
  if (azStripOn()) azDrawStrip(-1);
  lcd.startWrite();
  const int y1 = SCR_H - pairBannerH();   // bottom of the list
  lcd.fillRect(0, y0, SCR_W, y1 - y0, C_BG);
  int visible = browserVisible();
  for (int i = 0; i < visible; i++) drawBrowserRow(browserTop + i, browserTop + i == browserSel);
  // scrollbar
  if ((int)roms.size() > visible) {
    int h = (y1 - y0) * visible / roms.size();
    int y = y0 + (y1 - y0 - h) * browserTop / max(1, (int)roms.size() - visible);
    lcd.fillRect(SCR_W - 3, y0, 3, y1 - y0, C_BG);
    lcd.fillRect(SCR_W - 3, y, 3, h, C_ACCENT);
  }
  drawPairBanner();
  lcd.endWrite();
}

// With no row selected (touch), cut-off names take turns scrolling, one at a time.
static void browserMarqueeRotate() {
  if (browserSel >= 0 || roms.empty() || (marq.on && marq.loops == 0)) return;
  int visible = browserVisible();
  int start = browserMarq >= browserTop ? browserMarq - browserTop + 1 : 0;
  int prev = browserMarq;
  lcd.setFont(&fonts::Font2);
  for (int k = 0; k < visible; k++) {
    int idx = browserTop + (start + k) % visible;
    if (idx >= (int)roms.size() || idx == prev) continue;
    Ent &e = roms[idx];
    if (e.kind == K_UP || e.kind == K_MORE) continue;
    int tx = iconRow(e) ? 32 : 10;
    int nx1 = SCR_W - 10 - lcd.textWidth(browserTag(e)) - 10 - (e.fav ? 14 : 0);
    if (lcd.textWidth(browserName(e)) <= nx1 - tx) continue;
    lcd.startWrite();
    if (prev >= 0) drawBrowserRow(prev, false);   // back to its "~" form
    drawBrowserRow(idx, true);
    lcd.endWrite();
    return;
  }
  if (prev >= 0 && marq.loops > 0) { marq.loops = 0; }   // the only long name: it scrolls again
}

// Shows the first entry with letter L at the top (select: also highlight it).
static void azJump(int L, bool select) {
  int t = azFirst(L);
  if (t < 0) return;
  browserTop = constrain(t, 0, max(0, (int)roms.size() - browserVisible()));
  if (select) browserSel = t;
  drawBrowser();
}
static int azStep(int L, int d) {   // next letter that has entries, either way, wrapping
  for (int k = 1; k <= AZ_N; k++) {
    int n = ((L + d * k) % AZ_N + AZ_N) % AZ_N;
    if (azMask & (1u << n)) return n;
  }
  return L;
}

// Landscape, controller: Select puts the highlight on the letter strip. Left / right pick
// a letter (the list follows), OK / Select / Down go to it, Back returns to where you were.
static void azStripFocus() {
  int L = azCurrent();
  if (L < 0 || !(azMask & (1u << L))) L = azStep(0, 1);
  int top0 = browserTop, sel0 = browserSel;
  azJump(L, false);
  azDrawStrip(L);
  btNavReset();
  while (true) {
    int ev = btNav();
    if (ev == NAV_LEFT || ev == NAV_RIGHT || ev == NAV_UP) {
      L = azStep(L, ev == NAV_LEFT || ev == NAV_UP ? -1 : 1);
      azJump(L, false);
      azDrawStrip(L);
    } else if (ev == NAV_OK || ev == NAV_SELECT || ev == NAV_DOWN) {
      browserSel = azFirst(L);
      drawBrowser();
      return;
    } else if (ev == NAV_BACK || ev == NAV_SYS) {
      browserTop = top0; browserSel = sel0;
      drawBrowser();
      return;
    }
    int x, y;
    if (readTouch(x, y)) { while (readTouch(x, y)) delay(10); drawBrowser(); return; }
    uiTick();
    delay(10);
  }
}

// Portrait: a grid of big letter buttons over the list. Returns a letter, or -1.
static int azGrid() {
  const int cols = 6, rows = 5, gx = 6, gy = 40;
  const int cw = (SCR_W - 12) / cols, ch = min(46, (SCR_H - gy - 6) / rows);
  int f = azCurrent();
  if (f < 0 || !(azMask & (1u << f))) f = azStep(0, 1);
  auto cellRect = [&](int i, int &x, int &y, int &w) {   // 27 = "Cancel" (last three cells)
    x = gx + (i % cols) * cw; y = gy + (i / cols) * ch; w = i == AZ_N ? cw * 3 : cw;
  };
  auto drawCell = [&](int i) {
    int x, y, w;
    cellRect(i, x, y, w);
    bool foc = i == f;
    uint16_t bg = foc ? C_HILITE : C_PANEL;
    lcd.fillRoundRect(x + 2, y + 2, w - 4, ch - 4, 6, bg);
    lcd.drawRoundRect(x + 2, y + 2, w - 4, ch - 4, 6, foc ? C_ACCENT : bg);
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(middle_center);
    if (i == AZ_N) {
      lcd.setTextColor(C_DIM, bg);
      lcd.drawString("Cancel", x + w / 2, y + ch / 2);
    } else {
      lcd.setTextSize(2);
      lcd.setTextColor(azMask & (1u << i) ? C_TEXT : azDimColor(), bg);
      char t[2] = { azChar(i), 0 };
      lcd.drawString(t, x + w / 2, y + ch / 2 + 1);
      lcd.setTextSize(1);
    }
    lcd.setTextDatum(top_left);
  };
  glowOff(); marqueeOff(); browserMarq = -1;
  lcd.fillRect(0, 34, SCR_W, SCR_H - 34, C_BG);
  for (int i = 0; i <= AZ_N; i++) drawCell(i);
  btNavReset();
  int x, y;
  while (readTouch(x, y)) delay(10);
  while (true) {
    int ev = btNav(), nf = f;
    if (ev == NAV_LEFT) nf = f == AZ_N ? AZ_N - 1 : max(0, f - 1);
    else if (ev == NAV_RIGHT) nf = min(AZ_N, f + 1);
    else if (ev == NAV_UP) nf = f == AZ_N ? AZ_N - 3 : max(0, f - cols);
    else if (ev == NAV_DOWN) nf = min(AZ_N, f + cols);
    else if (ev == NAV_OK) { if (f == AZ_N) return -1; if (azMask & (1u << f)) return f; }
    else if (ev == NAV_BACK || ev == NAV_SELECT || ev == NAV_SYS) return -1;
    if (nf != f) { int o = f; f = nf; drawCell(o); drawCell(f); }
    if (readTouch(x, y)) {
      int sx = x, sy = y;
      while (readTouch(x, y)) delay(10);
      int c = (sx - gx) / cw, r = (sy - gy) / ch;
      if (sx < gx || sy < gy || c >= cols || r >= rows) return -1;
      int i = min(AZ_N, r * cols + c);
      if (i == AZ_N) return -1;
      if (azMask & (1u << i)) return i;
    }
    uiTick();
    delay(10);
  }
}

static int runBrowserLoop();
// Returns an index into roms, -1 = Setup, -2 = rescan, -4 = redraw, -5 = folder up
static int runBrowser() {
  int r = runBrowserLoop();
  glowOff(); marqueeOff(); browserMarq = -1; hdrOn = false;   // the menu animation stops here
  return r;
}

static int runBrowserLoop() {
  azBuild();
  const int rowH = BROWSER_ROW_H, y0 = browserY0();
  int visible = browserVisible();
  const bool azBtn = azButtonOn();
  lcd.fillScreen(C_BG);
  char title[64];
  lcd.setFont(&fonts::Font2);
  if (curDir == rootDir) {
    // main menu: name and version
    snprintf(title, sizeof title, APP_NAME " v" APP_VERSION);
  } else if (isVirtualDir(curDir)) {
    snprintf(title, sizeof title, "%s", curDir.c_str() + 2);   // Favorites / Recent / Most played
  } else {
    // inside a folder: its path below the games root, e.g. "GBC / RPG"
    String rel = curDir.substring(rootDir == "/" ? 1 : rootDir.length() + 1);
    rel.replace("/", " / ");
    while (rel.length() > 4 && lcd.textWidth(rel) > SCR_W - 76 - (azBtn ? 42 : 0)) rel = "~" + rel.substring(2);
    snprintf(title, sizeof title, "%s", rel.c_str());
  }
  uiHeader(title, "Setup", HAVE_BT ? "Options" : nullptr);   // PS4 Options (Start) opens Setup
  if (azBtn) {   // portrait: A-Z button left of Setup
    lcd.fillRoundRect(SCR_W - 106, 4, 38, 24, 6, C_PANEL);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_TEXT, C_PANEL);
    lcd.setTextDatum(middle_center);
    lcd.drawString("A-Z", SCR_W - 87, 16);
    lcd.setTextDatum(top_left);
    hdrReserve(SCR_W - 106);
  }
  headerHints(title, azBtn ? SCR_W - 110 : SCR_W - 68, "Open", curDir != rootDir ? "Up" : nullptr);
  bool empty = romFiles == 0 && (roms.empty() || (roms.size() == 1 && roms[0].kind == K_UP));
  if (!sdOk) { uiMessage("No SD card found", "Insert card, then tap to retry", C_RED); }
  else if (roms.empty()) { uiMessage("No games found", "Put them in /Roms/GB, /Roms/GBC, /Roms/NES", C_RED); }
  else {
    browserTop = constrain(browserTop, 0, max(0, (int)roms.size() - visible));
    if (browserSel >= (int)roms.size()) browserSel = -1;
    drawBrowser();
    if (empty) {
      lcd.setFont(&fonts::Font2);
      lcd.setTextColor(C_DIM, C_BG);
      lcd.setTextDatum(middle_center);
      lcd.drawString("No games in this folder", SCR_W / 2, y0 + rowH * 2 + 8);
      lcd.setTextDatum(top_left);
    }
  }
  btNavReset();
  bool bannerWas = pairBannerH() > 0;

  while (true) {
    int x, y;
    if ((pairBannerH() > 0) != bannerWas) return -4;   // controller came or went: redraw
    // controller: d-pad moves, A/Start opens, B goes up a folder, Select opens Setup
    int ev = btNav();
    if (ev != NAV_NONE) { browserByPad = true; idlePoke(); }
    if (ev == NAV_OK && navFromStart) return -1;   // Options: Setup
    if (ev == NAV_SELECT && sdOk && azNeeded()) {   // Select: A-Z
      browserByPad = true;
      if (azStripOn()) azStripFocus();
      else { int L = azGrid(); if (L >= 0) azJump(L, true); else drawBrowser(); }
      continue;
    }
    if (ev == NAV_BACK && sdOk && curDir != rootDir) return -5;
#if TOUCH_KIND == TOUCH_NONE
    if (ev == NAV_BACK) return -1;   // no touch: back at the top opens Setup (BOOT held 2 s)
#endif
    if (!roms.empty() && sdOk && ev != NAV_NONE) {
      int n = (int)roms.size(), s0 = browserSel;
      if (ev == NAV_UP) browserSel = browserSel <= 0 ? n - 1 : browserSel - 1;
      else if (ev == NAV_DOWN) browserSel = (browserSel < 0 || browserSel >= n - 1) ? 0 : browserSel + 1;
      else if (ev == NAV_LEFT) browserSel = max(0, (browserSel < 0 ? 0 : browserSel) - visible);
      else if (ev == NAV_RIGHT) browserSel = min(n - 1, (browserSel < 0 ? 0 : browserSel) + visible);
      else if (ev == NAV_OK) { if (browserSel >= 0) return browserSel; browserSel = 0; }
      if (browserSel != s0) {
        int t0 = browserTop;
        if (browserSel < browserTop) browserTop = browserSel;
        if (browserSel >= browserTop + visible) browserTop = browserSel - visible + 1;
        if (browserTop != t0 || s0 < 0) drawBrowser();
        else { lcd.startWrite(); drawBrowserRow(s0, false); drawBrowserRow(browserSel, true); lcd.endWrite(); }
      }
    } else if ((!sdOk || roms.empty()) && ev == NAV_OK) {
      return -2;  // rescan
    }
    if (bootPressed()) {
      uint32_t t0 = millis();
      while (bootPressed() && millis() - t0 < 1000) delay(10);
      if (millis() - t0 >= 1000) {   // held 1 s: next orientation
        cycleOrientation();
        applyDisplaySettings(false);
        saveSettings();
        while (bootPressed()) delay(10);
        return -4;                     // redraw
      }
      if (curDir != rootDir) return -5;   // short press: folder up
    }
    if (const char *cmd = xferPollLine()) {   // the "Send games" page said hello
      if (!strcmp(cmd, "HELLO")) return -6;
    }
    if (!readTouch(x, y)) {
      if (sdOk && !roms.empty()) { browserMarqueeRotate(); uiTick(); }
      // Ask for a full redraw rather than calling drawBrowser(): that only paints the
      // list from y0 down, so the title bar, the Setup and A-Z buttons and the letter
      // strip stayed blank after the screensaver had cleared the screen.
      if (screensaverIfIdle()) return -4;
      delay(10);
      continue;
    }
    browserByPad = false;
    int sx = x, sy = y, moved = 0;
    int topAtStart = browserTop;
    uint32_t pressT0 = millis();
    bool held = false;
    int selWas = browserSel, press = -1;
    if (sy >= y0 && !roms.empty()) {
      int i = browserTop + (sy - y0) / rowH;
      if (i >= 0 && i < (int)roms.size() && i < browserTop + visible) {
        press = i; browserSel = i;
        lcd.startWrite(); drawBrowserRow(i, true); lcd.endWrite();
      }
    }
    Drag dg; dragBegin(dg, y);
    while (dragNext(dg, x, y)) {
      moved = max(moved, abs(y - sy));
      if (press >= 0 && moved > 10) {          // a scroll, not a choice
        int was = press; press = -1; browserSel = selWas;
        lcd.startWrite();
        drawBrowserRow(was, was == browserSel);
        if (selWas >= 0 && selWas != was) drawBrowserRow(selWas, true);
        lcd.endWrite();
      }
      // hold a game for half a second: mark or unmark it as a favorite
      if (!held && moved <= 10 && sy >= y0 && sdOk && millis() - pressT0 > 500) {
        held = true;
        int i = browserTop + (sy - y0) / rowH;
        if (i >= 0 && i < (int)roms.size() && i < browserTop + visible && roms[i].kind <= K_NES) {
          roms[i].fav = histUpdate(entryPath(roms[i]), HIST_FAV);
          ledFlash(LED_FLASH_CHEAT);
          lcd.startWrite(); drawBrowserRow(i, i == browserSel); lcd.endWrite();
        }
      }
      if (moved > 10 && !roms.empty() && sy >= y0) {
        int nt = constrain(topAtStart - (y - sy) / (rowH / 2) , 0, max(0, (int)roms.size() - visible));
        if (nt != browserTop) { browserTop = nt; drawBrowser(); }
      }
      delay(15);
    }
    if (moved > 10 || held) {
      if (press >= 0) {   // a long press that marked a favorite still lit the row
        browserSel = selWas;
        lcd.startWrite(); drawBrowserRow(press, press == browserSel); lcd.endWrite();
      }
      continue;
    }
    if (sy < 32) {
      if (sx > SCR_W - 70) return -1;  // settings
      if (azBtn && sx > SCR_W - 108) { int L = azGrid(); if (L >= 0) azJump(L, false); else drawBrowser(); }
      continue;
    }
    if (azStripOn() && sy < y0) {      // tap a letter
      int L = (int)((sx - 4) * AZ_N / (float)(SCR_W - 8));
      if (L >= 0 && L < AZ_N && (azMask & (1u << L))) azJump(L, browserSel >= 0);
      continue;
    }
    if (!sdOk || roms.empty()) return -2;  // rescan
    int i = browserTop + (sy - y0) / rowH;
    if (i >= 0 && i < (int)roms.size()) { browserSel = i; return i; }
  }
}

// Puts the highlight on the entry with this path (or none) and scrolls to it.
static void browserSelectPath(const String &path) {
  browserSel = -1;
  int sl = path.lastIndexOf('/');
  String dir = sl <= 0 ? String("/") : path.substring(0, sl);
  const char *name = path.c_str() + sl + 1;
  if (dir == curDir)
    for (size_t i = 0; i < roms.size(); i++)
      if (roms[i].kind != K_UP && roms[i].kind != K_MORE && !strcmp(roms[i].name, name)) { browserSel = (int)i; break; }
  int visible = browserVisible();
  if (browserSel >= 0) {
    if (browserSel < browserTop || browserSel >= browserTop + visible) browserTop = max(0, browserSel - visible / 2);
  } else {
    browserTop = 0;
  }
}

static void enterDir(const String &dir) {
  String from = curDir;
  bool up = dir.length() < from.length() && from.startsWith(dir);
  curDir = dir;
  listDir();
  browserTop = 0;
  browserSel = -1;
  if (up) browserSelectPath(from);                  // land on the folder we came from
  else if (browserByPad && !roms.empty()) browserSel = roms[0].kind == K_UP && roms.size() > 1 ? 1 : 0;
}

// Arduino loop task stack (default 8 KB); RAM is tight once Bluetooth is running
size_t getArduinoLoopTaskStackSize(void) { return 6144; }

// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(XFER_BAUD);   // one speed for the boot log and USB transfers
  Serial.println("\n" APP_NAME " " APP_VERSION " for the " BOARD_NAME);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
#ifdef AUDIO_EN_PIN
  pinMode(AUDIO_EN_PIN, OUTPUT);       // switch the speaker amplifier on
#ifndef AUDIO_EN_LEVEL
#define AUDIO_EN_LEVEL HIGH
#endif
  digitalWrite(AUDIO_EN_PIN, AUDIO_EN_LEVEL);
#endif
  lcdMutex = xSemaphoreCreateMutex();
  frameReady = xSemaphoreCreateBinary();

  loadSettings();
  // hold BOOT during power-up to reset display/touch settings
  bool bootHeldAtStart = bootRaw();
  if (bootHeldAtStart) { cfg = Settings(); saveSettings(); }
  applyTheme();
  ledInit();

  lcd.selectPanel(cfg.panelType);
  const char *touchChip = lcd.detectTouch();   // 2.8" / 3.2" / 3.5" capacitive: GT911 or CST820
  lcd.setRgbOrder(cfg.swapRB);
  lcd.init();
  lcd.clearWholeScreen();
  if (*touchChip) Serial.printf("Touch: %s\n", touchChip);
  applyDisplaySettings(false);
  lcd.fillScreen(C_BG);

  // Tell the user why the board last restarted (there's no serial monitor on a handheld)
  {
    const char *why = nullptr, *hint = nullptr;
    switch (esp_reset_reason()) {
      case ESP_RST_BROWNOUT: why = "Restarted: power dip (brownout)";
                             hint = "Check USB cable / power supply"; break;
      case ESP_RST_PANIC:    why = "Restarted: software crash";
                             hint = "Note what you were doing"; break;
      case ESP_RST_INT_WDT:
      case ESP_RST_TASK_WDT:
      case ESP_RST_WDT:      why = "Restarted: froze (watchdog)";
                             hint = "Note what you were doing"; break;
      default: break;
    }
    if (why) {
      Serial.println(why);
      uiMessage(why, hint, C_RED);
      int x, y;
      waitTap(x, y, 6000);
    }
  }

  dmaBuf[0] = (uint16_t *)heap_caps_malloc(DMA_W * DMA_LINES * 2, MALLOC_CAP_DMA);
  dmaBuf[1] = (uint16_t *)heap_caps_malloc(DMA_W * DMA_LINES * 2, MALLOC_CAP_DMA);
  if (!dmaBuf[0] || !dmaBuf[1]) gb_fatal(__FILE__, __LINE__, "Out of memory for display buffers");
  splashStart();   // animates on core 0 while the rest starts up
  if (gb_glue_init(AUDIO_RATE) != 0) gb_fatal(__FILE__, __LINE__, "Out of memory for audio buffer");
  gb_ring_free();   // the display ring is allocated per game (GB) or not at all (NES)

  romPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "rom");
  if (!romPart) gb_fatal(__FILE__, __LINE__, "Flash partition 'rom' missing.\nFlash the merged .bin at 0x0.");

  audioInit();
  musicInit();
  musicStart();   // from here on there is music: no card, a bad card or an error does not stop it
  if (cfg.btEnabled) btInit();   // Bluetooth off: don't start it at all (frees its RAM)
#if TOUCH_KIND == TOUCH_NONE
  if (bootHeldAtStart) btForgetAll();   // BOOT held at power-up: also forget controllers
#endif
  xTaskCreatePinnedToCore(displayTask, "disp", 3072, nullptr, 2, nullptr, 0);

  sdInit();
  if (sdOk) scanRoms();
  // roms[] is this folder's listing, not every game on the card: it holds sub-folders
  // and the Continue / Favorites / Recent / Most played rows as well as games, and the
  // card is only ever listed one folder at a time to keep the RAM down. Calling that
  // figure "ROMs" made the board look like it had found nine games when it had found
  // nine rows in /Roms, most of them folders.
  Serial.printf("SD %s, %s: %d rows, %d game%s in this folder; heap free %u, largest %u\n",
                sdOk ? "ok" : "FAIL", curDir.c_str(), (int)roms.size(), romFiles,
                romFiles == 1 ? "" : "s",
                heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  { std::vector<MenuItem> it; sysInfoCollect(it); sysInfoSerial(it); }   // what this board is
  splashFinish();   // lets the animation finish (tap to skip) and frees its memory
#if TOUCH_ASK_CAL
  // A resistive screen needs calibrating once (Setup > Display & touch can redo it).
  // Until it's done the board asks at every start; BOOT skips it.
  if (!cfg.calValid) calibrateTouch();
#endif
}

void loop() {
  musicStart();   // menu music (if on); stops again before a game
  int sel = runBrowser();
  if (sel == -1) {
    settingsMenu();
    if (xferReceived && sdOk) { xferReceived = 0; listDir(); }   // new games from USB
    return;
  }
  if (sel == -6) {   // USB transfer started from the computer
    xferMode(true);
    if (xferReceived && sdOk) { xferReceived = 0; listDir(); }
    return;
  }
  if (sel == -2) { if (!sdOk) sdInit(); if (sdOk) scanRoms(); return; }
  if (sel == -5) { enterDir(parentDir(curDir)); return; }
  if (sel < 0) return;
  if (roms[sel].kind == K_UP || roms[sel].kind == K_DIR || roms[sel].kind >= K_VFAV) { enterDir(entryPath(roms[sel])); return; }
  if (roms[sel].kind == K_MORE) {
    lcd.fillScreen(C_BG);
    uiHeader("Folder too big");
    uiMessage("Not enough RAM to list it all", "Split it into sub-folders", C_RED);
    int x, y; waitTap(x, y, 5000);
    return;
  }
  // Free the folder list while playing (it can be tens of KB with a big folder),
  // then read it back from the SD card when the game ends.
  RomEntry r = { entryPath(roms[sel]), roms[sel].size, roms[sel].kind };
  if (r.kind == K_CONT) { r.kind = (uint8_t)romKind(r.path.c_str()); r.size = SIZE_UNKNOWN; }
  if (isVirtualDir(curDir)) r.size = SIZE_UNKNOWN;
  clearList();
  // Recent / Continue / Most played: mark it before the game starts (no extra file open
  // while the game needs its RAM) and add the time played once it ends
  curGamePath = r.path;
  if (sdOk) histUpdate(r.path, HIST_START);
  uint32_t playT0 = millis();
  if (r.kind == K_NES) runNes(r); else runGame(r);
  if (sdOk) histUpdate(r.path, HIST_TIME, (millis() - playT0) / 1000);
  lcd.fillScreen(C_BG);
  uiHeader(APP_NAME " v" APP_VERSION);
  uiMessage("Reading SD card...");
  if (sdOk) listDir();
  browserSelectPath(r.path);
}
