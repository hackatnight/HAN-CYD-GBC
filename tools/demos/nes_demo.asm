; H@N demo for the NES. Original code and graphics. Mapper 4 (MMC3), 32 KB PRG, 256 KB CHR.
; Built by build.py, which puts the generated constants first and the data last.
;
; Same scenes as the Game Boy demo: 0 intro, 1 wireframe H@N, 2 cubes, 3 wobbly text and a
; sine scroller, 4 input test. A = next scene, B = music on/off, START = input test.
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
    lda #$05                    ; pulse 1 and triangle
    sta $4015
    lda #1
    sta musOn
    sta musT
    sta starsOn
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
    lda wobOn
    beq .ni
    sta MMC_IRQOFF
    lda #WOB_TOP
    sta MMC_LATCH
    sta MMC_RELOAD
    sta MMC_IRQON
    lda #0
    sta irqLine
.ni:
    inc frame
    lda #1
    sta nmiFlag
    pla
    tay
    pla
    tax
    pla
    rti

IRQ:                            ; MMC3 scanline IRQ: wobble the X scroll every two lines
    pha
    txa
    pha
    sta MMC_IRQOFF
    lda #1
    sta MMC_LATCH
    sta MMC_IRQON
    ldx irqLine
    inc irqLine
    cpx #WOB_COUNT
    bcc .ok
    sta MMC_IRQOFF
    lda #0
    jmp .w
.ok:
    txa
    clc
    adc frame
    and #63
    tax
    lda SineTab,x
.w:
    bit PPUSTATUS
    sta PPUSCROLL
    lda #0
    sta PPUSCROLL
    pla
    tax
    pla
    rti

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
Music:
    lda musOn
    beq .r
    dec musT
    bne .r
    lda #MUS_SPEED
    sta musT
    ldy musStep
    lda MusLead,y
    beq .nl
    tax
    lda #$84
    sta $4000
    lda #$08
    sta $4001
    lda PulseLo,x
    sta $4002
    lda PulseHi,x
    ora #$08
    sta $4003
.nl:
    lda MusBass,y
    beq .nb
    tax
    lda #$30
    sta $4008
    lda TriLo,x
    sta $400A
    lda TriHi,x
    ora #$08
    sta $400B
.nb:
    iny
    cpy #MUS_LEN
    bcc .s
    ldy #0
.s:
    sty musStep
.r:
    rts

ToggleMusic:
    lda musOn
    eor #1
    sta musOn
    beq .off
    lda #$05
    sta $4015
    rts
.off:
    lda #0
    sta $4015
    rts

; ---------------------------------------------------------------- stars (sprites 0-15)
Stars:
    lda starsOn
    bne .on
    ldx #0
    lda #$FF
.h:
    sta OAM,x
    inx
    inx
    inx
    inx
    cpx #64
    bne .h
    rts
.on:
    ldx #0
    ldy #0
.l:
    lda StarData,y
    sta OAM,x
    lda StarData+2,y
    sta OAM+1,x
    lda #0
    sta OAM+2,x
    lda OAM+3,x
    sec
    sbc StarData+1,y
    sta OAM+3,x
    iny
    iny
    iny
    inx
    inx
    inx
    inx
    cpx #64
    bne .l
    rts

; ---------------------------------------------------------------- scenes
SceneTick:
    lda scene
    cmp #SC_INPUT
    bne .n0
    jmp InputTick
.n0:
    lda padNew
    and #$08
    beq .ns
    lda #SC_INPUT
    jmp GoScene
.ns:
    lda padNew
    and #$01
    beq .na
    jmp NextScene
.na:
    lda padNew
    and #$02
    beq .nb
    jsr ToggleMusic
.nb:
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
    jsr AnimTick
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
    sta qLen
    sta sceneT
    sta sceneT+1
    sta palDirty
    lda #$FF
    sta animPending
    lda #1
    sta starsOn
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
    lda scene
    asl a
    tax
    lda SetupTable+1,x
    pha
    lda SetupTable,x
    pha
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
    inc animIdx
    lda animIdx
    cmp animCount
    bcc .ok
    lda #0
    sta animIdx
.ok:
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
    ldx #T_MODE
    lda #$A0
    jsr Print
    ldx #T_HINT
    lda #$A0
    jsr Print
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
SetupWire:
    ldx #T_WIRE_TITLE
    lda #$E0
    jsr Print
    ldx #T_WIRE_CAP
    lda #$A0
    jsr Print
    ldx #T_HINT
    lda #$A0
    jsr Print
    lda #LOW(WIRE_NT)
    sta ptr
    lda #HIGH(WIRE_NT)
    sta ptr+1
    lda #WIRE_W
    sta tmp
    lda #WIRE_H
    sta tmp2
    jsr RegionMap
    lda #WIRE_BANK
    sta animBase
    lda #WIRE_COUNT
    sta animCount
    lda #2
    sta animDelay
    jsr AnimStart
    jmp SetupDone

SetupCubes:
    ldx #T_CUBE_TITLE
    lda #$E0
    jsr Print
    ldx #T_CUBE_CAP
    lda #$A0
    jsr Print
    ldx #T_HINT
    lda #$A0
    jsr Print
    lda #LOW(CUBE_NT)
    sta ptr
    lda #HIGH(CUBE_NT)
    sta ptr+1
    lda #CUBE_W
    sta tmp
    lda #CUBE_H
    sta tmp2
    jsr RegionMap
    lda #CUBE_BANK
    sta animBase
    lda #CUBE_COUNT
    sta animCount
    lda #2
    sta animDelay
    jsr AnimStart
    jmp SetupDone

; ---- 3: wobbly text (both screens, for the scroll wrap) and a sprite sine scroller
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
    ldx #T_HINT
    lda #$A0
    jsr Print
    lda #0
    sta scrollSub
    sta scrollChar
    lda #1
    sta wobOn
    lda #0                      ; no stars: at most 8 sprites fit on a line
    sta starsOn
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
    sta starsOn
    sta padShown
    sta idle
    sta idle+1
    ldx #T_INPUT_TITLE
    lda #$E0
    jsr Print
    ldx #T_INPUT_HINT
    lda #$A0
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
