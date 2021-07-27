/*
 * vim: set et ts=4 sw=4
 *------------------------------------------------------------
 *                                  ___ ___ _
 *  ___ ___ ___ ___ ___       _____|  _| . | |_
 * |  _| . |_ -|  _| . |     |     | . | . | '_|
 * |_| |___|___|___|___|_____|_|_|_|___|___|_,_|
 *                     |_____|
 * ------------------------------------------------------------
 * Copyright (c) 2021 Ross Bamford
 * Heavily based on (and portions reproduced from) Xark's 
 * xosera_test_m68k. Copyright (c) 2021 Xark
 *
 * MIT License
 *
 * Full-screen half frame video tech demo for Xosera
 * ------------------------------------------------------------
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <basicio.h>
#include <machine.h>
#include <sdfat.h>

#include "xosera_api.h"

/*
 * Options - Change these to suit your tastes
 */

// Directory on SD card containing frames with filenames like "0001.xmb".
// The files must be consecutive, and start at 1. A maximum of 30 will be
// loaded.
// WARNING: Max 9 characters!
#define FRAME_DIR   "walk"    

// Sets the (starting, if effects are used) attribute to draw the animation.
// High nybble is background, low is foreground
#define ATTR        0x0F

// One of the following two may be defined for "effects"
//#define SLOW_CYCLE                // Define to slowly cycle normal/inverse
#define PSYCHEDELIC               // Define to quickly cycle colours

// Define this to use the assembly draw routine (in `draw.asm`). It's not 
// really any faster...
#define ASSEMBLY_DRAW


/*
 * Probably leave the rest of the defines alone unless you know what you're doing...
 */
#define FRAME_SIZE  25440

extern void* _end;

// Define rosco_m68k Xosera board base address pointer (See
// https://github.com/rosco-m68k/hardware-projects/blob/feature/xosera/xosera/code/pld/decoder/ic3_decoder.pld#L25)
volatile xreg_t * const xosera_ptr = (volatile xreg_t * const)0xf80060;        // rosco_m68k Xosera base

#if !defined(checkchar)        // newer rosco_m68k library addition, this is in case not present
bool checkchar() {
    int rc;
    __asm__ __volatile__(
        "move.l #6,%%d1\n"        // CHECKCHAR
        "trap   #14\n"
        "move.b %%d0,%[rc]\n"
        "ext.w  %[rc]\n"
        "ext.l  %[rc]\n"
        : [rc] "=d"(rc)
        :
        : "d0", "d1");
    return rc != 0;
}
#endif

static void dputc(char c) {
    __asm__ __volatile__(
        "move.w %[chr],%%d0\n"
        "move.l #2,%%d1\n"        // SENDCHAR
        "trap   #14\n"
        :
        : [chr] "d"(c)
        : "d0", "d1");
}

static void dprint(const char * str) {
    register char c;
    while ((c = *str++) != '\0')
    {
        if (c == '\n')
        {
            dputc('\r');
        }
        dputc(c);
    }
}

static char dprint_buff[4096];
static void dprintf(const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(dprint_buff, sizeof(dprint_buff), fmt, args);
    dprint(dprint_buff);
    va_end(args);
}

// Call when in bitmap mode!
static void xcls() {
    xv_setw(wr_addr, 0);
    xv_setw(wr_inc, 1);
    xv_setbh(data, 0);
    for (uint16_t i = 0; i < (106 * 480); i++) {
        xv_setbl(data, 0);
    }
    xv_setw(wr_addr, 0);
}

static void xmsg(int x, int y, int color, const char * msg) {
    xv_setw(wr_addr, (y * 106) + x);
    xv_setbh(data, color);
    char c;
    while ((c = *msg++) != '\0') {
        xv_setbl(data, c);
    }
}

static uint16_t rosco_m68k_CPUMHz() {
    uint32_t count;
    uint32_t tv;
    __asm__ __volatile__(
        "   moveq.l #0,%[count]\n"
        "   move.w  _TIMER_100HZ+2.w,%[tv]\n"
        "0: cmp.w   _TIMER_100HZ+2.w,%[tv]\n"
        "   beq.s   0b\n"
        "   move.w  _TIMER_100HZ+2.w,%[tv]\n"
        "1: addq.w  #1,%[count]\n"                   //   4  cycles
        "   cmp.w   _TIMER_100HZ+2.w,%[tv]\n"        //  12  cycles
        "   beq.s   1b\n"                            // 10/8 cycles (taken/not)
        : [count] "=d"(count), [tv] "=&d"(tv)
        :
        :);
    uint16_t MHz = ((count * 26) + 500) / 1000;
    dprintf("rosco_m68k: m68k CPU speed %d.%d MHz (%d.%d BogoMIPS)\n",
            MHz / 10,
            MHz % 10,
            count * 3 / 10000,
            ((count * 3) % 10000) / 10);

    return (MHz + 5) / 10;
}

static uint32_t load_sd_mono_bitmap(const char *filename, uint8_t *buffer) {
    uint8_t *bufptr = buffer;
    int cnt = 0;
    void * file = fl_fopen(filename, "r");

    if (file != NULL) {
        while ((cnt = fl_fread(bufptr, 1, 512, file)) > 0) {
            bufptr += cnt;
        }

        fl_fclose(file);

        return (uint32_t)bufptr - (uint32_t)buffer;
    }

    return 0;
}

#ifdef ASSEMBLY_DRAW
extern void draw_sd_mono_bitmap(uint8_t *buffer, uint32_t size, uint8_t attr);
#else
static void draw_sd_mono_bitmap(uint8_t *buffer, uint32_t size, uint8_t attr) {
    int vaddr = 0;

    xv_setw(wr_addr, vaddr);
    xv_setbh(data, attr);

    if (size > 0x10000) {
        printf("FAILED; Buffer too large for VRAM...\n");
    } else {
        uint8_t x = 0;
        for (uint32_t i = 0; i < size; i++, x++) {
            if (x == 106) {
                x = 0;
                vaddr += 212;
                xv_setw(wr_addr, vaddr);
                xv_setbh(data, attr);
            }
            xv_setbl(data, *buffer++);
        }
    }
}
#endif

/*
 * Loads a maximum of 30 frames numbered 0001-0030. 
 * Actual number loaded is returned.
 */
static uint8_t load_frames(uint8_t *buffer) {
    uint8_t *bufptr = buffer;
    char strbuf[21];

    for (int i = 0; i < 30; i++) {
        if (sprintf(strbuf, "/" FRAME_DIR "/%04d.xmb", i + 1) < 0) {
            printf("sprintf failed!\n");
            return i;
        }

        uint8_t color = (i & 0xF);
        if (!color) {
            color = 8;
        }
        
        if (load_sd_mono_bitmap(strbuf, bufptr) != FRAME_SIZE) {
            printf("Failed loading %s!\n", strbuf);
            return i;
        }
        
        xmsg(80, i, color, "Loaded ");
        xmsg(87, i, color, strbuf);


        bufptr += FRAME_SIZE;
    }

    return 30;
}

void xosera_demo() {
    // flush any input charaters to avoid instant exit
    while (checkchar())
    {
        readchar();
    }

    dprintf("Xosera video demo (m68k)\n");

    if (SD_check_support())
    {
        dprintf("SD card supported: ");

        if (SD_FAT_initialize())
        {
            dprintf("card ready\n");
        }
        else
        {
            dprintf("no card; quitting\n");
            return;
        }
    }
    else
    {
        dprintf("No SD card support; quitting.\n");
        return;
    }

    dprintf("\nxosera_init(1)...");
    // wait for monitor to unblank
    bool success = xosera_init(1);
    dprintf("%s (%dx%d)\n", success ? "succeeded" : "FAILED", xv_reg_getw(vidwidth), xv_reg_getw(vidheight));

    rosco_m68k_CPUMHz();

    uint8_t *buffer = (uint8_t*)&_end;
    uint8_t frame_count;

    xmsg(1,  2, 0x9, "Video");
    xmsg(7,  2, 0xA, "Tech");
    xmsg(12, 2, 0xC, "Demo");
    xmsg(17, 2, 0xB, "by");
    xmsg(20, 2, 0xE, "@roscopeco");

    if ((frame_count = load_frames(buffer))) {
        dprintf("Loaded %d frames\n", frame_count);

        xv_reg_setw(gfxctrl, 0x8000);
        xcls();

        uint8_t current_frame = frame_count;
        uint8_t *bufptr = buffer;
#if defined SLOW_CYCLE || defined PSYCHEDELIC
        uint16_t counter = 0;
#endif
        uint8_t attr = ATTR;

        while (true) {     
            if (current_frame == frame_count) {
                current_frame = 0;
                bufptr = buffer;
            }

            draw_sd_mono_bitmap(bufptr, FRAME_SIZE, attr);

            current_frame++;
            bufptr += FRAME_SIZE;

#ifdef SLOW_CYCLE
            switch (counter++) {
            case 20:
                attr = 0x87;
                break;
            case 21:
                attr = 0x78;
                break;
            case 22:
                attr = 0xF0;
                break;
            case 42:
                attr = 0x78;
                break;
            case 43:
                attr = 0x87;
                break;
            case 44:
                attr = 0x0F;
                counter = 0;
                break;
            }
#else
#ifdef PSYCHEDELIC
            if (++counter == 2) {
                attr += 1;
                counter = 0;
            }
#endif
#endif
        }
    } else {
        printf("Load failed; No frames :(\n");
    }
}

