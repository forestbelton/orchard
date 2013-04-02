/*
 * Copyright (c) 2010 Forest Belton (apples)
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ORCHARD_GB_H_
#define ORCHARD_GB_H_

#include <stdint.h>
#include "z80.h"

/* Macros that expand to memory mapped registers. */
#define MMAP(n) z80_memory[0xff00+n]
#define TIMA MMAP(0x05)
#define TMA  MMAP(0x06)
#define TAC  MMAP(0x07)
#define IF   MMAP(0x0f)
#define NR10 MMAP(0x10)
#define NR11 MMAP(0x11)
#define NR12 MMAP(0x12)
#define NR14 MMAP(0x14)
#define NR21 MMAP(0x16)
#define NR22 MMAP(0x17)
#define NR24 MMAP(0x19)
#define NR30 MMAP(0x1a)
#define NR31 MMAP(0x1b)
#define NR32 MMAP(0x1c)
#define NR33 MMAP(0x1e)
#define NR41 MMAP(0x20)
#define NR42 MMAP(0x21)
#define NR43 MMAP(0x22)
#define NR40 MMAP(0x23)
#define NR50 MMAP(0x24)
#define NR51 MMAP(0x25)
#define NR52 MMAP(0x26)
#define LCDC MMAP(0x40)
#define STAT MMAP(0x41)
#define SCY  MMAP(0x42)
#define SCX  MMAP(0x43)
#define LY   MMAP(0x44)
#define LYC  MMAP(0x45)
#define BGP  MMAP(0x47)
#define OBP0 MMAP(0x48)
#define OBP1 MMAP(0x49)
#define WY   MMAP(0x4a)
#define WX   MMAP(0x4b)
#define IE   MMAP(0xff)

void gb_init(void);
void gb_run(void);
void gb_set_clock(void);

extern uint8_t bank_count;
extern uint8_t (*banks)[0x4000];
extern int sstep;

#endif
