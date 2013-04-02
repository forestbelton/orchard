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

#ifndef ORCHARD_Z80_H_
#define ORCHARD_Z80_H_

#include <stdint.h>

/* Macros to test various values of the flag register. */
#define FLAG(FLAG)  (F & (FLAG))
#define ZERO        (1 << 7)
#define SUBTRACTION (1 << 6)
#define HALFCARRY   (1 << 5)
#define CARRY       (1 << 4)

/* Macros to set and reset various values of the flag register. */
#define SETFLAG(FLAG)   F |=  (FLAG)
#define RESETFLAG(FLAG) F &= ~(FLAG)
#define FLIPFLAG(FLAG)  F ^=  (FLAG)

/* Macros representing the 8-bit registers. Note that
*  the values of the indices will have to be flipped for
*  a big-endian architecture. I couldn't find a portable way
*  around this, sadly enough. ): */
#define A _AF[1]
#define B _BC[1]
#define C _BC[0]
#define D _DE[1]
#define E _DE[0]
#define F _AF[0]
#define H _HL[1]
#define L _HL[0]

/* Macros representing the 16-bit registers. */
#define AF (*((uint16_t*)_AF))
#define BC (*((uint16_t*)_BC))
#define DE (*((uint16_t*)_DE))
#define HL (*((uint16_t*)_HL))
#define SP _SP
#define PC _PC

extern uint8_t  _AF[2], _BC[2], _DE[2], _HL[2];
extern uint16_t _SP,    _PC;
extern uint8_t  IME;
extern uint8_t z80_memory[0xffff+1];

/* Function prototypes. */
void           z80_init   (void);
uint8_t        z80_execute(void);
inline uint8_t GET8       (uint16_t addr);
inline void    PUT8       (uint16_t addr, uint8_t value);
inline void    PUSHWORD   (uint16_t value);

#endif
