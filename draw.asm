    section .text.draw_sd_mono_bitmap

XVID_WR_ADDR      equ   $C            ; VRAM Write Address          (RW)
XVID_DATA         equ   $10           ; VRAM Data (First word)      (RW)
XVID_DATA_H       equ   $10           ; VRAM Data (High byte)       (RW)
XVID_DATA_L       equ   $12           ; VRAM Data (Low byte)        (RW)

; Draw bitmap, C callable. See xosera_video_demo.c for prototype. 
draw_sd_mono_bitmap::
    movem.l D0-D3/A0-A1,-(A7)       ; Save regs

    move.l  #$FC0060,A0             ; Xosera base
    move.l  $24(A7),D1              ; Attribute
    move.l  $20(A7),D0              ; Frame size (Max: 16 bits!)
    move.l  $1C(A7),A1              ; Buffer

    clr.l   D2                      ; Current video addr
    clr.l   D3                      ; Current X coord

    movep.w D2,XVID_WR_ADDR(A0)     ; Start write at bottom of VRAM
    move.b  D1,XVID_DATA_H(A0)      ; Set attribute for all following writes

    bra.s   .LOOPSTART

.LOOP
    addq.b  #1,D3
    cmp.b   #106,D3
    bne.s   .NOEOL

    clr.b   D3
    addi.w  #212,D2
    movep.w D2,XVID_WR_ADDR(A0)     ; Move write to start of new line
    move.b  D1,XVID_DATA_H(A0)      ; Re-set attribute (TODO is this needed?)

.NOEOL
    move.b  (A1)+,XVID_DATA_L(A0)

.LOOPSTART
    dbra.w  D0,.LOOP

    movem.l (A7)+,D0-D3/A0-A1
    rts

        



;static void draw_sd_mono_bitmap(uint8_t *buffer, uint32_t size, uint8_t attr) {
;    int vaddr = 0;
;
;    xv_setw(wr_addr, vaddr);
;    xv_setbh(data, attr);
;
;    if (size > 0x10000) {
;        printf("FAILED; Buffer too large for VRAM...\n");
;    } else {
;        uint8_t x = 0;
;        for (uint32_t i = 0; i < size; i++, x++) {
;            if (x == 106) {
;                x = 0;
;                vaddr += 212;
;                xv_setw(wr_addr, vaddr);
;                xv_setbh(data, attr);
;            }
;            xv_setbl(data, *buffer++);
;        }
;    }
;}

