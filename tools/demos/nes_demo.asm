; H@N demo for the NES. Original code and graphics. Mapper 4 (MMC3), 32 KB PRG, 256 KB CHR.
; Built by build.py, which puts the generated constants first and the data last.
;
; Same scenes as the Game Boy demo: 0 intro, 1 wireframe H@N, 2 cubes, 3 dot ball, 4 pyramid,
; 5 D-pad cube, 6 tesseract, 7 wobbly text and a sine scroller, 8 input test. A = next scene,
; B = next tune; in the shape scenes SELECT / START recolor the shape / the H@N and the D-pad
; turns the spin around (or pushes the D-pad cube's spin). Every scene has its own starfield.
;
; Each 3D frame is its own pair of 1 KB CHR banks (tiles 64-191), so changing frames is two
; mapper writes in the NMI. The wobble uses the MMC3 scanline IRQ to change the X scroll
; every two lines. All PPU writes happen in the NMI (from a queue) or with rendering off.

PPUCTRL = $2000
PPUMASK = $2001
PPUSTATUS = $2002
OAMADDR = $2003
PPUSCROLL = $2005
PPUADDR = $2006
PPUDATA = $2007
OAMDMA = $4014
JOY1 = $4016
MMC_SEL = $8000
MMC_DATA = $8001
MMC_MIRROR = $A000
MMC_RAM = $A001
MMC_LATCH = $C000
MMC_RELOAD = $C001
MMC_IRQOFF = $E000
MMC_IRQON = $E001

; zero page
nmiFlag = $00
frame = $01
pad = $02
padNew = $03
scene = $04
tmp = $05
tmp2 = $06
tmp3 = $07
tmpX = $08
ptr = $09
qLen = $0B
ppuCtrl = $0C
ppuMask = $0D
animOn = $0E
animIdx = $0F
animCount = $10
animBase = $11
animDelay = $12
animWait = $13
animPending = $14
sceneT = $15
musStep = $17
musT = $18
musOn = $19
idle = $1A
typePtr = $1C
typeDst = $1E
typeT = $20
scrollSub = $21
scrollChar = $22
starsOn = $23
padShown = $24
wobOn = $25
irqLine = $26
palColor = $27
palDirty = $28
nlen = $29
noff = $2A
ncount = $2B
flyX = $2C                      ; 9-bit scroll of the flying strip
bandK = $2E                     ; first IRQ slot of the strip
bandShine = $2F
animDir = $30
animMode = $31
animNext = $32
shapeCol = $33
hanCol = $34
yaw = $35
pitch = $36
dvx = $37
dvy = $38
spinT = $39
starType = $3A
starCount = $3B
hintT = $3C
hintIdx = $3D
tune = $3E
musAcc = $3F                    ; 8.8 frames to the next step
musStep2 = $41                  ; 16-bit step
rec = $43                       ; current tune record
p1vol = $45
p2vol = $46
nzvol = $47
p1dec = $48
p2dec = $49
p1cnt = $4A
p2cnt = $4B
chptr = $4C
p1duty = $4E
p2duty = $4F
pal2a = $50                     ; palette 2: the shape (dim, bright) and the H@N
pal2b = $51
pal2c = $52
starIdx = $53
spd = $54
wtmp = $55
qDst = $56
jptr = $58
STAR_T = $0500                  ; warp starfields: each star's distance step
XTab = $0400                    ; per two-line slot from line 8: X scroll and PPUCTRL
CtrlTab = $0480
SLOTS = 100
OAM = $0200
QUEUE = $0300                   ; hi, lo, tile triplets

DISK_TILE = 66

org $C000

Reset:
    sei
    cld
    ldx #$40
    stx $4017
    ldx #$FF
    txs
    inx
    stx PPUCTRL
    stx PPUMASK
    stx $4010
    stx MMC_IRQOFF
    bit PPUSTATUS
.v1:
    bit PPUSTATUS
    bpl .v1
    lda #0
    tax
.clr:
    sta $00,x
    sta $0300,x
    sta $0400,x
    sta $0500,x
    sta $0600,x
    sta $0700,x
    lda #$FF
    sta OAM,x
    lda #0
    inx
    bne .clr
.v2:
    bit PPUSTATUS
    bpl .v2
    ; MMC3: CHR A12 inverted (1 KB banks at $0000 for the background, 2 KB at $1000 for sprites)
    lda #$86
    sta MMC_SEL
    lda #0
    sta MMC_DATA
    lda #$87
    sta MMC_SEL
    lda #1
    sta MMC_DATA
    lda #$80
    sta MMC_SEL
    lda #SPR_BANK
    sta MMC_DATA
    lda #$81
    sta MMC_SEL
    lda #SPR_BANK+2
    sta MMC_DATA
    lda #$82
    sta MMC_SEL
    lda #0
    sta MMC_DATA
    lda #$83
    sta MMC_SEL
    lda #PAD_BANK
    sta MMC_DATA
    lda #$84
    sta MMC_SEL
    lda #PAD_BANK+1
    sta MMC_DATA
    lda #$85
    sta MMC_SEL
    lda #1
    sta MMC_DATA
    lda #0
    sta MMC_MIRROR              ; vertical mirroring: two screens side by side
    lda #$80
    sta MMC_RAM
    lda #0
    sta tune
    jsr MusicStart
    ldx #0
    ldy #0
.sx:
    lda StarX0,y
    sta OAM+3,x
    inx
    inx
    inx
    inx
    iny
    cpy #16
    bne .sx
    lda #$FF
    sta animPending
    lda #0
    sta scene
    jsr SceneSetup
    cli

Main:
.w:
    lda nmiFlag
    beq .w
    lda #0
    sta nmiFlag
    jsr ReadPad
    jsr Music
    jsr Stars
    jsr SceneTick
    jsr BandTick
    jmp Main

; ---------------------------------------------------------------- interrupts
NMI:
    pha
    txa
    pha
    tya
    pha
    lda #0
    sta OAMADDR
    lda #HIGH(OAM)
    sta OAMDMA
    bit PPUSTATUS
    ldx #0
.q:
    cpx qLen
    beq .qd
    lda QUEUE,x
    sta PPUADDR
    lda QUEUE+1,x
    sta PPUADDR
    lda QUEUE+2,x
    sta PPUDATA
    inx
    inx
    inx
    jmp .q
.qd:
    lda #0
    sta qLen
    lda palDirty
    beq .np
    lda #$3F
    sta PPUADDR
    lda #$03
    sta PPUADDR
    lda palColor
    sta PPUDATA
    lda #0
    sta palDirty
.np:
    lda #$3F                    ; palette 1 for the flying strip: dark blue shadow, shining face
    sta PPUADDR
    lda #$05
    sta PPUADDR
    lda #$01
    sta PPUDATA
    lda #$11
    sta PPUDATA
    lda bandShine
    sta PPUDATA
    lda #$0F
    sta PPUDATA
    lda pal2a
    sta PPUDATA
    lda pal2b
    sta PPUDATA
    lda pal2c
    sta PPUDATA
    lda animPending
    cmp #$FF
    beq .na
    ldx #$83
    stx MMC_SEL
    sta MMC_DATA
    clc
    adc #1
    ldx #$84
    stx MMC_SEL
    sta MMC_DATA
    lda #$FF
    sta animPending
    lda #0
    sta animWait
    jmp .nb
.na:
    inc animWait
    bne .nb
    dec animWait
.nb:
    lda #0
    sta PPUSCROLL
    sta PPUSCROLL
    lda ppuCtrl
    sta PPUCTRL
    lda ppuMask
    sta PPUMASK
    sta MMC_IRQOFF
    lda #7
    sta MMC_LATCH
    sta MMC_RELOAD
    sta MMC_IRQON
    lda #0
    sta irqLine
    inc frame
    lda #1
    sta nmiFlag
    pla
    tay
    pla
    tax
    pla
    rti

IRQ:                            ; MMC3 scanline IRQ every two lines: X scroll and nametable from the slot tables
    pha
    txa
    pha
    sta MMC_IRQOFF
    ldx irqLine
    inx
    stx irqLine
    cpx #SLOTS
    bcs .last
    lda #1
    sta MMC_LATCH
    sta MMC_IRQON
.last:
    dex
    bit PPUSTATUS
    lda XTab,x
    sta PPUSCROLL
    lda #0
    sta PPUSCROLL
    lda CtrlTab,x
    sta PPUCTRL
    pla
    tax
    pla
    rti

; ---------------------------------------------------------------- flying HACKATNIGHT.COM strip
SetupBand:                      ; rendering off: clear the slots, print the strip, give its rows palette 1
    ldx #0
.c:
    lda #0
    sta XTab,x
    lda #$88
    sta CtrlTab,x
    inx
    cpx #SLOTS
    bne .c
    lda scene
    tax
    lda BandK,x
    sta bandK
    txa
    asl a
    tax
    bit PPUSTATUS
    lda BandNT+1,x
    sta PPUADDR
    lda BandNT,x
    sta PPUADDR
    ldy #0
.t:
    lda BandText,y
    beq .td
    clc
    adc #$E0
    sta PPUDATA
    iny
    bne .t
.td:
    lda #$23                    ; attributes: palette 1 on the strip, 2 on the picture
    sta PPUADDR
    lda #$C0
    sta PPUADDR
    lda scene
    asl a
    asl a
    asl a
    asl a
    asl a
    asl a
    tay
    lda scene
    lsr a
    lsr a
    clc
    adc #HIGH(AttrTab)
    sta ptr+1
    lda #LOW(AttrTab)
    sta ptr
    ldx #64
.a:
    lda (ptr),y
    sta PPUDATA
    iny
    dex
    bne .a
    rts

BandTick:                       ; fill this frame's slots: strip sway and fly, text wobble
    inc flyX
    bne .h
    inc flyX+1
.h:
    lda frame
    lsr a
    lsr a
    and #7
    tax
    lda ShineNes,x
    sta bandShine
    ldy #0
    ldx bandK
.l:
    stx tmpX
    tya
    clc
    adc frame
    and #63
    tax
    lda SwayTab,x
    sta tmp2
    ldx tmpX
    clc
    adc flyX
    sta XTab,x
    lda #0
    bit tmp2
    bpl .p
    lda #$FF
.p:
    adc flyX+1
    and #1
    ora #$88
    sta CtrlTab,x
    inx
    iny
    cpy #5
    bne .l
    lda wobOn
    beq .r
    ldx #0
.w:
    txa
    clc
    adc frame
    and #63
    tay
    lda SineTab,y
    sta XTab+WOB_SLOT,x
    inx
    cpx #WOB_COUNT
    bne .w
.r:
    rts

; ---------------------------------------------------------------- helpers
ReadPad:                        ; bit 0 A, 1 B, 2 select, 3 start, 4 up, 5 down, 6 left, 7 right
    lda #1
    sta JOY1
    lda #0
    sta JOY1
    ldx #8
.l:
    lda JOY1
    lsr a
    ror tmp
    dex
    bne .l
    lda pad
    eor #$FF
    and tmp
    sta padNew
    lda tmp
    sta pad
    rts

QueueAtDst:                     ; queue tile A for address typeDst
    ldx qLen
    cpx #144
    bcs .f
    pha
    lda typeDst+1
    sta QUEUE,x
    lda typeDst
    sta QUEUE+1,x
    pla
    sta QUEUE+2,x
    txa
    clc
    adc #3
    sta qLen
.f:
    rts

PrintEntry:                     ; ptr -> dw address, "text", 0 ; tmp = tile offset (rendering off)
    ldy #1
    lda (ptr),y
    sta PPUADDR
    dey
    lda (ptr),y
    sta PPUADDR
    ldy #2
.l:
    lda (ptr),y
    beq .d
    clc
    adc tmp
    sta PPUDATA
    iny
    bne .l
.d:
    rts

Print:                          ; X = index into the text table, A = tile offset
    sta tmp
    lda TextTable,x
    sta ptr
    lda TextTable+1,x
    sta ptr+1
    jmp PrintEntry

RegionMap:                      ; ptr = nametable address, tmp = width, tmp2 = height: tiles 64, 65 ...
    lda #64
    sta tmp3
.row:
    lda ptr+1
    sta PPUADDR
    lda ptr
    sta PPUADDR
    ldx tmp
.col:
    lda tmp3
    sta PPUDATA
    inc tmp3
    dex
    bne .col
    lda ptr
    clc
    adc #32
    sta ptr
    bcc .nc
    inc ptr+1
.nc:
    dec tmp2
    bne .row
    rts

SetChr:                         ; A = first of two 1 KB banks for tiles 64-191 (rendering off)
    ldx #$83
    stx MMC_SEL
    sta MMC_DATA
    clc
    adc #1
    ldx #$84
    stx MMC_SEL
    sta MMC_DATA
    rts

; ---------------------------------------------------------------- music
; Tune records (32 bytes): dw frames per step (8.8), dw steps, dw pulse 1, pulse 2, triangle,
; noise data (0 = unused), db volume, duty, decay for each pulse, 4 spare, kick, hat, snare
; volumes. Steps: 0 hold, 1 rest, 2 kick, 3 hat, 4 snare, 12+ MIDI note. The pulses and the
; noise have their volume stepped down in software each frame.
MusicStart:
    lda #0
    sta musStep2
    sta musStep2+1
    sta musAcc
    sta musAcc+1
    sta p1vol
    sta p2vol
    sta nzvol
    lda tune
    cmp #TUNE_COUNT
    bcs .off
    asl a
    asl a
    asl a
    asl a
    asl a
    clc
    adc #LOW(TuneTab)
    sta rec
    lda #HIGH(TuneTab)
    adc #0
    sta rec+1
    ldy #13
    lda (rec),y
    sta p1duty
    ldy #16
    lda (rec),y
    sta p2duty
    lda #$0F
    sta $4015
    lda #$80
    sta $4008
    rts
.off:
    lda #0
    sta $4015
    rts

NextTune:
    inc tune
    lda tune
    cmp #TUNE_COUNT+1
    bcc .s
    lda #0
    sta tune
.s:
    jsr MusicStart
    lda tune
    asl a
    tax
    lda TuneNames,x
    sta ptr
    lda TuneNames+1,x
    sta ptr+1
    lda #0
    sta hintT
    lda #$E0
    jmp QueuePrint

Music:
    jsr MusEnv
    lda tune
    cmp #TUNE_COUNT
    bcs .r
    lda musAcc+1
    sec
    sbc #1
    sta musAcc+1
    bcs .r
    ldy #0
    lda (rec),y
    clc
    adc musAcc
    sta musAcc
    iny
    lda (rec),y
    adc musAcc+1
    sta musAcc+1
    ldy #4
    jsr ChanCode
    jsr PlayP1
    ldy #6
    jsr ChanCode
    jsr PlayP2
    ldy #8
    jsr ChanCode
    jsr PlayTri
    ldy #10
    jsr ChanCode
    jsr PlayNoise
    inc musStep2
    bne .c
    inc musStep2+1
.c:
    ldy #2
    lda (rec),y
    cmp musStep2
    bne .r
    iny
    lda (rec),y
    cmp musStep2+1
    bne .r
    lda #0
    sta musStep2
    sta musStep2+1
.r:
    rts

ChanCode:                       ; y = record offset of a channel: A = this step's code (0 if unused)
    lda (rec),y
    sta chptr
    iny
    lda (rec),y
    sta chptr+1
    ora chptr
    beq .z
    lda chptr
    clc
    adc musStep2
    sta chptr
    lda chptr+1
    adc musStep2+1
    sta chptr+1
    ldy #0
    lda (chptr),y
.z:
    rts

MusEnv:                         ; every frame: volumes down, written to the APU
    lda p1dec
    beq .h1
    dec p1cnt
    bne .h1
    sta p1cnt
    lda p1vol
    beq .h1
    dec p1vol
.h1:
    lda p2dec
    beq .h2
    dec p2cnt
    bne .h2
    sta p2cnt
    lda p2vol
    beq .h2
    dec p2vol
.h2:
    lda p1duty
    ora #$30
    ora p1vol
    sta $4000
    lda p2duty
    ora #$30
    ora p2vol
    sta $4004
    lda nzvol
    beq .n
    dec nzvol
.n:
    ora #$30
    sta $400C
    rts

PlayP1:
    cmp #0
    beq .r
    cmp #1
    bne .n
    lda #0
    sta p1vol
    rts
.n:
    sec
    sbc #24
    tax
    lda #$08
    sta $4001
    lda PulseLo,x
    sta $4002
    lda PulseHi,x
    ora #$08
    sta $4003
    ldy #12
    lda (rec),y
    sta p1vol
    ldy #14
    lda (rec),y
    sta p1dec
    sta p1cnt
.r:
    rts

PlayP2:
    cmp #0
    beq .r
    cmp #1
    bne .n
    lda #0
    sta p2vol
    rts
.n:
    sec
    sbc #24
    tax
    lda #$08
    sta $4005
    lda PulseLo,x
    sta $4006
    lda PulseHi,x
    ora #$08
    sta $4007
    ldy #15
    lda (rec),y
    sta p2vol
    ldy #17
    lda (rec),y
    sta p2dec
    sta p2cnt
.r:
    rts

PlayTri:
    cmp #0
    beq .r
    cmp #1
    bne .n
    lda #$80
    sta $4008
    lda #0
    sta $400B
    rts
.n:
    sec
    sbc #24
    tax
    lda #$FF
    sta $4008
    lda TriLo,x
    sta $400A
    lda TriHi,x
    ora #$08
    sta $400B
.r:
    rts

PlayNoise:
    cmp #2
    bcc .r
    cmp #5
    bcs .r
    sec
    sbc #2
    tax
    lda DrumPer,x
    sta $400E
    txa
    clc
    adc #22
    tay
    lda (rec),y
    sta nzvol
    lda #$08
    sta $400F
.r:
    rts

; ---------------------------------------------------------------- stars (sprites 0-15)
; One kind per scene (StarTypeTab): 0 left, 1 right, 2 fall, 3 rise, 4 diagonal, 5 twinkle,
; 6 warp out, 7 warp in, 8 right in the top rows (the scroller's letters use the lower half),
; 9 follow the D-pad cube's spin.
StarsInit:                      ; rendering off
    ldx scene
    lda StarTypeTab,x
    sta starType
    lda StarCountTab,x
    sta starCount
    ldx #0
    ldy #0
    lda #0
    sta starIdx
.l:
    lda StarData,y
    pha
    lda starType
    cmp #8
    bne .y
    pla
    and #63
    clc
    adc #20
    pha
.y:
    pla
    sta OAM,x
    lda StarData+2,y
    sta OAM+1,x
    lda #0
    sta OAM+2,x
    sty tmp3
    ldy starIdx
    lda StarX0,y
    sta OAM+3,x
    tya
    asl a
    asl a
    asl a
    clc
    adc starIdx
    asl a
    clc
    adc starIdx
    adc #5
    and #31
    sta STAR_T,y
    lda starIdx
    cmp starCount
    bcc .v
    lda #$FF
    sta OAM,x
.v:
    ldy tmp3
    iny
    iny
    iny
    inx
    inx
    inx
    inx
    inc starIdx
    lda starIdx
    cmp #16
    bne .l
    rts

Stars:
    lda starCount
    beq .r
    ldx #0
    ldy #0
    sty starIdx
.l:
    lda StarData+1,y
    sta spd
    tya
    pha
    lda starType
    asl a
    tay
    lda StarJump,y
    sta jptr
    lda StarJump+1,y
    sta jptr+1
    jsr .go
    pla
    clc
    adc #3
    tay
    inx
    inx
    inx
    inx
    inc starIdx
    lda starIdx
    cmp starCount
    bne .l
.r:
    rts
.go:
    jmp (jptr)

StLeft:
    lda OAM+3,x
    sec
    sbc spd
    sta OAM+3,x
    rts
StRight:
    lda OAM+3,x
    clc
    adc spd
    sta OAM+3,x
    rts
StFall:
    lda OAM,x
    clc
    adc spd
    cmp #232
    bcc .s
    sbc #224
.s:
    sta OAM,x
    rts
StRise:
    lda OAM,x
    sec
    sbc spd
    cmp #8
    bcs .s
    adc #224
.s:
    sta OAM,x
    rts
StDiag:
    jsr StFall
    jmp StLeft
StTwinkle:
    lda starIdx
    asl a
    asl a
    adc starIdx
    sta wtmp
    lda frame
    lsr a
    lsr a
    lsr a
    clc
    adc wtmp
    and #15
    tay
    lda TwinkTab,y
    sta OAM+1,x
    rts
StWarpIn:
    lda #0
    sec
    sbc spd
    sta spd
StWarpOut:
    ldy starIdx
    lda STAR_T,y
    clc
    adc spd
    and #31
    sta STAR_T,y
    sta wtmp
    lda starIdx
    and #7
    asl a
    asl a
    asl a
    asl a
    asl a
    ora wtmp
    tay
    lda starIdx
    cmp #8
    bcs .hi
    lda WarpY0,y
    sta OAM,x
    lda WarpX0,y
    sta OAM+3,x
    jmp .t
.hi:
    lda WarpY1,y
    sta OAM,x
    lda WarpX1,y
    sta OAM+3,x
.t:
    lda wtmp
    cmp #18
    lda #64
    bcc .d
    lda #65
.d:
    sta OAM+1,x
    rts
StDpad:                         ; the stars stream the other way to the cube's spin
    lda dvx
    ora dvy
    bne .m
    jmp StLeft
.m:
    lda dvx
    beq .y
    bmi .xr
    lda OAM+3,x
    sec
    sbc spd
    sta OAM+3,x
    jmp .y
.xr:
    lda OAM+3,x
    clc
    adc spd
    sta OAM+3,x
.y:
    lda dvy
    beq .r
    bmi .f
    jmp StRise
.f:
    jmp StFall
.r:
    rts

QueuePrint:                     ; ptr -> dw address, "text", 0 ; A = tile offset; drawn by the NMI
    sta tmp
    ldy #0
    lda (ptr),y
    sta qDst
    iny
    lda (ptr),y
    sta qDst+1
    ldy #2
.l:
    lda (ptr),y
    beq .d
    clc
    adc tmp
    ldx qLen
    cpx #144
    bcs .d
    sta QUEUE+2,x
    lda qDst+1
    sta QUEUE,x
    lda qDst
    sta QUEUE+1,x
    txa
    clc
    adc #3
    sta qLen
    inc qDst
    bne .n
    inc qDst+1
.n:
    iny
    bne .l
.d:
    rts

HintTick:                       ; the hint row cycles through the scene's hints every 160 frames
    inc hintT
    lda hintT
    cmp #160
    bcc .r
    lda #0
    sta hintT
    inc hintIdx
    lda hintIdx
    cmp #3
    bcc .s
    lda #0
    sta hintIdx
.s:
    jsr HintPtr
    lda #$A0
    jmp QueuePrint
.r:
    rts

HintPtr:
    lda scene
    asl a
    clc
    adc scene
    adc hintIdx
    asl a
    tax
    lda HintTab,x
    sta ptr
    lda HintTab+1,x
    sta ptr+1
    rts

ShapePal:
    lda shapeCol
    asl a
    tax
    lda ShapeColNes,x
    sta pal2a
    lda ShapeColNes+1,x
    sta pal2b
    ldx hanCol
    lda HanColNes,x
    sta pal2c
    rts

; ---------------------------------------------------------------- scenes
SceneTick:
    lda scene
    cmp #SC_INPUT
    bne .n0
    jmp InputTick
.n0:
    lda padNew
    and #$01
    beq .na
    jmp NextScene
.na:
    lda padNew
    and #$02
    beq .nb
    jsr NextTune
.nb:
    jsr HintTick
    lda scene
    cmp #SC_INTRO
    bne .ni
    jsr IntroTick
.ni:
    lda scene
    cmp #SC_SCROLL
    bne .nsc
    jsr ScrollTick
.nsc:
    lda animOn
    beq .nsh
    jsr ShapeTick
.nsh:
    jsr AnimTick
    lda pad                     ; while someone is playing with the buttons, stay in the scene
    beq .cnt
    lda #0
    sta sceneT
    sta sceneT+1
.cnt:
    inc sceneT
    bne .t
    inc sceneT+1
.t:
    ldx scene
    lda sceneT+1
    cmp SceneLen,x
    bcc .r
    jmp NextScene
.r:
    rts

NextScene:
    lda scene
    clc
    adc #1
    cmp #SC_COUNT
    bcc GoScene
    lda #0
GoScene:
    sta scene
SceneSetup:
    lda #0
    sta PPUCTRL
    sta PPUMASK
    sta wobOn
    sta MMC_IRQOFF
    sta animOn
    sta animMode
    sta qLen
    sta sceneT
    sta sceneT+1
    sta palDirty
    sta hintT
    sta hintIdx
    lda #$FF
    sta animPending
    bit PPUSTATUS
    lda #$20
    sta PPUADDR
    lda #$00
    sta PPUADDR
    ldx #8
    ldy #0
    lda #0
.cl:
    sta PPUDATA
    iny
    bne .cl
    dex
    bne .cl
    lda #$FF
    ldx #$40
.hs:
    sta OAM,x
    inx
    bne .hs
    jsr LoadPalettes
    jsr SetupBand
    jsr StarsInit
    jsr HintPtr
    lda #$A0
    sta tmp
    jsr PrintEntry
    lda scene
    asl a
    tax
    lda SetupTable+1,x
    pha
    lda SetupTable,x
    pha
    rts

; ---- 1-6: the shapes (ShapeTab, 16 bytes: dw title, caption, nametable position; db width,
; height, first CHR bank, frames, VBlanks per frame, mode)
SetupShape:
    lda scene
    sec
    sbc #1
    asl a
    asl a
    asl a
    asl a
    tax
    stx tmpX
    lda ShapeTab,x
    sta ptr
    lda ShapeTab+1,x
    sta ptr+1
    lda #$E0
    sta tmp
    jsr PrintEntry
    ldx tmpX
    lda ShapeTab+2,x
    sta ptr
    lda ShapeTab+3,x
    sta ptr+1
    lda #$A0
    sta tmp
    jsr PrintEntry
    ldx tmpX
    lda ShapeTab+4,x
    sta ptr
    lda ShapeTab+5,x
    sta ptr+1
    lda ShapeTab+6,x
    sta tmp
    lda ShapeTab+7,x
    sta tmp2
    jsr RegionMap
    ldx tmpX
    lda ShapeTab+8,x
    sta animBase
    lda ShapeTab+9,x
    sta animCount
    lda ShapeTab+10,x
    sta animDelay
    lda ShapeTab+11,x
    sta animMode
    sta dvx                     ; the D-pad cube starts out spinning
    jsr AnimStart
    lda #1
    sta animDir
    lda #0
    sta animNext
    sta yaw
    sta pitch
    sta dvy
    sta spinT
    lda scene
    asl a
    tax
    lda ColorDefaults,x
    sta shapeCol
    lda ColorDefaults+1,x
    sta hanCol
    jsr ShapePal
    jmp SetupDone

ShapeTick:
    lda padNew
    and #$04                    ; SELECT: next shape color
    beq .ns
    lda shapeCol
    clc
    adc #1
    and #7
    sta shapeCol
    jsr ShapePal
.ns:
    lda padNew
    and #$08                    ; START: next H@N color
    beq .nt
    lda hanCol
    clc
    adc #1
    and #7
    sta hanCol
    jsr ShapePal
.nt:
    lda animMode
    bne DcubeTick
    lda padNew
    and #$80                    ; right: spin forward
    beq .nr
    lda #1
    sta animDir
.nr:
    lda padNew
    and #$40                    ; left: spin backward
    beq .r
    lda #$FF
    sta animDir
.r:
    rts

DcubeTick:                      ; each D-pad press pushes the spin that way (or slows it down)
    lda padNew
    and #$80
    beq .a
    lda dvx
    cmp #1
    beq .a
    inc dvx
.a:
    lda padNew
    and #$40
    beq .b
    lda dvx
    cmp #$FF
    beq .b
    dec dvx
.b:
    lda padNew
    and #$20
    beq .c
    lda dvy
    cmp #1
    beq .c
    inc dvy
.c:
    lda padNew
    and #$10
    beq .d
    lda dvy
    cmp #$FF
    beq .d
    dec dvy
.d:
    inc spinT
    lda spinT
    and #3
    bne .r
    lda yaw
    clc
    adc dvx
    and #YAW_N-1
    sta yaw
    lda pitch
    clc
    adc dvy
    and #2*PITCH_N-1
    sta pitch
    lda yaw                     ; a half turn of pitch is the same cube with the yaw mirrored
    sta wtmp
    lda pitch
    cmp #PITCH_N
    bcc .lo
    sbc #PITCH_N
    pha
    lda #0
    sec
    sbc yaw
    and #YAW_N-1
    sta wtmp
    pla
.lo:
    asl a
    asl a
    clc
    adc wtmp
    sta animNext
.r:
    rts

SetupDone:
    lda #$88                    ; NMI on, sprites at $1000, background at $0000
    sta ppuCtrl
    lda #$1E
    sta ppuMask
    bit PPUSTATUS
.w:
    bit PPUSTATUS
    bpl .w
    lda #0
    sta PPUSCROLL
    sta PPUSCROLL
    lda ppuCtrl
    sta PPUCTRL
    rts

LoadPalettes:
    bit PPUSTATUS
    lda #$3F
    sta PPUADDR
    lda #0
    sta PPUADDR
    ldy #4
.p4:
    lda scene
    asl a
    asl a
    tax
    lda ScenePal,x
    sta PPUDATA
    lda ScenePal+1,x
    sta PPUDATA
    lda ScenePal+2,x
    sta PPUDATA
    lda ScenePal+3,x
    sta PPUDATA
    dey
    bne .p4
    ldx #0
.s:
    lda SprPal,x
    sta PPUDATA
    inx
    cpx #16
    bne .s
    lda #$3F
    sta PPUADDR
    lda #0
    sta PPUADDR
    sta PPUADDR
    sta PPUADDR
    rts

AnimStart:                      ; animBase, animCount, animDelay set
    lda #0
    sta animIdx
    sta animWait
    lda animBase
    jsr SetChr
    lda #1
    sta animOn
    rts

AnimTick:
    lda animOn
    beq .r
    lda animPending
    cmp #$FF
    bne .r
    lda animWait
    cmp animDelay
    bcc .r
    lda animMode
    beq .auto
    lda animNext
    cmp animIdx
    beq .r
    jmp .ok
.auto:
    lda animIdx
    clc
    adc animDir
    ldx animDir
    bmi .back
    cmp animCount
    bcc .ok
    lda #0
    jmp .ok
.back:
    cmp #$FF
    bne .ok
    lda animCount
    sec
    sbc #1
.ok:
    sta animIdx
    asl a
    clc
    adc animBase
    sta animPending
.r:
    rts

; ---- 0: intro
SetupIntro:
    lda #LOW(IntroStream)
    sta typePtr
    lda #HIGH(IntroStream)
    sta typePtr+1
    lda #0
    sta typeT
    jmp SetupDone

IntroTick:
    inc typeT
    lda typeT
    and #3
    bne .r
.next:
    ldy #0
    lda (typePtr),y
    beq .r
    cmp #1
    bne .ch
    iny
    lda (typePtr),y
    sta typeDst
    iny
    lda (typePtr),y
    sta typeDst+1
    lda typePtr
    clc
    adc #3
    sta typePtr
    bcc .next
    inc typePtr+1
    jmp .next
.ch:
    clc
    adc #$E0
    jsr QueueAtDst
    inc typeDst
    bne .i
    inc typeDst+1
.i:
    inc typePtr
    bne .r
    inc typePtr+1
.r:
    rts

; ---- 1: wireframe, 2: cubes
; ---- 7: wobbly text (both screens, for the scroll wrap) and a sprite sine scroller
SetupScroll:
    ldx #T_WOB1
    lda #$E0
    jsr Print
    ldx #T_WOB1B
    lda #$E0
    jsr Print
    ldx #T_WOB2
    lda #$E0
    jsr Print
    ldx #T_WOB2B
    lda #$E0
    jsr Print
    ldx #T_WOB3
    lda #$E0
    jsr Print
    ldx #T_WOB3B
    lda #$E0
    jsr Print
    ldx #T_WOB4
    lda #$E0
    jsr Print
    ldx #T_WOB4B
    lda #$E0
    jsr Print
    lda #0
    sta scrollSub
    sta scrollChar
    lda #1
    sta wobOn
    jmp SetupDone

ScrollTick:
    lda frame
    and #3
    bne .np
    lda frame
    lsr a
    lsr a
    and #15
    tax
    lda RainNes,x
    sta palColor
    lda #1
    sta palDirty
.np:
    inc scrollSub
    lda scrollSub
    and #7
    sta scrollSub
    bne .n
    inc scrollChar
    lda scrollChar
    cmp #MSG_LEN
    bcc .n
    lda #0
    sta scrollChar
.n:
    ldx #$40
    ldy scrollChar
    lda #8
    sta tmp3
.l:
    lda tmp3
    sec
    sbc scrollSub
    sta tmpX
    lda tmpX                    ; one wave across 256 px, so only one crest is on screen
    lsr a
    lsr a
    clc
    adc frame
    and #63
    sty tmp
    tay
    lda SineY,y
    ldy tmp
    sta OAM,x
    lda ScrollMsg,y
    clc
    adc #$E0
    sta OAM+1,x
    lda #0
    sta OAM+2,x
    lda tmpX
    sta OAM+3,x
    iny
    cpy #MSG_LEN
    bcc .k
    ldy #0
.k:
    inx
    inx
    inx
    inx
    lda tmp3
    clc
    adc #8
    sta tmp3
    cpx #$40+31*4
    bne .l
    rts

; ---- 4: input test
SetupInput:
    lda #0
    sta padShown
    sta idle
    sta idle+1
    ldx #T_INPUT_TITLE
    lda #$E0
    jsr Print
    ldy #0
    lda #8
    sta ncount
.n:
    lda PadNames+1,y
    sta PPUADDR
    lda PadNames,y
    sta PPUADDR
    lda PadNames+2,y
    sta nlen
    iny
    iny
    iny
.c:
    lda PadNames,y
    clc
    adc #$A0
    sta PPUDATA
    iny
    dec nlen
    bne .c
    dec ncount
    bne .n
    lda #LOW(PAD_NT)
    sta ptr
    lda #HIGH(PAD_NT)
    sta ptr+1
    lda #PAD_W
    sta tmp
    lda #PAD_H
    sta tmp2
    jsr RegionMap
    lda #PAD_BANK
    jsr SetChr
    jmp SetupDone

InputTick:
    lda pad
    beq .idle
    lda #0
    sta idle
    sta idle+1
    jmp .draw
.idle:
    inc idle
    bne .ck
    inc idle+1
.ck:
    lda idle+1
    cmp #3
    bcc .draw
    jmp NextScene
.draw:
    lda pad
    sta tmp
    ldx #$40
    ldy #0
.s:
    lsr tmp
    lda #$FF
    bcc .off
    lda PadSpots,y
.off:
    sta OAM,x
    lda #DISK_TILE
    sta OAM+1,x
    lda #0
    sta OAM+2,x
    lda PadSpots+1,y
    sta OAM+3,x
    iny
    iny
    inx
    inx
    inx
    inx
    cpx #$60
    bne .s
    lda pad
    cmp padShown
    beq .r
    sta padShown
    sta tmp
    ldy #0
    lda #8
    sta ncount
.nm:
    lda PadNames,y
    sta typeDst
    iny
    lda PadNames,y
    sta typeDst+1
    iny
    lda PadNames,y
    sta nlen
    iny
    lsr tmp
    lda #$E0
    bcs .br
    lda #$A0
.br:
    sta noff
.ch:
    lda PadNames,y
    clc
    adc noff
    jsr QueueAtDst
    inc typeDst
    iny
    dec nlen
    bne .ch
    dec ncount
    bne .nm
.r:
    rts
