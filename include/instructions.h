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

#ifndef ORCHARD_ISNS_H_
#define ORCHARD_ISNS_H_

#include "z80.h"

/* Macro to add the given number of clock cycles. */
#define CLK(n) actual += n * 4

/* Macros representing instructions of the Z80. */
#define ADC(IN) ADD(IN + !!FLAG(CARRY))

#define ADD(IN) ADD8(A, IN)

#define ADD8(OUT, IN)                     \
  T1   = IN;                              \
  T2   = OUT;                             \
  F    = 0;                               \
  OUT += T1;                              \
  if(OUT < T2) SETFLAG(CARRY);            \
  T3 = (T2 & 0xf) + (T1 & 0xf);           \
  if(T3 > 0xf) SETFLAG(HALFCARRY);        \
  if(!OUT) SETFLAG(ZERO);                 \
  RESETFLAG(SUBTRACTION);

#define ADD16(OUT, IN)                           \
  RESETFLAG(SUBTRACTION);                        \
  if((OUT + IN) > 0xffff) SETFLAG(CARRY);        \
  else                    RESETFLAG(CARRY);      \
  if(((OUT & 0xFF00) & 0xF) + ((IN >> 8) & 0xF)) \
    SETFLAG(HALFCARRY);                          \
  else                                           \
    RESETFLAG(HALFCARRY);                        \
  OUT += IN;                                     \
  CLK(2);

#define AND(IN)                    \
  A   &= IN;                       \
  F = HALFCARRY | (!A ? ZERO : 0); \

#undef BIT
#define BIT(B, R)                   \
  if(R & (1 << B)) RESETFLAG(ZERO); \
  else             SETFLAG(ZERO);   \
  SETFLAG(HALFCARRY);               \
  RESETFLAG(SUBTRACTION);           \
  CLK(2)

#define CALL(PRED)           \
  if(PRED) {                 \
    PUSHWORD(PC + 2);        \
    PC  = GET16(PC);         \
    CLK(6);                  \
  }                          \
  else {                     \
    PC += 2;                 \
    CLK(3);                  \
  }

#define CCF()                         \
  RESETFLAG(SUBTRACTION | HALFCARRY); \
  FLIPFLAG(CARRY);                    \
  CLK(1)

#define CP(IN)                   \
  T1 = IN;                       \
  T2 = A;                        \
                                 \
  F  = 0;                        \
  A -= T1;                       \
  SETFLAG(SUBTRACTION);          \
  SETFLAG(!A ? ZERO : 0);        \
  SETFLAG((A > T2) ? CARRY : 0); \
  S1  = T2 & 0xf;                \
  S1 -= T1 & 0xf;                \
  if(S1 < 0) SETFLAG(HALFCARRY); \
  A = T2

#define CPL()                       \
  A = ~A;                           \
  SETFLAG(SUBTRACTION | HALFCARRY); \
  CLK(1)

#define DAA()                                    \
  T1 = A;                                        \
  T2 = FLAG(CARRY);                              \
  if(((A & 0xf) > 9) || FLAG(HALFCARRY)) {       \
    SETFLAG(T2 | CADD(A, 6));                    \
    A += 6;                                      \
  }                                              \
                                                 \
  if((T1 > 0x99) || T2) {                        \
    A += 0x60;                                   \
    SETFLAG(CARRY);                              \
  }                                              \
  else RESETFLAG(CARRY);                         \
  RESETFLAG(HALFCARRY);                          \
  if(!A) SETFLAG(ZERO);                          \
  CLK(1);

#define DEC8(OUT)                  \
  S1 = (OUT & 0xf) - 1;            \
  --OUT;                           \
  F  = FLAG(CARRY);                \
  SETFLAG(SUBTRACTION);            \
  SETFLAG(!OUT ? ZERO : 0);        \
  SETFLAG(S1 < 0 ? HALFCARRY : 0); \
  CLK(1)

#define DEC16(OUT) \
  --OUT;           \
  CLK(2)

#define DI() \
  IME = 0;   \
  CLK(1);

#define EI() \
  IME = 1;   \
  CLK(1);

#define HALT() TODO("HALT")
  
#define INC8(OUT)   \
  T1 = FLAG(CARRY); \
  ADD8(OUT, 1);     \
  F |= T1;          \
  CLK(1)
  
#define INC16(OUT) \
  ++OUT;           \
  CLK(2)

#define JP(PRED)    \
  if(PRED) {        \
    PC = GET16(PC); \
    CLK(4);         \
  }                 \
  else {            \
    PC += 2;        \
    CLK(3);         \
  }

#define JR(PRED)            \
  if(PRED) {                \
    PC += (int8_t)GET8(PC) + 1; \
    CLK(3);                 \
  }                         \
  else {                    \
    ++PC;                   \
    CLK(2);                 \
  }

#define LD(OUT, IN)  OUT  = IN

#define LDMEMIN(OUT, MEM) OUT = GET8(MEM)
#define LDMEMOUT(MEM, IN) PUT8(MEM, IN)

#define OR(IN)        \
  A |= IN;            \
  F  = !A ? ZERO : 0

#define POP(IN)   \
  IN = POPWORD(); \
  CLK(3)

#define PUSH(IN) \
  PUSHWORD(IN);  \
  CLK(4)

#define RES(B, R) \
  R &= ~(1 << B); \
  CLK(2)

/* TODO: RET and RETI only take 4 cycles. */
#define RET(PRED)   \
  if(PRED) {        \
    PC = POPWORD(); \
    CLK(5);         \
  }                 \
  else {            \
    CLK(2);         \
  }

#define ROTL(x, n) ((x << n) | (x >> (8 - n)))
#define ROTR(x, n) ((x >> n) | (x << (8 - n)))

#define RL(n) \
  n = ROTL(n, 1); \
  T1 = n & 1; \
  if(FLAG(CARRY)) n |= 1; else n &= ~1; \
  F = 0; \
  SETFLAG((n ? 0 : ZERO) | (T1 ? CARRY : 0)); \
  CLK(2)

#define RLA()  TODO("RLA")
#define RLC(n) TODO("RLC")

#define RLCA()                        \
  A = ROTL(A, 1);                     \
  RESETFLAG(SUBTRACTION | HALFCARRY); \
  SETFLAG((A ? 0 : ZERO) | ((A & 1) ? CARRY : 0))

#define RR(n)  TODO("RR")
#define RRA()  TODO("RRA")
#define RRC(n) TODO("RRC")
#define RRCA() TODO("RRCA")

#define RST(IN) \
  PUSHWORD(PC); \
  PC  = IN;     \
  CLK(4)

#define SBC(IN) SUB8(A, IN + !!FLAG(CARRY))

#define SCF()                         \
  RESETFLAG(SUBTRACTION | HALFCARRY); \
  SETFLAG(CARRY);                     \

#define SET(B, R) \
  R |= (1 << B);  \
  CLK(2)

#define SLA(IN)       \
  F = 0;              \
  if(IN & (1 << 7)) { \
    SETFLAG(CARRY);   \
  }                   \
  if(!(IN <<= 1)) {   \
    SETFLAG(ZERO);    \
  }                   \
  RESETFLAG(SUBTRACTION | HALFCARRY); \
  CLK(2)

#define SRA(n) TODO("SRA")
#define SRL(n) TODO("SRL")

/* TODO: Make actually work. :P */
#define STOP() DBG("STOP instruction encountered"); do { } while(1)

#define SUB(IN) SUB8(A, IN)

#define SUB8(OUT, IN)              \
  T1   = IN;                       \
  T2   = OUT;                      \
  F    = 0;                        \
  OUT -= T1;                       \
  SETFLAG(SUBTRACTION);            \
  SETFLAG(!OUT ? ZERO : 0);        \
  SETFLAG((OUT > T2) ? CARRY : 0); \
  S1  = T2 & 0xf;                  \
  S1 -= T1 & 0xf;                  \
  if(S1 < 0) SETFLAG(HALFCARRY)

#define SWAP(IN)                              \
  if(!IN) SETFLAG(ZERO);                      \
  IN = ((IN & 0x0f) << 4) | (IN >> 4);        \
  RESETFLAG(SUBTRACTION | HALFCARRY | CARRY); \
  CLK(2)

#define XOR(IN)      \
  A ^= IN;           \
  SETFLAG(!A ? ZERO : 0)

#endif
