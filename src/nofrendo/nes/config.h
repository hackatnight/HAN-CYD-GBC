#define APP_STRING  "Nofrendo"
#define APP_VERSION "3.0"

/* Uncomment to enable debugging messages */
// #define NOFRENDO_DEBUG

/* Uncomment to enable live dissassembler */
// #define NES6502_DISASM

/* Uncomment to save/load a game's SRAM to disk */
// #define USE_SRAM_FILE

/* Uncomment on big-endian machines */
// #define IS_BIG_ENDIAN

/* H@N-CYD-GBC: put the 6502 core in IRAM in the touch-only build (the Bluetooth
   build has almost no IRAM left, so there it runs from flash) */
#if defined(ESP_PLATFORM) && !__has_include(<Bluepad32.h>)
#include "esp_attr.h"
#define NES_CPU_IRAM IRAM_ATTR
#endif
