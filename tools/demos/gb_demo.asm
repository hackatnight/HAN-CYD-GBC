; H@N demo for Game Boy / Game Boy Color. Original code and graphics, no Nintendo assets
; (apart from the header logo bytes every cartridge needs to boot on real hardware).
; Built by build.py, which puts the generated constants first and the data last.
;
; Scenes: 0 intro (typed text, stars), 1 spinning wireframe H@N, 2 spinning cubes,
; 3 wobbly text and a sine scroller, 4 input test. A = next scene, B = music on/off,
; START = input test. The input test goes back to the intro after ~13 s with no buttons.
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
NR50 = $FF24
NR51 = $FF25
NR52 = $FF26
LCDC = $FF40
STAT = $FF41
SCX = $FF43
LY = $FF44
DMA = $FF46
BGP = $FF47
OBP0 = $FF48
VBK = $FF4F
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
hStarsOn = $FF9B

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

LCD_ON = $93            ; on, tiles at $8000, sprites on, background on
SPR_DISK = 122

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
    ld bc,3*16
    call Copy
    ld a,$1B                ; color 0 = black ... color 3 = white
    ldh [BGP],a
    ldh [OBP0],a
    ld a,$80
    ldh [NR52],a
    ld a,$77
    ldh [NR50],a
    ld a,$FF
    ldh [NR51],a
    ld a,1
    ld [wMusOn],a
    ld [wMusT],a
    ldh [hStarsOn],a
    ld hl,OAMBUF+1          ; stars' starting x
    ld de,StarX0
    ld c,16
.sx:
    ld a,[de]
    inc de
    ld [hl],a
    ld a,l
    add a,4
    ld l,a
    dec c
    jr nz,.sx
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
    call ReadPad
    call Music
    call Stars
    call SceneTick
    call CopyRun
    jr MainLoop

; ---------------------------------------------------------------- interrupts
VBlankISR:
    push af
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
    ldh a,[hFrame]
    inc a
    ldh [hFrame],a
    ld a,1
    ldh [hVbl],a
    pop af
    reti

; HBlank (scroller scene only): wobble the text rows and, on GBC, a rainbow for color 3
StatISR:
    push af
    push bc
    push hl
    ldh a,[LY]
    inc a
    ld b,a
    ldh a,[hCGB]
    and a
    jr z,.nopal
    ld a,$86                ; palette 0, color 3, auto-increment
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
.nopal:
    ld a,b
    cp WOB_TOP
    jr c,.flat
    cp WOB_BOT
    jr nc,.flat
    ldh a,[hFrame]
    add a,a
    add a,b
    and 63
    ld l,a
    ld h,HIGH(SineTab)
    ld a,[hl]
    ldh [SCX],a
    jr .done
.flat:
    xor a
    ldh [SCX],a
.done:
    pop hl
    pop bc
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

; ---------------------------------------------------------------- music (two square channels)
Music:
    ld a,[wMusOn]
    and a
    ret z
    ld a,[wMusT]
    dec a
    ld [wMusT],a
    ret nz
    ld a,MUS_SPEED
    ld [wMusT],a
    ld a,[wMusStep]
    ld e,a
    ld d,0
    ld hl,MusLead
    add hl,de
    ld a,[hl]
    and a
    jr z,.nolead
    ld c,a
    ld b,0
    ld a,$80
    ldh [NR21],a
    ld a,$A2
    ldh [NR22],a
    ld hl,NoteLo
    add hl,bc
    ld a,[hl]
    ldh [NR23],a
    ld hl,NoteHi
    add hl,bc
    ld a,[hl]
    or $80
    ldh [NR24],a
.nolead:
    ld hl,MusBass
    add hl,de
    ld a,[hl]
    and a
    jr z,.nobass
    ld c,a
    ld b,0
    xor a
    ldh [NR10],a
    ld a,$40
    ldh [NR11],a
    ld a,$C5
    ldh [NR12],a
    ld hl,NoteLo
    add hl,bc
    ld a,[hl]
    ldh [NR13],a
    ld hl,NoteHi
    add hl,bc
    ld a,[hl]
    or $80
    ldh [NR14],a
.nobass:
    ld a,[wMusStep]
    inc a
    cp MUS_LEN
    jr c,.s
    xor a
.s:
    ld [wMusStep],a
    ret

ToggleMusic:
    ld a,[wMusOn]
    xor 1
    ld [wMusOn],a
    jr z,.off
    ld a,$80
    ldh [NR52],a
    ld a,$77
    ldh [NR50],a
    ld a,$FF
    ldh [NR51],a
    ret
.off:
    xor a
    ldh [NR52],a
    ret

; ---------------------------------------------------------------- stars (sprites 0-15)
Stars:
    ldh a,[hStarsOn]
    and a
    jr nz,.on
    ld hl,OAMBUF
    ld b,16
.h:
    ld [hl],0
    ld a,l
    add a,4
    ld l,a
    dec b
    jr nz,.h
    ret
.on:
    ld hl,OAMBUF
    ld de,StarData          ; per star: y, speed, tile
    ld c,16
.l:
    ld a,[de]
    inc de
    ld [hl+],a
    ld a,[de]
    inc de
    ld b,a
    ld a,[hl]
    sub b
    ld [hl+],a
    ld a,[de]
    inc de
    ld [hl+],a
    xor a
    ld [hl+],a
    dec c
    jr nz,.l
    ret

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
    di
.w:
    ldh a,[STAT]
    and 2
    jr nz,.w
    ld a,[hl+]
    ld [de],a
    inc de
    ld a,[hl+]
    ld [de],a
    inc de
    ei
    dec bc
    dec bc
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
    ret
.copying:
    ld a,[wCopyLeft]
    ld b,a
    ld a,[wCopyLeft+1]
    or b
    ret nz
    ld a,2
    ld [wAnimState],a
    ret
.start:
    ld a,[wAnimIdx]
    inc a
    ld hl,wAnimCount
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
    ld a,[wAnimBytes]
    ld [wCopyLeft],a
    ld a,[wAnimBytes+1]
    ld [wCopyLeft+1],a
    ld a,1
    ld [wAnimState],a
    ret

; ---------------------------------------------------------------- scenes
SceneTick:
    ldh a,[hScene]
    cp SC_INPUT
    jp z,InputTick
    ldh a,[hPadNew]
    bit 3,a
    jr z,.nostart
    ld a,SC_INPUT
    jp GoScene
.nostart:
    bit 0,a
    jp nz,NextScene
    bit 1,a
    call nz,ToggleMusic
    ldh a,[hScene]
    cp SC_INTRO
    call z,IntroTick
    ldh a,[hScene]
    cp SC_SCROLL
    call z,ScrollTick
    call AnimTick
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
    call LcdOff
    xor a
    ld [wSceneT],a
    ld [wSceneT+1],a
    ld [wAnimOn],a
    ld [wCopyLeft],a
    ld [wCopyLeft+1],a
    ldh [hFlip],a
    ldh [STAT],a
    ld a,1
    ldh [IE],a
    ldh [hStarsOn],a
    ld hl,$9800
    ld bc,$0400
    ld a,96+32              ; blank (space)
    call Fill
    ld hl,OAMBUF+$40        ; sprites 16-39 off
    ld bc,$A0-$40
    xor a
    call Fill
    call LoadCgbPalettes
    ldh a,[hScene]
    cp SC_INTRO
    jp z,SetupIntro
    cp SC_WIRE
    jp z,SetupWire
    cp SC_CUBES
    jp z,SetupCubes
    cp SC_SCROLL
    jp z,SetupScroll
    jp SetupInput

LoadCgbPalettes:            ; per-scene palettes (Game Boy Color only)
    ldh a,[hCGB]
    and a
    ret z
    ldh a,[hScene]
    add a,a
    add a,a
    add a,a
    ld e,a
    ld d,0
    ld hl,CgbBgPal
    add hl,de
    ld a,$80
    ldh [BCPS],a
    ld b,8
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

; ---- 0: intro
SetupIntro:
    ld hl,IntroStream
    ld a,l
    ld [wTypePtr],a
    ld a,h
    ld [wTypePtr+1],a
    xor a
    ld [wTypeT],a
    ldh a,[hCGB]
    and a
    ld hl,TxtModeDmg
    jr z,.m
    ld hl,TxtModeCgb
.m:
    ld c,160
    call PrintEntry
    ld hl,TxtHint
    ld c,160
    call PrintEntry
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

; ---- 1: wireframe logo
SetupWire:
    ld hl,TxtWireTitle
    ld c,96
    call PrintEntry
    ld hl,TxtWireCap
    ld c,160
    call PrintEntry
    ld hl,TxtHint
    ld c,160
    call PrintEntry
    ld hl,WIRE_MAP
    ld b,WIRE_W
    ld c,WIRE_H
    call RegionMap
    ld hl,WireFrames
    ld a,WIRE_COUNT
    ld de,WIRE_BYTES
    ld b,WIRE_DELAY
    call AnimInit
    jp LcdOnDefault

; ---- 2: cubes
SetupCubes:
    ld hl,TxtCubeTitle
    ld c,96
    call PrintEntry
    ld hl,TxtCubeCap
    ld c,160
    call PrintEntry
    ld hl,TxtHint
    ld c,160
    call PrintEntry
    ld hl,CUBE_MAP
    ld b,CUBE_W
    ld c,CUBE_H
    call RegionMap
    ld hl,CubeFrames
    ld a,CUBE_COUNT
    ld de,CUBE_BYTES
    ld b,CUBE_DELAY
    call AnimInit
    jp LcdOnDefault

; ---- 3: wobbly text and sine scroller (sprites 16-36)
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
    ld hl,TxtHint
    ld c,160
    call PrintEntry
    xor a
    ld [wScrollSub],a
    ld [wScrollChar],a
    ldh [hStarsOn],a        ; no stars: at most 10 sprites fit on a line
    call LcdOnDefault
    ld a,%00001000          ; STAT interrupt at each HBlank
    ldh [STAT],a
    xor a
    ldh [IF],a
    ld a,%00000011
    ldh [IE],a
    ret

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

; ---- 4: input test
SetupInput:
    xor a
    ldh [hStarsOn],a
    ld [wPadShown],a
    ld [wIdle],a
    ld [wIdle+1],a
    ld hl,TxtInputTitle
    ld c,96
    call PrintEntry
    ld hl,TxtInputHint
    ld c,160
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
