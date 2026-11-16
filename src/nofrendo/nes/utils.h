#pragma once

#ifdef RETRO_GO
#include <rg_system.h>
#define LOG_PRINTF(level, x...) rg_system_log(RG_LOG_PRINTF, NULL, x)
#define CRC32(a, b, c) rg_crc32(a, b, c)
#else
/* H@N-CYD-GBC: logs go to the serial port; only errors/warnings (level <= 2) are printed */
#include <stdio.h>
#define LOG_PRINTF(level, x...) do { if ((level) <= 2) printf(x); } while (0)
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#define CRC32(a, b, c) (0)
#endif
/* nes6502_execute goes to IRAM only where there is room (see nes_glue.c) */
#ifndef NES_CPU_IRAM
#define NES_CPU_IRAM
#endif

#define MESSAGE_ERROR(x...) LOG_PRINTF(1, "!! " x)
#define MESSAGE_WARN(x...)  LOG_PRINTF(2, " ! " x)
#define MESSAGE_INFO(x...)  LOG_PRINTF(3, x)
#ifdef NOFRENDO_DEBUG
#define MESSAGE_DEBUG(x, ...) LOG_PRINTF(4, "> %s: " x, __func__, ## __VA_ARGS__)
#else
#define MESSAGE_DEBUG(x...)
#endif
#define MESSAGE_TRACE(x, ...) LOG_PRINTF(4, "~ %s: " x, __func__, ## __VA_ARGS__)

#undef MIN
#define MIN(a, b) ({__typeof__(a) _a = (a); __typeof__(b) _b = (b);_a < _b ? _a : _b; })
#undef MAX
#define MAX(a, b) ({__typeof__(a) _a = (a); __typeof__(b) _b = (b);_a > _b ? _a : _b; })

#define ASSERT(expr) while (!(expr)) { LOG_PRINTF(1, "ASSERTION FAILED IN %s: " #expr "\n", __func__); abort(); }
#define UNUSED(x) (void)x

/* H@N-CYD-GBC: the core's own allocations (save RAM, name tables, ...) come first
   from the Game Boy core's idle video memory, then from the heap (nes_glue.c). */
#ifndef NES_GLUE_IMPL
void *nes_malloc(size_t n);
void *nes_calloc(size_t n, size_t size);
void *nes_realloc(void *p, size_t n);
void nes_free(void *p);
#define malloc(n) nes_malloc(n)
#define calloc(n, s) nes_calloc(n, s)
#define realloc(p, n) nes_realloc(p, n)
#define free(p) nes_free(p)
#endif
