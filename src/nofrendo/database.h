// H@N-CYD-GBC: retro-go's 4,900-game fix-up database (~49 KB of flash) is left out to
// save space. Games run from their iNES headers; "(E)"/"(Europe)" in the file name
// still selects PAL timing. The original file can be dropped back in if wanted.
#pragma once
#include <stdint.h>
typedef struct __attribute__((packed))
{
    uint32_t crc;
    uint32_t system:2;
    uint32_t mapper:8;
    uint32_t submap:3;
    uint32_t mirror:3;
    uint32_t battery:1;
    uint32_t prg_rom:10;
    uint32_t prg_ram:4;
    int16_t  chr_rom:10;
    int16_t  chr_ram:6;
} db_game_t;
static const db_game_t games_database[] = { { 0 } };
