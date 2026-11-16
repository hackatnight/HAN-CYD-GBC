; H@N demo for Game Boy / Game Boy Color. Original code and graphics, no Nintendo assets
; (apart from the header logo bytes every cartridge needs to boot on real hardware).
; Built by build.py, which puts the generated constants first and the data last.
;
; Scenes: 0 intro (typed text), 1 wireframe H@N, 2 two cubes, 3 dot ball, 4 pyramid with an
; orbiting H@N, 5 D-pad cube, 6 tesseract, 7 wobbly text and a sine scroller, 8 input test.
; A = next scene, B = next tune (five tunes, then off). In the shape scenes SELECT recolors the
; shape and START recolors the H@N (Game Boy Color), and the D-pad turns the spin around (or,
; on the D-pad cube, pushes the spin). Every scene has its own starfield. The input test goes
; back to the intro after ~13 s with no buttons.
; In every scene HACKATNIGHT.COM flies across a strip of the background nothing else uses:
; one map row, moved by changing the scroll in each HBlank (so no sprites are used), with a
; shine that sweeps through it (a palette color per line on GBC, the shade on Game Boy).
;
; The 3D scenes play pre-rendered frames. Two copies of the picture's tiles live in VRAM
; (tiles 0.. at $8000 and at $9000); LCDC bit 4 picks which one the screen shows, so the
; next frame is copied into the hidden one and then the two swap at VBlank. VRAM is only
; written while the LCD isn't drawing (STAT mode 0 or 1), two bytes at a time.

; ---------------------------------------------------------------- hardware
P1 = $FF00
IF = $FF0F
NR10 = $FF10
NR11 = $FF11
NR12 = $FF12
NR13 = $FF13
NR14 = $FF14
NR21 = $FF16
NR22 = $FF17
NR23 = $FF18
NR24 = $FF19
NR30 = $FF1A
NR31 = $FF1B
NR32 = $FF1C
NR33 = $FF1D
NR34 = $FF1E
NR41 = $FF20
NR42 = $FF21
NR43 = $FF22
NR44 = $FF23
NR50 = $FF24
NR51 = $FF25
NR52 = $FF26
LCDC = $FF40
STAT = $FF41
SCX = $FF43
LY = $FF44
LYC = $FF45
DMA = $FF46
BGP = $FF47
OBP0 = $FF48
VBK = $FF4F
HDMA1 = $FF51
HDMA2 = $FF52
HDMA3 = $FF53
HDMA4 = $FF54
HDMA5 = $FF55
BCPS = $FF68
BCPD = $FF69
OCPS = $FF6A
OCPD = $FF6B
IE = $FFFF
MBC_BANK = $2000

; ---------------------------------------------------------------- HRAM ($FF80: OAM DMA routine)
hVbl = $FF90
hFrame = $FF91
hCGB = $FF92
hPad = $FF93
hPadNew = $FF94
hFlip = $FF95
hScene = $FF96
hTmp = $FF97
hTmpX = $FF98
hTmpLen = $FF99
hTmpOff = $FF9A
hBandTop = $FF9C        ; first line of the flying HACKATNIGHT.COM strip
hFlyX = $FF9D           ; its scroll position
hWobOn = $FF9E          ; scroller scene: wobble the text rows
hIsr = $FF9F            ; set by every interrupt (the frame copy checks it)

; ---------------------------------------------------------------- WRAM
OAMBUF = $C000
wSceneT = $C100
wCopyLeft = $C102
wCopySrc = $C104
wCopyDst = $C106
wCopyBank = $C108
wAnimBase = $C109
wAnimCount = $C10B
wAnimIdx = $C10C
wAnimBytes = $C10D
wAnimDelay = $C10F
wAnimWait = $C110
wAnimOn = $C111
wAnimState = $C112
wTypePtr = $C113
wTypeDst = $C115
wTypeT = $C117
wMusStep = $C118
wMusT = $C119
wMusOn = $C11A
wIdle = $C11B
wScrollSub = $C11D
wScrollChar = $C11E
wPadShown = $C11F
wAnimDir = $C120        ; 1 forward, $FF backward
wAnimMode = $C121       ; 0 plays in a loop, 1 shows wAnimNext (the D-pad cube)
wAnimNext = $C122
wShapeCol = $C123       ; color choices (SELECT, START)
wHanCol = $C124
wPalDirty = $C125
wYaw = $C126            ; D-pad cube
wPitch = $C127
wDvx = $C128            ; its spin: -1, 0, 1 each way
wDvy = $C129
wSpinT = $C12A
wStarType = $C12B
wStarCount = $C12C
wHintT = $C12D
wHintIdx = $C12E
wTune = $C12F           ; 0 .. TUNE_COUNT-1, TUNE_COUNT = off
wMusAcc = $C130         ; frames to the next step, 8.8
wRec = $C134            ; current tune record
wShapePtr = $C136
wBandScx = $C200        ; per line of the flying strip: scroll, then shine colors and shade
wBandLo = $C210
wBandHi = $C220
wBandBgp = $C230
wStarT = $C240          ; warp starfields: each star's distance step
wLineScx = $C300        ; per screen line: its X scroll (strip and wobble), set in VBlank

LCD_ON = $93            ; on, tiles at $8000, sprites on, background on
SPR_DISK = 122
SPR_DOT = 120
SPR_PLUS = 121
SPR_BLANK = 123

; ---------------------------------------------------------------- vectors and header
org $0040
    jp VBlankISR
org $0048
    jp StatISR
org $0100
    nop
    jp Start
org $0150

Start:
    di
    ld sp,$E000
    cp $11                  ; A = $11 at boot on a Game Boy Color
    ld a,0
    jr nz,.dmg
    inc a
.dmg:
    ldh [hCGB],a
    call LcdOff
    ld hl,$C000
    ld bc,$0200
    xor a
    call Fill
    ld hl,$8000
    ld bc,$2000
    xor a
    call Fill
    ldh a,[hCGB]
    and a
    jr z,.novbk
    ld a,1
    ldh [VBK],a
    ld hl,$9800
    ld bc,$0800
    xor a
    call Fill
    xor a
    ldh [VBK],a
.novbk:
    ld hl,DmaRoutine
    ld de,$FF80
    ld bc,DmaRoutineEnd-DmaRoutine
    call Copy
    ld hl,FontBright
    ld de,$8800
    ld bc,64*16
    call Copy
    ld hl,FontDim
    ld de,$8C00
    ld bc,64*16
    call Copy
    ld hl,SprTiles
    ld de,$8780
    ld bc,4*16
    call Copy
    ld a,$1B                ; color 0 = black ... color 3 = white
    ldh [BGP],a
    ldh [OBP0],a
    xor a
    ld [wTune],a
    call MusicStart
    xor a
    ldh [hScene],a
    call SceneSetup
    xor a
    ldh [IF],a
    ei

MainLoop:
.w:
    ldh a,[hVbl]
    and a
    jr z,.w
    xor a
    ldh [hVbl],a
    call PalUpdate
    call BandPrep
    call ReadPad
    call Music
    call Stars
    call SceneTick
    call CopyRun
    jr MainLoop

; ---------------------------------------------------------------- interrupts
VBlankISR:
    push af
    ld a,1
    ldh [hIsr],a
    call $FF80
    ldh a,[hFlip]
    and a
    jr z,.noflip
    ldh a,[LCDC]
    xor $10
    ldh [LCDC],a
    xor a
    ldh [hFlip],a
    ld [wAnimWait],a
    jr .wdone
.noflip:
    ld a,[wAnimWait]
    inc a
    jr nz,.ws
    dec a
.ws:
    ld [wAnimWait],a
.wdone:
    xor a
    ldh [SCX],a
    ldh a,[hFlyX]
    inc a
    ldh [hFlyX],a
    ldh a,[hFrame]
    inc a
    ldh [hFrame],a
    ld a,1
    ldh [hVbl],a
    pop af
    reti

; STAT interrupt. In the scroller scene it runs in every HBlank (wobble, rainbow). In the
; other scenes it only runs for the flying strip: a line-compare interrupt on the line above
; the strip turns the HBlank interrupt on, and the first line after the strip turns it off
; again, so the rest of each frame's HBlanks are free for copying the next 3D frame.
StatISR:
    push af
    ld a,1
    ldh [hIsr],a
    ldh a,[STAT]
    bit 3,a
    jr nz,.hb
    ld a,%00001000          ; the line above the strip: HBlank interrupts from here
    ldh [STAT],a
    pop af
    reti
.hb:
    push hl
    ldh a,[LY]
    inc a
    ld l,a                  ; the line about to be drawn
    ld h,HIGH(wLineScx)     ; its scroll was worked out in VBlank: set it first, while
    ld a,[hl]               ; there's still time before the line starts drawing
    ldh [SCX],a
    push bc
    ld b,l
    ldh a,[hBandTop]
    ld c,a
    ld a,b
    sub c
    cp BAND_H
    jr nc,.notband
    ld l,a                  ; line within the strip: shine values precomputed by BandPrep
    ld h,HIGH(wBandScx)
    set 5,l
    set 4,l
    ld a,[hl]               ; wBandBgp
    ldh [BGP],a
    ldh a,[hCGB]
    and a
    jr z,.done
    ld a,$8E                ; palette 1, color 3
    ldh [BCPS],a
    res 5,l                 ; wBandLo
    ld a,[hl]
    ldh [BCPD],a
    res 4,l
    set 5,l                 ; wBandHi
    ld a,[hl]
    ldh [BCPD],a
    jr .done
.notband:
    ld a,$1B
    ldh [BGP],a
    ldh a,[hWobOn]
    and a
    jr nz,.wob
    ld a,%01000000          ; past the strip: back to the line-compare interrupt
    ldh [STAT],a
    jr .done
.wob:
    ldh a,[hCGB]
    and a
    jr z,.done
    ld a,$86                ; palette 0, color 3: rainbow
    ldh [BCPS],a
    ldh a,[hFrame]
    add a,b
    ld l,a
    ld h,HIGH(RainLo)
    ld a,[hl]
    ldh [BCPD],a
    inc h
    ld a,[hl]
    ldh [BCPD],a
.done:
    pop bc
    pop hl
    pop af
    reti

DmaRoutine:
    ld a,HIGH(OAMBUF)
    ldh [DMA],a
    ld a,40
.w:
    dec a
    jr nz,.w
    ret
DmaRoutineEnd:

; ---------------------------------------------------------------- helpers
LcdOff:
    ldh a,[LCDC]
    bit 7,a
    ret z
.w:
    ldh a,[LY]
    cp 144
    jr c,.w
    ldh a,[LCDC]
    res 7,a
    ldh [LCDC],a
    ret

LcdOnDefault:
    ld a,LCD_ON
    ldh [LCDC],a
    ldh a,[hBandTop]
    dec a
    ldh [LYC],a
    ldh a,[hWobOn]
    and a
    ld a,%00001000          ; scroller: STAT interrupt at each HBlank
    jr nz,.st
    ld a,%01000000          ; others: at the line above the flying strip
.st:
    ldh [STAT],a
    xor a
    ldh [IF],a
    ld a,%00000011
    ldh [IE],a
    ret

Fill:                       ; hl = dest, bc = count, a = value
    ld e,a
.l:
    ld a,e
    ld [hl+],a
    dec bc
    ld a,b
    or c
    jr nz,.l
    ret

Copy:                       ; hl = source, de = dest, bc = count
.l:
    ld a,[hl+]
    ld [de],a
    inc de
    dec bc
    ld a,b
    or c
    jr nz,.l
    ret

VramPut:                    ; [de] = c, waiting until the LCD allows it
    di
.w:
    ldh a,[STAT]
    and 2
    jr nz,.w
    ld a,c
    ld [de],a
    ei
    ret

PrintEntry:                 ; hl -> dw dest, "text", 0 ; c = tile offset (96 bright, 160 dim)
    ld a,[hl+]
    ld e,a
    ld a,[hl+]
    ld d,a
.l:
    ld a,[hl+]
    and a
    ret z
    add a,c
    ld [de],a
    inc de
    jr .l

RegionMap:                  ; hl = map position, b = width, c = height: tiles 0, 1, 2 ...
    ld e,0
.row:
    push hl
    ld d,b
.col:
    ld a,e
    ld [hl+],a
    inc e
    dec d
    jr nz,.col
    pop hl
    ld a,l
    add a,32
    ld l,a
    jr nc,.nc
    inc h
.nc:
    dec c
    jr nz,.row
    ret

ReadPad:                    ; bit 0 A, 1 B, 2 select, 3 start, 4 right, 5 left, 6 up, 7 down
    ld a,$20
    ldh [P1],a
    ldh a,[P1]
    ldh a,[P1]
    cpl
    and $0F
    swap a
    ld b,a
    ld a,$10
    ldh [P1],a
    ldh a,[P1]
    ldh a,[P1]
    ldh a,[P1]
    ldh a,[P1]
    cpl
    and $0F
    or b
    ld b,a
    ldh a,[hPad]
    xor b
    and b
    ldh [hPadNew],a
    ld a,b
    ldh [hPad],a
    ld a,$30
    ldh [P1],a
    ret

; this frame's scroll, shine color and shade for each line of the flying strip (in VBlank)
BandPrep:
    ld hl,wBandScx
    ld c,0
.l:
    ld a,c                  ; wobble: SineSmall[(line + frame * 2) & 63], a gentle sway
    ld e,a
    ldh a,[hFrame]
    add a,a
    add a,e
    and 63
    ld e,a
    ld d,HIGH(SineSmall)
    ld a,[de]
    ld e,a
    ldh a,[hFlyX]
    add a,e
    ld [hl],a
    ldh a,[hFrame]          ; shine: line * 8 - frame * 3
    ld e,a
    add a,a
    add a,e
    ld e,a
    ld a,c
    add a,a
    add a,a
    add a,a
    sub e
    ld e,a
    ld d,HIGH(ShineLo)
    set 4,l
    ld a,[de]
    ld [hl],a
    inc d
    res 4,l
    set 5,l
    ld a,[de]
    ld [hl],a
    ld a,e
    and $E0
    ld a,$1B                ; Game Boy: white in the shine, light gray elsewhere
    jr z,.w
    ld a,$5B
.w:
    set 4,l
    ld [hl],a
    res 4,l
    res 5,l
    inc l
    inc c
    ld a,c
    cp BAND_H
    jr nz,.l
    ldh a,[hBandTop]        ; the strip's scrolls into the per-line table
    ld e,a
    ld d,HIGH(wLineScx)
    ld hl,wBandScx
    ld b,BAND_H
.cp:
    ld a,[hl+]
    ld [de],a
    inc e
    dec b
    jr nz,.cp
    ldh a,[hWobOn]          ; scroller scene: the wobbling rows
    and a
    ret z
    ld e,WOB_TOP
.wb:
    ldh a,[hFrame]
    add a,a
    add a,e
    and 63
    ld l,a
    ld h,HIGH(SineTab)
    ld a,[hl]
    ld [de],a
    inc e
    ld a,e
    cp WOB_BOT
    jr nz,.wb
    ret

; ---------------------------------------------------------------- music
; Each tune record (32 bytes, bank MUS_BANK): dw frames per step (8.8), dw steps, dw pulse 1,
; pulse 2, wave and noise step data (0 = unused), db env 1, duty 1, env 2, duty 2, wave level,
; kick, hat and snare envelopes. Steps: 0 hold, 1 rest, 2 kick, 3 hat, 4 snare, 12+ MIDI note.
MusicStart:                 ; start wTune from its first step, or go quiet when it's off
    xor a
    ld [wMusStep],a
    ld [wMusStep+1],a
    ld [wMusAcc],a
    ld [wMusAcc+1],a
    ld a,[wTune]
    cp TUNE_COUNT
    jr nc,.off
    ld a,$80
    ldh [NR52],a
    ld a,$77
    ldh [NR50],a
    ld a,$FF
    ldh [NR51],a
    xor a
    ldh [NR30],a
    ld hl,WaveTri
    ld de,$FF30
    ld b,16
.w:
    ld a,[hl+]
    ld [de],a
    inc de
    dec b
    jr nz,.w
    ret
.off:
    xor a
    ldh [NR52],a
    ret

NextTune:                   ; B: the next tune (or off), with its name on the hint row
    ld a,[wTune]
    inc a
    cp TUNE_COUNT+1
    jr c,.s
    xor a
.s:
    ld [wTune],a
    call MusicStart
    ld a,[wTune]
    add a,a
    ld e,a
    ld d,0
    ld hl,TuneNames
    add hl,de
    ld a,[hl+]
    ld h,[hl]
    ld l,a
    ld c,96
    call PrintLive
    xor a
    ld [wHintT],a
    ret

Music:
    ld a,[wTune]
    cp TUNE_COUNT
    ret nc
    ld a,[wMusAcc+1]
    sub 1
    ld [wMusAcc+1],a
    ret nc
    ld a,[wTune]
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld de,TuneTab
    add hl,de
    ld a,l
    ld [wRec],a
    ld a,h
    ld [wRec+1],a
    ld a,[hl+]
    ld b,a
    ld a,[wMusAcc]
    add a,b
    ld [wMusAcc],a
    ld a,[hl+]
    ld b,a
    ld a,[wMusAcc+1]
    adc a,b
    ld [wMusAcc+1],a
    inc hl
    inc hl
    call ChanByte
    push hl
    call PlayP1
    pop hl
    call ChanByte
    push hl
    call PlayP2
    pop hl
    call ChanByte
    push hl
    call PlayWave
    pop hl
    call ChanByte
    call PlayNoise
    ld c,2                  ; next step, back to 0 at the end
    call RecByte
    ld c,a
    inc hl
    ld b,[hl]
    ld a,[wMusStep]
    add a,1
    ld e,a
    ld a,[wMusStep+1]
    adc a,0
    ld d,a
    ld a,e
    cp c
    jr nz,.st
    ld a,d
    cp b
    jr nz,.st
    ld de,0
.st:
    ld a,e
    ld [wMusStep],a
    ld a,d
    ld [wMusStep+1],a
    ret

ChanByte:                   ; hl -> dw channel data: a = this step's code (0 if unused), hl += 2
    ld a,[hl+]
    ld e,a
    ld a,[hl+]
    ld d,a
    or e
    ret z
    ld a,[wMusStep]
    add a,e
    ld e,a
    ld a,[wMusStep+1]
    adc a,d
    ld d,a
    ld a,[de]
    ret

RecByte:                    ; a = byte c of the tune record (hl points at it)
    ld a,[wRec]
    add a,c
    ld l,a
    ld a,[wRec+1]
    adc a,0
    ld h,a
    ld a,[hl]
    ret

PlayP1:
    and a
    ret z
    cp 1
    jr nz,.n
    xor a
    ldh [NR12],a
    ld a,$80
    ldh [NR14],a
    ret
.n:
    sub 24
    ld e,a
    ld d,0
    xor a
    ldh [NR10],a
    ld c,13
    call RecByte
    ldh [NR11],a
    ld c,12
    call RecByte
    ldh [NR12],a
    ld hl,NoteLo
    add hl,de
    ld a,[hl]
    ldh [NR13],a
    ld hl,NoteHi
    add hl,de
    ld a,[hl]
    or $80
    ldh [NR14],a
    ret

PlayP2:
    and a
    ret z
    cp 1
    jr nz,.n
    xor a
    ldh [NR22],a
    ld a,$80
    ldh [NR24],a
    ret
.n:
    sub 24
    ld e,a
    ld d,0
    ld c,15
    call RecByte
    ldh [NR21],a
    ld c,14
    call RecByte
    ldh [NR22],a
    ld hl,NoteLo
    add hl,de
    ld a,[hl]
    ldh [NR23],a
    ld hl,NoteHi
    add hl,de
    ld a,[hl]
    or $80
    ldh [NR24],a
    ret

PlayWave:                   ; the triangle bass on the wave channel
    and a
    ret z
    cp 1
    jr nz,.n
    xor a
    ldh [NR30],a
    ret
.n:
    sub 24
    ld e,a
    ld d,0
    xor a
    ldh [NR30],a
    ld a,$80
    ldh [NR30],a
    ld c,16
    call RecByte
    ldh [NR32],a
    ld hl,WaveLo
    add hl,de
    ld a,[hl]
    ldh [NR33],a
    ld hl,WaveHi
    add hl,de
    ld a,[hl]
    or $80
    ldh [NR34],a
    ret

PlayNoise:                  ; drums: 2 kick, 3 hat, 4 snare
    cp 2
    ret c
    cp 5
    ret nc
    sub 2
    ld e,a
    ld d,0
    add a,17
    ld c,a
    call RecByte
    ldh [NR42],a
    ld hl,DrumPoly
    add hl,de
    ld a,[hl]
    ldh [NR43],a
    xor a
    ldh [NR41],a
    ld a,$80
    ldh [NR44],a
    ret

; ---------------------------------------------------------------- stars (sprites 0-15)
; One kind of starfield per scene (StarTypeTab): 0 drift left, 1 drift right, 2 fall,
; 3 rise, 4 diagonal, 5 twinkle, 6 warp out, 7 warp in, 8 drift right in the top rows
; (the scroller's letters use the lower half), 9 follow the D-pad cube's spin.
StarsInit:                  ; LCD off
    ldh a,[hScene]
    ld e,a
    ld d,0
    ld hl,StarTypeTab
    add hl,de
    ld a,[hl]
    ld [wStarType],a
    ld hl,StarCountTab
    add hl,de
    ld a,[hl]
    ld [wStarCount],a
    ld hl,OAMBUF
    ld de,StarData          ; per star: y, speed, tile
    ld bc,StarX0
.l:
    ld a,[wStarType]
    cp 8
    ld a,[de]
    jr nz,.y
    and 63
    add a,20
.y:
    ld [hl+],a
    ld a,[bc]
    inc bc
    ld [hl+],a
    inc de
    inc de
    ld a,[de]
    inc de
    ld [hl+],a
    xor a
    ld [hl+],a
    ld a,l
    cp 64
    jr nz,.l
    ld hl,wStarT
    ld a,5
.t:
    ld [hl+],a
    add a,11
    and 31
    ld b,a
    ld a,l
    cp LOW(wStarT)+16
    ld a,b
    jr nz,.t
    ld a,[wStarCount]       ; hide the stars this scene doesn't use
    add a,a
    add a,a
    ld l,a
    ld h,HIGH(OAMBUF)
.h:
    ld a,l
    cp 64
    ret nc
    ld [hl],0
    add a,4
    ld l,a
    jr .h

Stars:
    ld a,[wStarCount]
    and a
    ret z
    ld c,a
    ld hl,OAMBUF
    ld de,StarData+1
    xor a
    ldh [hTmp],a
.l:
    ld a,[de]
    ld b,a                  ; speed 1-3
    inc de
    inc de
    inc de
    push de
    push bc
    ld a,[wStarType]
    add a,a
    push hl
    ld e,a
    ld d,0
    ld hl,StarJump
    add hl,de
    ld e,[hl]
    inc hl
    ld d,[hl]
    pop hl
    call .go
    pop bc
    pop de
    ld a,l
    add a,4
    ld l,a
    ldh a,[hTmp]
    inc a
    ldh [hTmp],a
    dec c
    jr nz,.l
    ret
.go:
    push de
    ret

StLeft:
    inc hl
    ld a,[hl]
    sub b
    ld [hl],a
    dec hl
    ret
StRight:
    inc hl
    ld a,[hl]
    add a,b
    ld [hl],a
    dec hl
    ret
StFall:
    ld a,[hl]
    add a,b
    cp 168
    jr c,.s
    sub 160
.s:
    ld [hl],a
    ret
StRise:
    ld a,[hl]
    sub b
    cp 8
    jr nc,.s
    add a,160
.s:
    ld [hl],a
    ret
StDiag:
    call StFall
    jp StLeft
StTwinkle:
    ldh a,[hTmp]
    ld c,a
    add a,a
    add a,a
    add a,c
    ld c,a
    ldh a,[hFrame]
    srl a
    srl a
    srl a
    add a,c
    and 15
    ld e,a
    ld d,0
    push hl
    ld hl,TwinkTab
    add hl,de
    ld a,[hl]
    pop hl
    inc hl
    inc hl
    ld [hl],a
    dec hl
    dec hl
    ret
StWarpIn:
    xor a
    sub b
    ld b,a
StWarpOut:
    ldh a,[hTmp]
    add a,LOW(wStarT)
    ld e,a
    ld d,HIGH(wStarT)
    ld a,[de]
    add a,b
    and 31
    ld [de],a
    ld c,a                  ; distance step
    ldh a,[hTmp]            ; offset = star * 32 + step
    ld d,a
    srl d
    srl d
    srl d
    and 7
    swap a
    add a,a
    or c
    ld e,a
    push hl
    ld hl,WarpY
    add hl,de
    ld a,[hl]
    ldh [hTmpX],a
    ld hl,WarpX
    add hl,de
    ld b,[hl]
    pop hl
    ldh a,[hTmpX]
    ld [hl+],a
    ld a,b
    ld [hl+],a
    ld a,c
    cp 18
    ld a,SPR_DOT
    jr c,.d
    inc a
.d:
    ld [hl-],a
    dec hl
    ret
StDpad:                     ; the stars stream the other way to the cube's spin
    ld a,[wDvx]
    ld d,a
    ld a,[wDvy]
    or d
    jp z,StLeft
    ld a,[wDvx]
    and a
    jr z,.y
    inc hl
    ld a,[hl]
    bit 7,d
    jr nz,.xr
    sub b
    jr .xs
.xr:
    add a,b
.xs:
    ld [hl],a
    dec hl
.y:
    ld a,[wDvy]
    and a
    ret z
    bit 7,a
    jp nz,StFall
    jp StRise

; ---------------------------------------------------------------- frame streaming
CopyRun:                    ; copies until done or the next VBlank
    ld a,[wCopyLeft]
    ld c,a
    ld a,[wCopyLeft+1]
    ld b,a
    or c
    ret z
    ld a,[wCopyBank]
    ld [MBC_BANK],a
    ld a,[wCopySrc]
    ld l,a
    ld a,[wCopySrc+1]
    ld h,a
    ld a,[wCopyDst]
    ld e,a
    ld a,[wCopyDst+1]
    ld d,a
.loop:
    ldh a,[hVbl]
    and a
    jr nz,.save
    ldh a,[STAT]
    and 3
    cp 1
    jr z,.two               ; VBlank: two bytes at a time
    xor a
    ldh [hIsr],a
.w3:                        ; wait for the start of an HBlank
    ldh a,[STAT]
    and 3
    cp 3
    jr nz,.w3
.w0:
    ldh a,[STAT]
    and 3
    jr nz,.w0
    di
    ldh a,[hIsr]            ; an interrupt used up part of this HBlank: take the next one
    and a
    jr nz,.next
    ld a,b
    and a
    jr nz,.six
    ld a,c
    cp 6
    jr c,.two2
.six:                       ; six bytes fit in an HBlank plus the OAM scan after it
    ld a,[hl+]
    ld [de],a
    inc de
    ld a,[hl+]
    ld [de],a
    inc de
    ld a,[hl+]
    ld [de],a
    inc de
    ld a,[hl+]
    ld [de],a
    inc de
    ld a,[hl+]
    ld [de],a
    inc de
    ld a,[hl+]
    ld [de],a
    inc de
    ei
    ld a,c
    sub 6
    ld c,a
    jr nc,.chk
    dec b
    jr .chk
.next:
    ei
    jr .loop
.two:
    di
.w:
    ldh a,[STAT]
    and 2
    jr nz,.w
.two2:
    ld a,[hl+]
    ld [de],a
    inc de
    ld a,[hl+]
    ld [de],a
    inc de
    ei
    dec bc
    dec bc
.chk:
    ld a,b
    or c
    jr nz,.loop
.save:
    ld a,l
    ld [wCopySrc],a
    ld a,h
    ld [wCopySrc+1],a
    ld a,e
    ld [wCopyDst],a
    ld a,d
    ld [wCopyDst+1],a
    ld a,c
    ld [wCopyLeft],a
    ld a,b
    ld [wCopyLeft+1],a
    ret

AnimInit:                   ; hl = frame table, a = frames, de = bytes per frame, b = VBlanks per frame
    ld [wAnimCount],a
    ld a,b
    ld [wAnimDelay],a
    ld a,e
    ld [wAnimBytes],a
    ld a,d
    ld [wAnimBytes+1],a
    ld a,l
    ld [wAnimBase],a
    ld a,h
    ld [wAnimBase+1],a
    xor a
    ld [wAnimIdx],a
    ld [wAnimState],a
    ld [wAnimWait],a
    ld a,[hl+]
    ld [MBC_BANK],a
    ld a,[hl+]
    ld e,a
    ld a,[hl]
    ld h,a
    ld l,e
    push hl
    ld de,$8000
    ld a,[wAnimBytes]
    ld c,a
    ld a,[wAnimBytes+1]
    ld b,a
    call Copy
    pop hl
    ld de,$9000
    ld a,[wAnimBytes]
    ld c,a
    ld a,[wAnimBytes+1]
    ld b,a
    call Copy
    ld a,1
    ld [wAnimOn],a
    ret

AnimTick:                   ; states: 0 start a copy, 1 copying, 2 ready, 3 swap requested
    ld a,[wAnimOn]
    and a
    ret z
    ld a,[wAnimState]
    and a
    jr z,.start
    cp 1
    jr z,.copying
    cp 2
    jr nz,.s3
.wait:
    ld a,[wAnimWait]
    ld hl,wAnimDelay
    cp [hl]
    ret c
    ld a,1
    ldh [hFlip],a
    ld a,3
    ld [wAnimState],a
    ret
.s3:
    ldh a,[hFlip]
    and a
    ret nz
    xor a
    ld [wAnimState],a
    jr .start               ; swapped: start on the next frame right away
.copying:
    ldh a,[hCGB]
    and a
    jr z,.cpu
    ldh a,[HDMA5]           ; Game Boy Color: $FF once the HBlank transfer is done
    cp $FF
    ret nz
    jr .ready
.cpu:
    ld a,[wCopyLeft]
    ld b,a
    ld a,[wCopyLeft+1]
    or b
    ret nz
.ready:
    ld a,2
    ld [wAnimState],a
    jr .wait
.start:
    ld a,[wAnimMode]
    and a
    jr z,.auto
    ld a,[wAnimNext]
    ld hl,wAnimIdx
    cp [hl]
    ret z
    jr .ok
.auto:
    ld a,[wAnimDir]
    ld b,a
    ld a,[wAnimIdx]
    add a,b
    ld hl,wAnimCount
    bit 7,b
    jr z,.fw
    cp $FF
    jr nz,.ok
    ld a,[hl]
    dec a
    jr .ok
.fw:
    cp [hl]
    jr c,.ok
    xor a
.ok:
    ld [wAnimIdx],a
    ld e,a
    ld d,0
    ld h,d
    ld l,e
    add hl,hl
    add hl,de
    ld a,[wAnimBase]
    ld e,a
    ld a,[wAnimBase+1]
    ld d,a
    add hl,de
    ld a,[hl+]
    ld [wCopyBank],a
    ld a,[hl+]
    ld [wCopySrc],a
    ld a,[hl]
    ld [wCopySrc+1],a
    ldh a,[LCDC]
    bit 4,a
    ld a,$80
    jr z,.b
    ld a,$90
.b:
    ld [wCopyDst+1],a
    xor a
    ld [wCopyDst],a
    ldh a,[hCGB]
    and a
    jr z,.dmg
    ; Game Boy Color: the hardware copies 16 bytes in every HBlank by itself (HBlank DMA),
    ; so a whole frame arrives within one screen refresh
    ld a,[wCopyBank]
    ld [MBC_BANK],a
    ld a,[wCopySrc+1]
    ldh [HDMA1],a
    ld a,[wCopySrc]
    ldh [HDMA2],a
    ld a,[wCopyDst+1]
    ldh [HDMA3],a
    xor a
    ldh [HDMA4],a
    ld a,[wAnimBytes]       ; bytes / 16 - 1, with bit 7 set for HBlank mode
    ld l,a
    ld a,[wAnimBytes+1]
    ld h,a
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld a,h
    dec a
    or $80
    ldh [HDMA5],a
    jr .go
.dmg:
    ld a,[wAnimBytes]
    ld [wCopyLeft],a
    ld a,[wAnimBytes+1]
    ld [wCopyLeft+1],a
.go:
    ld a,1
    ld [wAnimState],a
    ret

; ---------------------------------------------------------------- scenes
SceneTick:
    ldh a,[hScene]
    cp SC_INPUT
    jp z,InputTick
    ldh a,[hPadNew]
    bit 0,a
    jp nz,NextScene
    bit 1,a
    call nz,NextTune
    call HintTick
    ldh a,[hScene]
    cp SC_INTRO
    call z,IntroTick
    ldh a,[hScene]
    cp SC_SCROLL
    call z,ScrollTick
    ld a,[wAnimOn]
    and a
    call nz,ShapeTick
    call AnimTick
    ldh a,[hPad]            ; while someone is playing with the buttons, stay in the scene
    and a
    jr z,.cnt
    xor a
    ld [wSceneT],a
    ld [wSceneT+1],a
.cnt:
    ld hl,wSceneT
    inc [hl]
    jr nz,.t
    inc hl
    inc [hl]
.t:
    ldh a,[hScene]
    ld e,a
    ld d,0
    ld hl,SceneLen
    add hl,de
    ld a,[wSceneT+1]
    cp [hl]
    ret c
NextScene:
    ldh a,[hScene]
    inc a
    cp SC_COUNT
    jr c,GoScene
    xor a
GoScene:
    ldh [hScene],a
SceneSetup:
    ldh a,[hCGB]            ; let a running HBlank transfer finish before the LCD goes off
    and a
    jr z,.nh
    ld a,[wAnimOn]
    and a
    jr z,.nh
    ld a,[wAnimState]
    cp 1
    jr nz,.nh
.wh:
    ldh a,[HDMA5]
    cp $FF
    jr nz,.wh
.nh:
    call LcdOff
    xor a
    ld [wSceneT],a
    ld [wSceneT+1],a
    ld [wAnimOn],a
    ld [wAnimMode],a
    ld [wCopyLeft],a
    ld [wCopyLeft+1],a
    ld [wHintT],a
    ld [wHintIdx],a
    ldh [hFlip],a
    ldh [STAT],a
    ldh [hWobOn],a
    ld a,1
    ldh [IE],a
    ld hl,$9800
    ld bc,$0400
    ld a,96+32              ; blank (space)
    call Fill
    ld hl,OAMBUF+$40        ; sprites 16-39 off
    ld bc,$A0-$40
    xor a
    call Fill
    ld hl,wLineScx
    ld bc,256
    xor a
    call Fill
    call LoadCgbPalettes
    call SetupBand
    call StarsInit
    call ShowHint
    ldh a,[hScene]
    cp SC_INTRO
    jp z,SetupIntro
    cp SC_SCROLL
    jp z,SetupScroll
    cp SC_INPUT
    jp z,SetupInput
    jp SetupShape

PrintLive:                  ; like PrintEntry, but safe while the LCD is on
    ld a,[hl+]
    ld e,a
    ld a,[hl+]
    ld d,a
.l:
    ld a,[hl+]
    and a
    ret z
    add a,c
    push bc
    ld c,a
    call VramPut
    pop bc
    inc de
    jr .l

HintTick:                   ; the hint row cycles through the scene's hints every 160 frames
    ld a,[wHintT]
    inc a
    ld [wHintT],a
    cp 160
    ret c
    xor a
    ld [wHintT],a
    ld a,[wHintIdx]
    inc a
    cp 3
    jr c,.s
    xor a
.s:
    ld [wHintIdx],a
ShowHint:
    ldh a,[hScene]
    ld e,a
    add a,a
    add a,e
    add a,a
    ld e,a
    ld d,0
    ldh a,[hCGB]
    and a
    ld hl,HintTab
    jr nz,.c
    ld hl,HintTabDmg
.c:
    add hl,de
    ld a,[wHintIdx]
    add a,a
    ld e,a
    add hl,de
    ld a,[hl+]
    ld h,[hl]
    ld l,a
    ld c,160
    jp PrintLive

PalUpdate:                  ; GBC palette 2 (the shapes): scene background, shape pair, H@N
    ld a,[wPalDirty]
    and a
    ret z
    xor a
    ld [wPalDirty],a
    ldh a,[hCGB]
    and a
    ret z
    ld a,$90
    ldh [BCPS],a
    ldh a,[hScene]
    swap a
    ld e,a
    ld d,0
    ld hl,CgbBgPal
    add hl,de
    ld a,[hl+]
    ldh [BCPD],a
    ld a,[hl]
    ldh [BCPD],a
    ld a,[wShapeCol]
    add a,a
    add a,a
    ld e,a
    ld hl,ShapeColTab
    add hl,de
    ld b,4
.s:
    ld a,[hl+]
    ldh [BCPD],a
    dec b
    jr nz,.s
    ld a,[wHanCol]
    add a,a
    ld e,a
    ld hl,HanColTab
    add hl,de
    ld a,[hl+]
    ldh [BCPD],a
    ld a,[hl]
    ldh [BCPD],a
    ret

SetupBand:                  ; HACKATNIGHT.COM on this scene's strip row (LCD off)
    ldh a,[hScene]
    ld e,a
    ld d,0
    ld hl,BandTopTab
    add hl,de
    ld a,[hl]
    ldh [hBandTop],a
    ld hl,BandMapTab
    add hl,de
    add hl,de
    ld a,[hl+]
    ld h,[hl]
    ld l,a
    push hl
    ld de,BandText
.t:
    ld a,[de]
    inc de
    and a
    jr z,.td
    add a,96
    ld [hl+],a
    jr .t
.td:
    pop hl
    ldh a,[hCGB]
    and a
    ret z
    ld a,1                  ; GBC: the strip row uses palette 1, everything else palette 0
    ldh [VBK],a
    push hl
    ld hl,$9800
    ld bc,$0400
    xor a
    call Fill
    pop hl
    ld b,32
.a:
    ld [hl],1
    inc hl
    dec b
    jr nz,.a
    xor a
    ldh [VBK],a
    ret

LoadCgbPalettes:            ; per-scene palettes 0 and 1 (Game Boy Color only)
    ldh a,[hCGB]
    and a
    ret z
    ldh a,[hScene]
    swap a
    ld e,a
    ld d,0
    ld hl,CgbBgPal
    add hl,de
    ld a,$80
    ldh [BCPS],a
    ld b,16
.b:
    ld a,[hl+]
    ldh [BCPD],a
    dec b
    jr nz,.b
    ld a,$80
    ldh [OCPS],a
    ld hl,CgbObjPal
    ld b,8
.o:
    ld a,[hl+]
    ldh [OCPD],a
    dec b
    jr nz,.o
    ret

; ---- 1-6: the shapes (ShapeTab, 16 bytes each: dw title, caption, map position; db width,
; height; dw frame table; db frames; dw bytes per frame; db VBlanks per frame, mode)
SetupShape:
    ldh a,[hScene]
    dec a
    swap a
    ld e,a
    ld d,0
    ld hl,ShapeTab
    add hl,de
    ld a,[hl+]
    ld e,a
    ld a,[hl+]
    ld d,a
    push hl
    ld h,d
    ld l,e
    ld c,96
    call PrintEntry
    pop hl
    ld a,[hl+]
    ld e,a
    ld a,[hl+]
    ld d,a
    push hl
    ld h,d
    ld l,e
    ld c,160
    call PrintEntry
    pop hl
    ld a,[hl+]
    ld e,a
    ld a,[hl+]
    ld d,a
    ld a,[hl+]
    ld b,a
    ld a,[hl+]
    ld c,a
    push hl
    push de
    push bc
    ld h,d
    ld l,e
    call RegionMap
    pop bc
    pop hl
    ldh a,[hCGB]
    and a
    call nz,RegionAttr
    pop hl
    ld a,[hl+]
    ld e,a
    ld a,[hl+]
    ld d,a
    ld a,[hl+]
    ldh [hTmp],a
    ld a,[hl+]
    ld c,a
    ld a,[hl+]
    ld b,a
    ld a,[hl+]
    ldh [hTmpLen],a
    ld a,[hl]
    ld [wAnimMode],a
    ld [wDvx],a             ; the D-pad cube starts out spinning
    ld h,d
    ld l,e
    ld d,b
    ld e,c
    ldh a,[hTmpLen]
    ld b,a
    ldh a,[hTmp]
    call AnimInit
    ld a,1
    ld [wAnimDir],a
    xor a
    ld [wAnimNext],a
    ld [wYaw],a
    ld [wPitch],a
    ld [wDvy],a
    ld [wSpinT],a
    ldh a,[hScene]
    add a,a
    ld e,a
    ld d,0
    ld hl,ColorDefaults
    add hl,de
    ld a,[hl+]
    ld [wShapeCol],a
    ld a,[hl]
    ld [wHanCol],a
    ld a,1
    ld [wPalDirty],a
    call PalUpdate
    jp LcdOnDefault

RegionAttr:                 ; GBC: palette 2 for the picture (hl = map position, b x c tiles)
    ld a,1
    ldh [VBK],a
.row:
    push hl
    ld d,b
.col:
    ld [hl],2
    inc hl
    dec d
    jr nz,.col
    pop hl
    ld de,32
    add hl,de
    dec c
    jr nz,.row
    xor a
    ldh [VBK],a
    ret

ShapeTick:
    ldh a,[hPadNew]
    bit 2,a                 ; SELECT: next shape color
    jr z,.ns
    ld a,[wShapeCol]
    inc a
    and 7
    ld [wShapeCol],a
    ld a,1
    ld [wPalDirty],a
.ns:
    ldh a,[hPadNew]
    bit 3,a                 ; START: next H@N color
    jr z,.nt
    ld a,[wHanCol]
    inc a
    and 7
    ld [wHanCol],a
    ld a,1
    ld [wPalDirty],a
.nt:
    ld a,[wAnimMode]
    and a
    jr nz,DcubeTick
    ldh a,[hPadNew]
    bit 4,a                 ; right: spin forward
    jr z,.nr
    ld a,1
    ld [wAnimDir],a
.nr:
    ldh a,[hPadNew]
    bit 5,a                 ; left: spin backward
    ret z
    ld a,$FF
    ld [wAnimDir],a
    ret

DcubeTick:                  ; each D-pad press pushes the spin that way (or slows it down)
    ldh a,[hPadNew]
    ld b,a
    ld hl,wDvx
    bit 4,b
    jr z,.a
    ld a,[hl]
    cp 1
    jr z,.a
    inc [hl]
.a:
    bit 5,b
    jr z,.b
    ld a,[hl]
    cp $FF
    jr z,.b
    dec [hl]
.b:
    ld hl,wDvy
    bit 7,b
    jr z,.c
    ld a,[hl]
    cp 1
    jr z,.c
    inc [hl]
.c:
    bit 6,b
    jr z,.d
    ld a,[hl]
    cp $FF
    jr z,.d
    dec [hl]
.d:
    ld a,[wSpinT]
    inc a
    and 3
    ld [wSpinT],a
    ret nz
    ld a,[wDvx]
    ld b,a
    ld a,[wYaw]
    add a,b
    and YAW_N-1
    ld [wYaw],a
    ld a,[wDvy]
    ld b,a
    ld a,[wPitch]
    add a,b
    and 2*PITCH_N-1
    ld [wPitch],a
    ld a,[wYaw]             ; a half turn of pitch is the same cube with the yaw mirrored
    ld c,a
    ld a,[wPitch]
    cp PITCH_N
    jr c,.lo
    sub PITCH_N
    ld b,a
    xor a
    sub c
    and YAW_N-1
    ld c,a
    ld a,b
.lo:
    add a,a
    add a,a
    add a,a
    add a,c
    ld [wAnimNext],a
    ret

; ---- 0: intro
SetupIntro:
    ldh a,[hCGB]
    and a
    ld hl,IntroStreamDmg
    jr z,.m
    ld hl,IntroStreamCgb
.m:
    ld a,l
    ld [wTypePtr],a
    ld a,h
    ld [wTypePtr+1],a
    xor a
    ld [wTypeT],a
    jp LcdOnDefault

IntroTick:                  ; types one character every 4 frames
    ld a,[wTypeT]
    inc a
    ld [wTypeT],a
    and 3
    ret nz
    ld a,[wTypePtr]
    ld l,a
    ld a,[wTypePtr+1]
    ld h,a
.next:
    ld a,[hl+]
    and a
    ret z
    cp 1
    jr nz,.char
    ld a,[hl+]
    ld [wTypeDst],a
    ld a,[hl+]
    ld [wTypeDst+1],a
    jr .next
.char:
    add a,96
    ld c,a
    ld a,[wTypeDst]
    ld e,a
    ld a,[wTypeDst+1]
    ld d,a
    call VramPut
    inc de
    ld a,e
    ld [wTypeDst],a
    ld a,d
    ld [wTypeDst+1],a
    ld a,l
    ld [wTypePtr],a
    ld a,h
    ld [wTypePtr+1],a
    ret

; ---- 7: wobbly text and sine scroller (sprites 16-36)
SetupScroll:
    ld hl,TxtWob1
    ld c,96
    call PrintEntry
    ld hl,TxtWob2
    ld c,96
    call PrintEntry
    ld hl,TxtWob3
    ld c,96
    call PrintEntry
    ld hl,TxtWob4
    ld c,96
    call PrintEntry
    xor a
    ld [wScrollSub],a
    ld [wScrollChar],a
    ld a,1
    ldh [hWobOn],a
    jp LcdOnDefault

ScrollTick:
    ld a,[wScrollSub]
    inc a
    and 7
    ld [wScrollSub],a
    jr nz,.n
    ld a,[wScrollChar]
    inc a
    cp MSG_LEN
    jr c,.c
    xor a
.c:
    ld [wScrollChar],a
.n:
    ld hl,OAMBUF+$40
    ld a,[wScrollChar]
    ld e,a
    ld b,0
    ld c,21
.l:
    ld a,[wScrollSub]
    ld d,a
    ld a,b
    sub d
    add a,8
    ldh [hTmpX],a
    push hl
    ldh a,[hTmpX]           ; one wave across 256 px, so only one crest is on screen
    srl a
    srl a
    ld d,a
    ldh a,[hFrame]
    add a,d
    and 63
    ld l,a
    ld h,HIGH(SineY)
    ld a,[hl]
    pop hl
    ld [hl+],a
    ldh a,[hTmpX]
    ld [hl+],a
    push hl
    ld hl,ScrollMsg
    ld d,0
    add hl,de
    ld a,[hl]
    add a,96
    pop hl
    ld [hl+],a
    xor a
    ld [hl+],a
    inc e
    ld a,e
    cp MSG_LEN
    jr c,.k
    ld e,0
.k:
    ld a,b
    add a,8
    ld b,a
    dec c
    jr nz,.l
    ret

; ---- 8: input test
SetupInput:
    xor a
    ld [wPadShown],a
    ld [wIdle],a
    ld [wIdle+1],a
    ld hl,TxtInputTitle
    ld c,96
    call PrintEntry
    ld hl,PadNames
    ld b,8
.n:
    push bc
    ld a,[hl+]
    ld e,a
    ld a,[hl+]
    ld d,a
    ld a,[hl+]
    ld b,a
.c:
    ld a,[hl+]
    add a,160
    ld [de],a
    inc de
    dec b
    jr nz,.c
    pop bc
    dec b
    jr nz,.n
    ld hl,PadTiles
    ld de,$8000
    ld bc,PAD_BYTES
    call Copy
    ld hl,PAD_MAP
    ld b,PAD_W
    ld c,PAD_H
    call RegionMap
    jp LcdOnDefault

InputTick:
    ldh a,[hPad]
    and a
    jr z,.idle
    xor a
    ld [wIdle],a
    ld [wIdle+1],a
    jr .draw
.idle:
    ld hl,wIdle
    inc [hl]
    jr nz,.ck
    inc hl
    inc [hl]
.ck:
    ld a,[wIdle+1]
    cp 3
    jp nc,NextScene
.draw:
    ldh a,[hPad]
    ld b,a
    ld hl,OAMBUF+$40
    ld de,PadSpots
    ld c,8
.s:
    ld a,[de]
    inc de
    srl b
    jr c,.on
    xor a
.on:
    ld [hl+],a
    ld a,[de]
    inc de
    ld [hl+],a
    ld a,SPR_DISK
    ld [hl+],a
    xor a
    ld [hl+],a
    dec c
    jr nz,.s
    ldh a,[hPad]
    ld hl,wPadShown
    cp [hl]
    ret z
    ld [hl],a
    ld b,a
    ld hl,PadNames
    ld a,8
    ldh [hTmp],a
.nm:
    ld a,[hl+]
    ld e,a
    ld a,[hl+]
    ld d,a
    ld a,[hl+]
    ldh [hTmpLen],a
    srl b
    ld a,96
    jr c,.br
    ld a,160
.br:
    ldh [hTmpOff],a
.ch:
    ld a,[hl+]
    push hl
    ld l,a
    ldh a,[hTmpOff]
    add a,l
    ld c,a
    call VramPut
    pop hl
    inc de
    ldh a,[hTmpLen]
    dec a
    ldh [hTmpLen],a
    jr nz,.ch
    ldh a,[hTmp]
    dec a
    ldh [hTmp],a
    jr nz,.nm
    ret
