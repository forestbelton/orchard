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

#include <stdint.h>
#include <stdio.h>
#include <nds.h>

#include "instructions.h"
#include "z80.h"
#include "gb.h"

#define DBG(msg)       if(sstep) { iprintf("%s\n", msg); }
#define DBGF(fmt, ...) if(sstep) { iprintf(fmt, __VA_ARGS__); iprintf("\n"); }
#define DUMPREGS()     if(sstep) { iprintf("AF: %04x, BC: %04x\n", AF, BC); \
                         iprintf("DE: %04x, HL: %04x\n", DE, HL); \
                         iprintf("PC: %04x, SP: %04x\n", PC, SP); \
                         iprintf("Z %u C %u H %u N %u\n", !!FLAG(ZERO), \
                           !!FLAG(CARRY), !!FLAG(HALFCARRY), !!FLAG(SUBTRACTION)); }
#define TODO(ins)      iprintf("TODO: %s\n", ins); for(;;)
#define POLL()         if(sstep) { do { scanKeys(); if(keysDown() & KEY_B) break; if(keysDownRepeat() & KEY_A) break; } while(1); }
  
/* Actual Z80 memory. */
uint8_t z80_memory[0x10000] = {0};

/* Underlying register implementation. */
uint8_t  IME = 1;
uint8_t  _AF[2], _BC[2], _DE[2], _HL[2];
uint16_t _SP,    _PC;

/* Temporaries. */
uint8_t  T1, T2;
uint16_t T3;
uint32_t T4;
int16_t  S1;

int debug = 0;

/* Inline functions used for memory retrieval. */
inline uint8_t GET8(uint16_t addr) {
  /* TODO: Add support for bank-switching. */
  return z80_memory[addr];
}

inline uint16_t GET16(uint16_t addr) {
  return (GET8(addr+1) << 8) | GET8(addr);
}

inline void PUSHWORD(uint16_t word) {
  PUT8(--SP, (word >> 8) & 0xff);
  PUT8(--SP, (word >> 0) & 0xff);
}

inline uint16_t POPWORD() {
  uint16_t t;
  
  t   = GET8(SP+1) << 8 ;
  t  |= GET8(SP);
  SP += 2;
  
  return t;
}

/* Inline functions used for memory modification. */
void PUT8(uint16_t addr, uint8_t value) {
  /* Disallow write access to ROM. */
  if(addr < 0x8000) { }
  
  /* TODO: Add support for switchable RAM banks. */

  /* Writing to ECHO RAM also writes in regular RAM. */
  if((addr >= 0xe000) && (addr <= 0xfdff)) {
    z80_memory[addr] = value;
    z80_memory[addr - 0x2000] = value;
  }
  
  /* Disallow write access to restricted area. */
  else if((addr >= 0xfea0) && (addr < 0xfeff)) { }
  
  /* Writes to the division register zero it. */
  else if(addr == 0xff04) {
    z80_memory[addr] = 0;
  }
  
  /* Writes to the timer control register means we need to update it. */
  else if(addr == 0xff07) {
    uint8_t t        = z80_memory[addr];
    z80_memory[addr] = value;
    
    if(t != value) {
      gb_set_clock();
    }
  }
  
  /* Zero the scanline register upon write. */
  else if(addr == 0xff44) {
    z80_memory[addr] = 0;
  }
  
  /* Perform a DMA transfer. The data being written is the source address
   * divided by 100. DMA only has one destination; 0xfe00. 0xa0 bytes are
   * always written. */
  else if(addr == 0xff46) {
    uint16_t i, src = value << 8;
    for(i = 0; i < 0xa0; ++i) PUT8(0xfe00+i, GET8(src+i));
  }
  
  /* Otherwise, this is regular memory. */
  else {
    z80_memory[addr] = value;
  }
}

inline void PUT16(uint16_t addr, uint16_t value) {
  PUT8(addr,     (value >> 8) & 0xff);
  PUT8(addr + 1, (value >> 0) & 0xff);
}

/* Inline functions to detect carries. */
inline int HADD(uint8_t a, uint8_t b) {
  return ((a + b) & 0x10) << 1;
}

inline int CADD(uint8_t a, uint8_t b) {
  return ((a + b) & 0x0100) >> 4;
}

inline int HSUB(uint8_t a, uint8_t b) {
  return ((a - b) & 0x0f) ? 0 : HALFCARRY;
}

inline int CSUB(uint8_t a, uint8_t b) {
  return ((a - b) & 0xff) ? 0 : CARRY;
}

uint8_t z80_execute() {
  uint8_t actual = 0;
  
  if(sstep) {
    iprintf("%04x: ", PC);
  }
  
  while(!actual) {
    switch(GET8(PC++)) {
      /* NOP */
    case 0x00:
      DBG("NOP");
      CLK(1);
      break;

      /* LD BC, $aabb */
    case 0x01:
      DBGF("LD BC, 0x%04x", GET16(PC));
      LD(BC, GET16(PC));
      PC += 2;
      CLK(3);
      break;
      
      /* LD (BC), A */
    case 0x02:
      DBG("LD (BC), A");
      LDMEMOUT(BC, A);
      CLK(2);
      break;
      
      /* INC BC */
    case 0x03:
      DBG("INC BC");
      INC16(BC);
      break;
      
      /* INC B */
    case 0x04:
      DBG("INC B");
      INC8(B);
      break;
      
      /* DEC B */
    case 0x05:
      DBG("DEC B");
      DEC8(B);
      break;
      
      /* LD B, $xx */
    case 0x06:
      DBGF("LD B, 0x%02x", GET8(PC));
      LDMEMIN(B, PC++);
      CLK(2);
      break;
      
      /* RLCA */
    case 0x07:
      DBG("RLCA");
      RLCA();
      CLK(1);
      break;
      
      /* LD ($aabb), SP */
    case 0x08:
      DBGF("LD (0x%04x), SP", GET16(PC));
      PUT16(GET16(PC), SP);
      PC += 2;
      CLK(5);
      break;
      
      /* ADD HL, BC */
    case 0x09:
      DBG("ADD HL, BC");
      ADD16(HL, BC);
      break;
      
      /* LD A, (BC) */
    case 0x0a:
      DBG("LD A, (BC)");
      LDMEMIN(A, BC);
      CLK(2);
      break;
      
      /* DEC BC */
    case 0x0b:
      DBG("DEC BC");
      DEC16(BC);
      break;
      
      /* INC C */
    case 0x0c:
      DBG("INC C");
      INC8(C);
      CLK(1);
      break;
      
      /* DEC C */
    case 0x0d:
      DBG("DEC C");
      DEC8(C);
      CLK(1);
      break;
      
      /* LD C, $xx */
    case 0x0e:
      DBGF("LD C, 0x%02x", GET8(PC));
      LDMEMIN(C, PC++);
      CLK(2);
      break;
      
      /* RRCA */
    case 0x0f:
      DBG("RRCA");
      RRCA();
      break;
      
      /* STOP */
    case 0x10:
      DBG("STOP");
      STOP();
      break;
      
      /* LD DE, $aabb */
    case 0x11:
      DBGF("LD DE, 0x%04x", GET16(PC));
      LD(DE, GET16(PC));
      PC += 2;
      CLK(3);
      break;
      
      /* LD (DE), A */
    case 0x12:
      DBG("LD (DE), A");
      LDMEMOUT(DE, A);
      CLK(2);
      break;
      
      /* INC DE */
    case 0x13:
      DBG("INC DE");
      INC16(DE);
      break;
      
      /* INC D */
    case 0x14:
      DBG("INC D");
      INC8(D);
      break;
      
      /* DEC D */
    case 0x15:
      DBG("DEC D");
      DEC8(D);
      break;

      /* LD D, $xx */
    case 0x16:
      DBGF("LD D, 0x%02x", GET8(PC));
      LDMEMIN(D, PC++);
      CLK(2);
      break;

      /* RLA */
    case 0x17:
      DBG("RLA");
      RLA();
      break;

      /* JR $xx */
    case 0x18:
      DBGF("JR %d", (int8_t)GET8(PC));
      JR(1);
      break;

      /* ADD HL, DE */
    case 0x19:
      DBG("ADD HL, DE");
      ADD16(HL, DE);
      break;

      /* LD A, (DE) */
    case 0x1a:
      DBG("LD A, (DE)");
      LDMEMIN(A, DE);
      CLK(2);
      break;

      /* DEC DE */
    case 0x1b:
      DBG("DEC DE");
      DEC16(DE);
      break;

      /* INC E */
    case 0x1c:
      DBG("INC E");
      INC8(E);
      break;

      /* DEC E */
    case 0x1d:
      DBG("DEC E");
      DEC8(E);
      break;

      /* LD E, $xx */
    case 0x1e:
      DBGF("LD E, 0x%02x", GET8(PC));
      LDMEMIN(E, PC++);
      CLK(2);
      break;

      /* RRA */
    case 0x1f:
      DBG("RRA");
      RRA();
      CLK(1);
      break;

      /* JR NZ, $xx */
    case 0x20:
      DBGF("JR NZ, %d", (int8_t)GET8(PC));
      JR(!FLAG(ZERO));
      break;

      /* LD HL, $aabb */
    case 0x21:
      DBGF("LD HL, 0x%04x", GET16(PC));
      LD(HL, GET16(PC));
      PC += 2;
      CLK(3);
      break;

      /* LDI (HL), A */
    case 0x22:
      DBG("LDI (HL), A");
      LDMEMOUT(HL++, A);
      CLK(2);
      break;

      /* INC HL */
    case 0x23:
      DBG("INC HL");
      INC16(HL);
      break;

      /* INC H */
    case 0x24:
      DBG("INC H");
      INC8(H);
      break;

      /* DEC H */
    case 0x25:
      DBG("DEC H");
      DEC8(H);
      break;

      /* LD H, $xx */
    case 0x26:
      DBGF("LD H, 0x%02x", GET8(PC));
      LDMEMIN(H, PC++);
      CLK(2);
      break;

      /* DAA */
    case 0x27:
      DBG("DAA");
      DAA();
      break;

      /* JR Z, $xx */
    case 0x28:
      DBGF("JR Z, %d", (int8_t)GET8(PC));
      JR(FLAG(ZERO));
      break;

      /* ADD HL, HL */
    case 0x29:
      DBG("ADD HL, HL");
      ADD16(HL, HL);
      break;

      /* LDI A, (HL) */
    case 0x2a:
      DBG("LDI A, (HL)");
      LDMEMIN(A, HL++);
      CLK(2);
      break;

      /* DEC HL */
    case 0x2b:
      DBG("DEC HL");
      DEC16(HL);
      break;

      /* INC L */
    case 0x2c:
      DBG("INC L");
      INC8(L);
      break;

      /* DEC L */
    case 0x2d:
      DBG("DEC L");
      DEC8(L);
      break;

      /* LD L, $xx */
    case 0x2e:
      DBGF("LD L, 0x%02x", GET8(PC));
      LDMEMIN(L, PC++);
      CLK(2);
      break;

      /* CPL */
    case 0x2f:
      DBG("CPL");
      CPL();
      break;

      /* JR NC, $xx */
    case 0x30:
      DBGF("JR NC, %d", GET8(PC));
      JR(!FLAG(CARRY));
      break;

      /* LD SP, $aabb */
    case 0x31:
      DBGF("LD SP, 0x%04x", GET16(PC));
      LD(SP, GET16(PC));
      PC += 2;
      CLK(3);
      break;

      /* LDD (HL), A */
    case 0x32:
      DBG("LDD (HL), A");
      LDMEMOUT(HL--, A);
      CLK(2);
      break;

      /* INC SP */
    case 0x33:
      DBG("INC SP");
      INC16(SP);
      break;

      /* INC (HL) */
    case 0x34:
      DBG("INC (HL)");
      PUT8(HL, GET8(HL) + 1);
      CLK(3);
      break;

      /* DEC (HL) */
    case 0x35:
      DBG("DEC (HL)");
      PUT8(HL, GET8(HL) - 1);
      CLK(3);
      break;

      /* LD (HL), $xx */
    case 0x36:
      DBGF("LD (HL), 0x%02x", GET8(PC));
      LDMEMOUT(HL, GET8(PC++));
      CLK(3);
      break;

      /* SCF */
    case 0x37:
      DBG("SCF");
      SCF();
      break;

      /* JR C, $xx */
    case 0x38:
      DBGF("JR C, %d", (int8_t)GET8(PC));
      JR(FLAG(CARRY));
      break;

      /* ADD HL, SP */
    case 0x39:
      DBG("ADD HL, SP");
      ADD16(HL, SP);
      break;

      /* LDD A, (HL) */
    case 0x3a:
      DBG("LDD A, (HL)");
      LDMEMIN(A, HL--);
      CLK(2);
      break;

      /* DEC SP */
    case 0x3b:
      DBG("DEC SP");
      DEC16(SP);
      CLK(2);
      break;

      /* INC A */
    case 0x3c:
      DBG("INC A");
      INC8(A);
      break;

      /* DEC A */
    case 0x3d:
      DBG("DEC A");
      DEC8(A);
      break;

      /* LD A, $xx */
    case 0x3e:
      DBGF("LD A, 0x%02x", GET8(PC));
      LDMEMIN(A, PC++);
      CLK(2);
      break;

      /* CCF */
    case 0x3f:
      DBG("CCF");
      CCF();
      break;

      /* LD B, B */
    case 0x40:
      DBG("LD B, B");
      CLK(1);
      break;

      /* LD B, C */
    case 0x41:
      DBG("LD B, C");
      LD(B, C);
      CLK(1);
      break;

      /* LD B, D */
    case 0x42:
      DBG("LD B, D");
      LD(B, D);
      CLK(1);
      break;

      /* LD B, E */
    case 0x43:
      DBG("LD B, E");
      LD(B, E);
      CLK(1);
      break;

      /* LD B, H */
    case 0x44:
      DBG("LD B, H");
      LD(B, H);
      CLK(1);
      break;

      /* LD B, L */
    case 0x45:
      DBG("LD B, L");
      LD(B, L);
      CLK(1);
      break;

      /* LD B, (HL) */
    case 0x46:
      DBG("LD B, (HL)");
      LDMEMIN(B, HL);
      CLK(2);
      break;

      /* LD B, A */
    case 0x47:
      DBG("LD B, A");
      LD(B, A);
      CLK(1);
      break;

      /* LD C, B */
    case 0x48:
      DBG("LD C, B");
      LD(C, B);
      CLK(1);
      break;

      /* LD C, C */
    case 0x49:
      DBG("LD C, C");
      CLK(1);
      break;

      /* LD C, D */
    case 0x4a:
      DBG("LD C, D");
      LD(C, D);
      CLK(1);
      break;

      /* LD C, E */
    case 0x4b:
      DBG("LD C, E");
      LD(C, E);
      CLK(1);
      break;

      /* LD C, H */
    case 0x4c:
      DBG("LD C, H");
      LD(C, H);
      CLK(1);
      break;

      /* LD C, L */
    case 0x4d:
      DBG("LD C, L");
      LD(C, L);
      CLK(1);
      break;

      /* LD C, (HL) */
    case 0x4e:
      DBG("LD C, (HL)");
      LDMEMIN(C, HL);
      CLK(2);
      break;

      /* LD C, A */
    case 0x4f:
      DBG("LD C, A");
      LD(C, A);
      CLK(1);
      break;

      /* LD D, B */
    case 0x50:
      DBG("LD D, B");
      LD(D, B);
      CLK(1);
      break;

      /* LD D, C */
    case 0x51:
      DBG("LD D, C");
      LD(D, C);
      CLK(1);
      break;

      /* LD D, D */
    case 0x52:
      DBG("LD D, D");
      CLK(1);
      break;

      /* LD D, E */
    case 0x53:
      DBG("LD D, E");
      LD(D, E);
      CLK(1);
      break;

      /* LD D, H */
    case 0x54:
      DBG("LD D, H");
      LD(D, H);
      CLK(1);
      break;

      /* LD D, L */
    case 0x55:
      DBG("LD D, L");
      LD(D, L);
      CLK(1);
      break;

      /* LD D, (HL) */
    case 0x56:
      DBG("LD D, (HL)");
      LDMEMIN(D, HL);
      CLK(2);
      break;

      /* LD D, A */
    case 0x57:
      DBG("LD D, A");
      LD(D, A);
      CLK(1);
      break;

      /* LD E, B */
    case 0x58:
      DBG("LD E, B");
      LD(E, B);
      CLK(1);
      break;

      /* LD E, C */
    case 0x59:
      DBG("LD E, C");
      LD(E, C);
      CLK(1);
      break;

      /* LD E, D */
    case 0x5a:
      DBG("LD E, D");
      LD(E, D);
      CLK(1);
      break;

      /* LD E, E */
    case 0x5b:
      DBG("LD E, E");
      CLK(1);
      break;

      /* LD E, H */
    case 0x5c:
      DBG("LD E, H");
      LD(E, H);
      CLK(1);
      break;

      /* LD E, L */
    case 0x5d:
      DBG("LD E, L");
      LD(E, L);
      CLK(1);
      break;

      /* LD E, (HL) */
    case 0x5e:
      DBG("LD E, (HL)");
      LDMEMIN(E, HL);
      CLK(2);
      break;

      /* LD E, A */
    case 0x5f:
      DBG("LD E, A");
      LD(E, A);
      CLK(1);
      break;

      /* LD H, B */
    case 0x60:
      DBG("LD H, B");
      LD(H, B);
      CLK(1);
      break;

      /* LD H, C */
    case 0x61:
      DBG("LD H, C");
      LD(H, C);
      CLK(1);
      break;

      /* LD H, D */
    case 0x62:
      DBG("LD H, D");
      LD(H, D);
      CLK(1);
      break;

      /* LD H, E */
    case 0x63:
      DBG("LD H, E");
      LD(H, E);
      CLK(1);
      break;

      /* LD H, H */
    case 0x64:
      DBG("LD H, H");
      CLK(1);
      break;

      /* LD H, L */
    case 0x65:
      DBG("LD H, L");
      LD(H, L);
      CLK(1);
      break;

      /* LD H, (HL) */
    case 0x66:
      DBG("LD H, (HL)");
      LDMEMIN(H, HL);
      CLK(2);
      break;

      /* LD H, A */
    case 0x67:
      DBG("LD H, A");
      LD(H, A);
      CLK(1);
      break;

      /* LD L, B */
    case 0x68:
      DBG("LD L, B");
      LD(L, B);
      CLK(1);
      break;

      /* LD L, C */
    case 0x69:
      DBG("LD L, C");
      LD(L, C);
      CLK(1);
      break;

      /* LD L, D */
    case 0x6a:
      DBG("LD L, D");
      LD(L, D);
      CLK(1);
      break;

      /* LD L, E */
    case 0x6b:
      DBG("LD L, E");
      LD(L, E);
      CLK(1);
      break;

      /* LD L, H */
    case 0x6c:
      DBG("LD L, H");
      LD(L, H);
      CLK(1);
      break;

      /* LD L, L */
    case 0x6d:
      DBG("LD L, L");
      CLK(1);
      break;

      /* LD L, (HL) */
    case 0x6e:
      DBG("LD L, (HL)");
      LDMEMIN(L, HL);
      CLK(2);
      break;

      /* LD L, A */
    case 0x6f:
      DBG("LD L, A");
      LD(L, A);
      CLK(1);
      break;

      /* LD (HL), B */
    case 0x70:
      DBG("LD (HL), B");
      LDMEMOUT(HL, B);
      CLK(2);
      break;

      /* LD (HL), C */
    case 0x71:
      DBG("LD (HL), C");
      LDMEMOUT(HL, C);
      CLK(2);
      break;

      /* LD (HL), D */
    case 0x72:
      DBG("LD (HL), D");
      LDMEMOUT(HL, D);
      CLK(2);
      break;

      /* LD (HL), E */
    case 0x73:
      DBG("LD (HL), E");
      LDMEMOUT(HL, E);
      CLK(2);
      break;

      /* LD (HL), H */
    case 0x74:
      DBG("LD (HL), H");
      LDMEMOUT(HL, H);
      CLK(2);
      break;

      /* LD (HL), L */
    case 0x75:
      DBG("LD (HL), L");
      LDMEMOUT(HL, L);
      CLK(2);
      break;

      /* HALT */
    case 0x76:
      DBG("HALT");
      HALT();
      break;

      /* LD (HL), A */
    case 0x77:
      DBG("LD (HL), A");
      LDMEMOUT(HL, A);
      CLK(2);
      break;

      /* LD A, B */
    case 0x78:
      DBG("LD A, B");
      LD(A, B);
      CLK(1);
      break;

      /* LD A, C */
    case 0x79:
      DBG("LD A, C");
      LD(A, C);
      CLK(1);
      break;

      /* LD A, D */
    case 0x7a:
      DBG("LD A, D");
      LD(A, D);
      CLK(1);
      break;

      /* LD A, E */
    case 0x7b:
      DBG("LD A, E");
      LD(A, E);
      CLK(1);
      break;

      /* LD A, H */
    case 0x7c:
      DBG("LD A, H");
      LD(A, H);
      CLK(1);
      break;

      /* LD A, L */
    case 0x7d:
      DBG("LD A, L");
      LD(A, L);
      CLK(1);
      break;

      /* LD A, (HL) */
    case 0x7e:
      DBG("LD A, (HL)");
      LDMEMIN(A, HL);
      CLK(2);
      break;

      /* LD A, A */
    case 0x7f:
      DBG("LD A, A");
      CLK(1);
      break;

      /* ADD A, B */
    case 0x80:
      DBG("ADD A, B");
      ADD(B);
      CLK(1);
      break;

      /* ADD A, C */
    case 0x81:
      DBG("ADD A, C");
      ADD(C);
      CLK(1);
      break;

      /* ADD A, D */
    case 0x82:
      DBG("ADD A, D");
      ADD(D);
      CLK(1);
      break;

      /* ADD A, E */
    case 0x83:
      DBG("ADD A, E");
      ADD(E);
      CLK(1);
      break;

      /* ADD A, H */
    case 0x84:
      DBG("ADD A, H");
      ADD(H);
      CLK(1);
      break;

      /* ADD A, L */
    case 0x85:
      DBG("ADD A, L");
      ADD(L);
      CLK(1);
      break;

      /* ADD A, (HL) */
    case 0x86:
      DBG("ADD A, (HL)");
      ADD(GET8(HL));
      CLK(2);
      break;

      /* ADD A, A */
    case 0x87:
      DBG("ADD A, A");
      ADD(A);
      CLK(2);
      break;

      /* ADC A, B */
    case 0x88:
      DBG("ADC A, B");
      ADC(B);
      CLK(1);
      break;

      /* ADC A, C */
    case 0x89:
      DBG("ADC A, C");
      ADC(C);
      CLK(1);
      break;

      /* ADC A, D */
    case 0x8a:
      DBG("ADC A, D");
      ADC(D);
      CLK(1);
      break;

      /* ADC A, E */
    case 0x8b:
      DBG("ADC A, E");
      ADC(E);
      CLK(1);
      break;

      /* ADC A, H */
    case 0x8c:
      DBG("ADC A, H");
      ADC(H);
      CLK(1);
      break;

      /* ADC A, L */
    case 0x8d:
      DBG("ADC A, L");
      ADC(L);
      CLK(1);
      break;

      /* ADC A, (HL) */
    case 0x8e:
      DBG("ADC A, (HL)");
      ADC(GET8(HL));
      CLK(2);
      break;

      /* ADC A, A */
    case 0x8f:
      DBG("ADC A, A");
      ADC(A);
      CLK(1);
      break;

      /* SUB B */
    case 0x90:
      DBG("SUB B");
      SUB(B);
      CLK(1);
      break;

      /* SUB C */
    case 0x91:
      DBG("SUB C");
      SUB(C);
      CLK(1);
      break;

      /* SUB D */
    case 0x92:
      DBG("SUB D");
      SUB(D);
      CLK(1);
      break;

      /* SUB E */
    case 0x93:
      DBG("SUB E");
      SUB(E);
      CLK(1);
      break;

      /* SUB H */
    case 0x94:
      DBG("SUB H");
      SUB(H);
      CLK(1);
      break;

      /* SUB L */
    case 0x95:
      DBG("SUB L");
      SUB(L);
      CLK(1);
      break;
      
      /* SUB (HL) */
    case 0x96:
      DBG("SUB (HL)");
      SUB(GET8(HL));
      CLK(2);
      break;

      /* SUB A */
    case 0x97:
      DBG("SUB A");
      /* TODO: Optimize this. */
      SUB(A);
      CLK(1);
      break;

      /* SBC B */
    case 0x98:
      DBG("SBC B");
      SBC(B);
      CLK(1);
      break;

      /* SBC C*/
    case 0x99:
      DBG("SBC C");
      SBC(C);
      CLK(1);
      break;

      /* SBC D */
    case 0x9a:
      DBG("SBC D");
      SBC(D);
      CLK(1);
      break;

      /* SBC E */
    case 0x9b:
      DBG("SBC E");
      SBC(E);
      CLK(1);
      break;

      /* SBC H */
    case 0x9c:
      DBG("SBC H");
      SBC(H);
      CLK(1);
      break;

      /* SBC L */
    case 0x9d:
      DBG("SBC L");
      SBC(L);
      CLK(1);
      break;

      /* SBC (HL) */
    case 0x9e:
      DBG("SBC (HL)");
      SBC(GET8(HL));
      CLK(2);
      break;

      /* SBC A */
    case 0x9f:
      DBG("SBC A");
      SBC(A);
      CLK(1);
      break;

      /* AND B */
    case 0xa0:
      DBG("AND B");
      AND(B);
      CLK(1);
      break;

      /* AND C */
    case 0xa1:
      DBG("AND C");
      AND(C);
      CLK(1);
      break;

      /* AND D */
    case 0xa2:
      DBG("AND D");
      AND(D);
      CLK(1);
      break;

      /* AND E */
    case 0xa3:
      DBG("AND E");
      AND(E);
      CLK(1);
      break;

      /* AND H */
    case 0xa4:
      DBG("AND H");
      AND(H);
      CLK(1);
      break;

      /* AND L */
    case 0xa5:
      DBG("AND L");
      AND(L);
      CLK(1);
      break;

      /* AND (HL) */
    case 0xa6:
      DBG("AND (HL)");
      AND(GET8(HL));
      CLK(2);
      break;

      /* AND A */
    case 0xa7:
      DBG("AND A");
      AND(A);
      CLK(1);
      break;

      /* XOR B */
    case 0xa8:
      DBG("XOR B");
      XOR(B);
      CLK(1);
      break;

      /* XOR C  */
    case 0xa9:
      DBG("XOR C");
      XOR(C);
      CLK(1);
      break;

      /* XOR D */
    case 0xaa:
      DBG("XOR D");
      XOR(D);
      CLK(1);
      break;

      /* XOR E */
    case 0xab:
      DBG("XOR E");
      XOR(E);
      CLK(1);
      break;

      /* XOR H */
    case 0xac:
      DBG("XOR H");
      XOR(H);
      CLK(1);
      break;

      /* XOR L */
    case 0xad:
      DBG("XOR L");
      XOR(L);
      CLK(1);
      break;

      /* XOR (HL) */
    case 0xae:
      DBG("XOR (HL)");
      XOR(GET8(HL));
      CLK(2);
      break;

      /* XOR A */
    case 0xaf:
      DBG("XOR A");
      A = 0;
      F = ZERO;
      CLK(1);
      break;

      /* OR B */
    case 0xb0:
      DBG("OR B");
      OR(B);
      CLK(1);
      break;

      /* OR C */
    case 0xb1:
      DBG("OR C");
      OR(C);
      CLK(1);
      break;

      /* OR D */
    case 0xb2:
      DBG("OR D");
      OR(D);
      CLK(1);
      break;

      /* OR E */
    case 0xb3:
      DBG("OR E");
      OR(E);
      CLK(1);
      break;

      /* OR H */
    case 0xb4:
      DBG("OR H");
      OR(H);
      CLK(1);
      break;

      /* OR L */
    case 0xb5:
      DBG("OR L");
      OR(L);
      CLK(1);
      break;

      /* OR (HL) */
    case 0xb6:
      DBG("OR (HL)");
      OR(GET8(HL));
      CLK(2);
      break;

      /* OR A */
    case 0xb7:
      DBG("OR A");
      /* TODO: Optimize. */
      OR(A);
      CLK(1);
      break;

      /* CP B */
    case 0xb8:
      DBG("CP B");
      CP(B);
      CLK(1);
      break;

      /* CP C */
    case 0xb9:
      DBG("CP C");
      CP(C);
      CLK(1);
      break;

      /* CP D */
    case 0xba:
      DBG("CP D");
      CP(D);
      CLK(1);
      break;

      /* CP E */
    case 0xbb:
      DBG("CP E");
      CP(E);
      CLK(1);
      break;

      /* CP H */
    case 0xbc:
      DBG("CP H");
      CP(H);
      CLK(1);
      break;

      /* CP L */
    case 0xbd:
      DBG("CP L");
      CP(L);
      CLK(1);
      break;

      /* CP (HL) */
    case 0xbe:
      DBG("CP (HL)");
      CP(GET8(HL));
      CLK(2);
      break;

      /* CP A */
    case 0xbf:
      DBG("CP A");
      /* TODO: Optimize. */
      CP(A);
      CLK(1);
      break;

      /* RET NZ */
    case 0xc0:
      DBG("RET NZ");
      RET(!FLAG(ZERO));
      break;

      /* POP BC */
    case 0xc1:
      DBG("POP BC");
      POP(BC);
      break;

      /* JP NZ, $aabb */
    case 0xc2:
      DBGF("JP NZ, 0x%04x", GET16(PC));
      JP(!FLAG(ZERO));
      break;

      /* JP $aabb */
    case 0xc3:
      DBGF("JP 0x%04x", GET16(PC));
      JP(1);
      break;

      /* CALL NZ, $aabb */
    case 0xc4:
      DBGF("CALL NZ, 0x%04x", GET16(PC));
      CALL(!FLAG(ZERO));
      break;

      /* PUSH BC */
    case 0xc5:
      DBG("PUSH BC");
      PUSH(BC);
      CLK(4);
      break;

      /* ADD A, $xx */
    case 0xc6:
      DBGF("ADD A, 0x%02x", GET8(PC));
      ADD(GET8(PC++));
      CLK(2);
      break;

      /* RST $00 */
    case 0xc7:
      DBG("RST $00");
      RST(0x00);
      break;

      /* RET Z */
    case 0xc8:
      DBG("RET Z");
      RET(FLAG(ZERO));
      break;

      /* RET */
    case 0xc9:
      DBG("RET");
      RET(1);
      break;

      /* JP Z, $aabb */
    case 0xca:
      DBGF("JP Z, 0x%04x", GET16(PC));
      JP(FLAG(ZERO));
      break;

      /* CB-extended opcodes. */
    case 0xcb:
      switch(GET8(PC++)) {
          /* RLC B */
        case 0x00:
          DBG("RLC B");
          RLC(B);
          break;
        
          /* RLC C */
        case 0x01:
          DBG("RLC C");
          RLC(C);
          break;
        
          /* RLC D */
        case 0x02:
          DBG("RLC D");
          RLC(D);
          break;
        
          /* RLC E */
        case 0x03:
          DBG("RLC E");
          RLC(E);
          break;
        
          /* RLC H */
        case 0x04:
          DBG("RLC H");
          RLC(H);
          break;
        
          /* RLC L */
        case 0x05:
          DBG("RLC L");
          RLC(L);
          break;
        
          /* RLC (HL) */
        case 0x06:
          DBG("RLC (HL)");
          TODO("RLC (HL)");
          break;
        
          /* RLC A */
        case 0x07:
          DBG("RLC A");
          RLC(A);
          break;
        
          /* RRC B */
        case 0x08:
          DBG("RRC B");
          RRC(B);
          break;
        
          /* RRC C */
        case 0x09:
          DBG("RRC C");
          RRC(C);
          break;
        
          /* RRC D */
        case 0x0a:
          DBG("RRC D");
          RRC(D);
          break;
        
          /* RRC E */
        case 0x0b:
          DBG("RRC E");
          RRC(E);
          break;
        
          /* RRC H */
        case 0x0c:
          DBG("RRC H");
          RRC(H);
          break;
        
          /* RRC L */
        case 0x0d:
          DBG("RRC L");
          RRC(L);
          break;
        
          /* RRC (HL) */
        case 0x0e:
          DBG("RRC (HL)");
          TODO("RRC (HL)");
          break;
        
          /* RRC A */
        case 0x0f:
          DBG("RRC A");
          RRC(A);
          break;
        
          /* RL B */
        case 0x10:
          DBG("RL B");
          RL(B);
          break;
        
          /* RL C */
        case 0x11:
          DBG("RL C");
          RL(C);
          break;
        
          /* RL D */
        case 0x12:
          DBG("RL D");
          RL(D);
          break;
        
          /* RL E */
        case 0x13:
          DBG("RL E");
          RL(E);
          break;
        
          /* RL H */
        case 0x14:
          DBG("RL H");
          RL(H);
          break;
        
          /* RL L */
        case 0x15:
          DBG("RL L");
          RL(L);
          break;
        
          /* RL (HL) */
        case 0x16:
          DBG("RL (HL)");
          TODO("RL (HL)");
          break;
        
          /* RL A */
        case 0x17:
          DBG("RL A");
          RL(A);
          break;
        
          /* RR B */
        case 0x18:
          DBG("RR B");
          RR(B);
          break;
        
          /* RR C */
        case 0x19:
          DBG("RR C");
          RR(C);
          break;
        
          /* RR D */
        case 0x1a:
          DBG("RR D");
          RR(D);
          break;
        
          /* RR E */
        case 0x1b:
          DBG("RR E");
          RR(E);
          break;
        
          /* RR H */
        case 0x1c:
          DBG("RR H");
          RR(H);
          break;
        
          /* RR L */
        case 0x1d:
          DBG("RR L");
          RR(L);
          break;
        
          /* RR (HL) */
        case 0x1e:
          DBG("RR (HL)");
          TODO("RR (HL)");
          break;
        
          /* RR A */
        case 0x1f:
          DBG("RR A");
          RR(A);
          break;
        
          /* SLA B */
        case 0x20:
          DBG("SLA B");
          SLA(B);
          break;
        
          /* SLA C */
        case 0x21:
          DBG("SLA C");
          SLA(C);
          break;
        
          /* SLA D */
        case 0x22:
          DBG("SLA D");
          SLA(D);
          break;
        
          /* SLA E */
        case 0x23:
          DBG("SLA E");
          SLA(E);
          break;
        
          /* SLA H */
        case 0x24:
          DBG("SLA H");
          SLA(H);
          break;
        
          /* SLA L */
        case 0x25:
          DBG("SLA L");
          SLA(L);
          break;
        
          /* SLA (HL) */
        case 0x26:
          DBG("SLA (HL)");
          TODO("SLA (HL)");
          break;
        
          /* SLA A */
        case 0x27:
          DBG("SLA A");
          SLA(A);
          break;
        
          /* SRA B */
        case 0x28:
          DBG("SRA B");
          SRA(B);
          break;
        
          /* SRA C */
        case 0x29:
          DBG("SRA C");
          SRA(C);
          break;
        
          /* SRA D */
        case 0x2a:
          DBG("SRA D");
          SRA(D);
          break;
        
          /* SRA E */
        case 0x2b:
          DBG("SRA E");
          SRA(E);
          break;
        
          /* SRA H */
        case 0x2c:
          DBG("SRA H");
          SRA(H);
          break;
        
          /* SRA L */
        case 0x2d:
          DBG("SRA L");
          SRA(L);
          break;
        
          /* SRA (HL) */
        case 0x2e:
          DBG("SRA (HL)");
          TODO("SRA (HL)");
          break;
        
          /* SRA A */
        case 0x2f:
          DBG("SRA A");
          SRA(A);
          break;
        
          /* SWAP A */
        case 0x37:
          DBG("SWAP A");
          SWAP(A);
          break;
        
          /* SRL B */
        case 0x38:
          DBG("SRL B");
          SRL(B);
          break;
        
          /* SRL C */
        case 0x39:
          DBG("SRL C");
          SRL(C);
          break;
        
          /* SRL D */
        case 0x3a:
          DBG("SRL D");
          SRL(D);
          break;
        
          /* SRL E */
        case 0x3b:
          DBG("SRL E");
          SRL(E);
          break;
        
          /* SRL H */
        case 0x3c:
          DBG("SRL H");
          SRL(H);
          break;
        
          /* SRL L */
        case 0x3d:
          DBG("SRL L");
          SRL(L);
          break;
        
          /* SRL (HL) */
        case 0x3e:
          DBG("SRL (HL)");
          TODO("SRL (HL)");
          break;
        
          /* SRL A */
        case 0x3f:
          DBG("SRL A");
          SRL(A);
          break;

          /* BIT 0, B */
        case 0x40:
          DBG("BIT 0, B");
          BIT(0, B);
          break;

          /* BIT 0, C */
        case 0x41:
          DBG("BIT 0, C");
          BIT(0, C);
          break;

          /* BIT 0, D */
        case 0x42:
          DBG("BIT 0, D");
          BIT(0, D);
          break;

          /* BIT 0, E */
        case 0x43:
          DBG("BIT 0, E");
          BIT(0, E);
          break;

          /* BIT 0, H */
        case 0x44:
          DBG("BIT 0, H");
          BIT(0, H);
          break;

          /* BIT 0, L */
        case 0x45:
          DBG("BIT 0, L");
          BIT(0, L);
          break;

          /* BIT 0, (HL) */
        case 0x46:
          DBG("BIT 0, (HL)");
          TODO("B");
          break;

          /* BIT 0, A */
        case 0x47:
          DBG("BIT 0, A");
          BIT(0, A);
          break;

          /* BIT 1, B */
        case 0x48:
          DBG("BIT 1, B");
          BIT(1, B);
          break;

          /* BIT 1, C */
        case 0x49:
          DBG("BIT 1, C");
          BIT(1, C);
          break;

          /* BIT 1, D */
        case 0x4a:
          DBG("BIT 1, D");
          BIT(1, D);
          break;

          /* BIT 1, E */
        case 0x4b:
          DBG("BIT 1, E");
          BIT(1, E);
          break;

          /* BIT 1, H */
        case 0x4c:
          DBG("BIT 1, H");
          BIT(1, H);
          break;

          /* BIT 1, L */
        case 0x4d:
          DBG("BIT 1, L");
          BIT(1, L);
          break;

          /* BIT 1, (HL) */
        case 0x4e:
          DBG("BIT 1, (HL)");
          TODO("B");
          break;

          /* BIT 1, A */
        case 0x4f:
          DBG("BIT 1, A");
          BIT(1, A);
          break;

          /* BIT 2, B */
        case 0x50:
          DBG("BIT 2, B");
          BIT(2, B);
          break;

          /* BIT 2, C */
        case 0x51:
          DBG("BIT 2, C");
          BIT(2, C);
          break;

          /* BIT 2, D */
        case 0x52:
          DBG("BIT 2, D");
          BIT(2, D);
          break;

          /* BIT 2, E */
        case 0x53:
          DBG("BIT 2, E");
          BIT(2, E);
          break;

          /* BIT 2, H */
        case 0x54:
          DBG("BIT 2, H");
          BIT(2, H);
          break;

          /* BIT 2, L */
        case 0x55:
          DBG("BIT 2, L");
          BIT(2, L);
          break;

          /* BIT 2, (HL) */
        case 0x56:
          DBG("BIT 2, (HL)");
          TODO("B");
          break;

          /* BIT 2, A */
        case 0x57:
          DBG("BIT 2, A");
          BIT(2, A);
          break;

          /* BIT 3, B */
        case 0x58:
          DBG("BIT 3, B");
          BIT(3, B);
          break;

          /* BIT 3, C */
        case 0x59:
          DBG("BIT 3, C");
          BIT(3, C);
          break;

          /* BIT 3, D */
        case 0x5a:
          DBG("BIT 3, D");
          BIT(3, D);
          break;

          /* BIT 3, E */
        case 0x5b:
          DBG("BIT 3, E");
          BIT(3, E);
          break;

          /* BIT 3, H */
        case 0x5c:
          DBG("BIT 3, H");
          BIT(3, H);
          break;

          /* BIT 3, L */
        case 0x5d:
          DBG("BIT 3, L");
          BIT(3, L);
          break;

          /* BIT 3, (HL) */
        case 0x5e:
          DBG("BIT 3, (HL)");
          TODO("B");
          break;

          /* BIT 3, A */
        case 0x5f:
          DBG("BIT 3, A");
          BIT(3, A);
          break;

          /* BIT 4, B */
        case 0x60:
          DBG("BIT 4, B");
          BIT(4, B);
          break;

          /* BIT 4, C */
        case 0x61:
          DBG("BIT 4, C");
          BIT(4, C);
          break;

          /* BIT 4, D */
        case 0x62:
          DBG("BIT 4, D");
          BIT(4, D);
          break;

          /* BIT 4, E */
        case 0x63:
          DBG("BIT 4, E");
          BIT(4, E);
          break;

          /* BIT 4, H */
        case 0x64:
          DBG("BIT 4, H");
          BIT(4, H);
          break;

          /* BIT 4, L */
        case 0x65:
          DBG("BIT 4, L");
          BIT(4, L);
          break;

          /* BIT 4, (HL) */
        case 0x66:
          DBG("BIT 4, (HL)");
          TODO("B");
          break;

          /* BIT 4, A */
        case 0x67:
          DBG("BIT 4, A");
          BIT(4, A);
          break;

          /* BIT 5, B */
        case 0x68:
          DBG("BIT 5, B");
          BIT(5, B);
          break;

          /* BIT 5, C */
        case 0x69:
          DBG("BIT 5, C");
          BIT(5, C);
          break;

          /* BIT 5, D */
        case 0x6a:
          DBG("BIT 5, D");
          BIT(5, D);
          break;

          /* BIT 5, E */
        case 0x6b:
          DBG("BIT 5, E");
          BIT(5, E);
          break;

          /* BIT 5, H */
        case 0x6c:
          DBG("BIT 5, H");
          BIT(5, H);
          break;

          /* BIT 5, L */
        case 0x6d:
          DBG("BIT 5, L");
          BIT(5, L);
          break;

          /* BIT 5, (HL) */
        case 0x6e:
          DBG("BIT 5, (HL)");
          TODO("B");
          break;

          /* BIT 5, A */
        case 0x6f:
          DBG("BIT 5, A");
          BIT(5, A);
          break;

          /* BIT 6, B */
        case 0x70:
          DBG("BIT 6, B");
          BIT(6, B);
          break;

          /* BIT 6, C */
        case 0x71:
          DBG("BIT 6, C");
          BIT(6, C);
          break;

          /* BIT 6, D */
        case 0x72:
          DBG("BIT 6, D");
          BIT(6, D);
          break;

          /* BIT 6, E */
        case 0x73:
          DBG("BIT 6, E");
          BIT(6, E);
          break;

          /* BIT 6, H */
        case 0x74:
          DBG("BIT 6, H");
          BIT(6, H);
          break;

          /* BIT 6, L */
        case 0x75:
          DBG("BIT 6, L");
          BIT(6, L);
          break;

          /* BIT 6, (HL) */
        case 0x76:
          DBG("BIT 6, (HL)");
          TODO("B");
          break;

          /* BIT 6, A */
        case 0x77:
          DBG("BIT 6, A");
          BIT(6, A);
          break;

          /* BIT 7, B */
        case 0x78:
          DBG("BIT 7, B");
          BIT(7, B);
          break;

          /* BIT 7, C */
        case 0x79:
          DBG("BIT 7, C");
          BIT(7, C);
          break;

          /* BIT 7, D */
        case 0x7a:
          DBG("BIT 7, D");
          BIT(7, D);
          break;

          /* BIT 7, E */
        case 0x7b:
          DBG("BIT 7, E");
          BIT(7, E);
          break;

          /* BIT 7, H */
        case 0x7c:
          DBG("BIT 7, H");
          BIT(7, H);
          break;

          /* BIT 7, L */
        case 0x7d:
          DBG("BIT 7, L");
          BIT(7, L);
          break;

          /* BIT 7, (HL) */
        case 0x7e:
          DBG("BIT 7, (HL)");
          TODO("B");
          break;

          /* BIT 7, A */
        case 0x7f:
          DBG("BIT 7, A");
          BIT(7, A);
          break;

          /* RES 0, B */
        case 0x80:
          DBG("RES 0, B");
          RES(0, B);
          break;

          /* RES 0, C */
        case 0x81:
          DBG("RES 0, C");
          RES(0, C);
          break;

          /* RES 0, D */
        case 0x82:
          DBG("RES 0, D");
          RES(0, D);
          break;

          /* RES 0, E */
        case 0x83:
          DBG("RES 0, E");
          RES(0, E);
          break;

          /* RES 0, H */
        case 0x84:
          DBG("RES 0, H");
          RES(0, H);
          break;

          /* RES 0, L */
        case 0x85:
          DBG("RES 0, L");
          RES(0, L);
          break;

          /* RES 0, (HL) */
        case 0x86:
          DBG("RES 0, (HL)");
          TODO("B");
          break;

          /* RES 0, A */
        case 0x87:
          DBG("RES 0, A");
          RES(0, A);
          break;

          /* RES 1, B */
        case 0x88:
          DBG("RES 1, B");
          RES(1, B);
          break;

          /* RES 1, C */
        case 0x89:
          DBG("RES 1, C");
          RES(1, C);
          break;

          /* RES 1, D */
        case 0x8a:
          DBG("RES 1, D");
          RES(1, D);
          break;

          /* RES 1, E */
        case 0x8b:
          DBG("RES 1, E");
          RES(1, E);
          break;

          /* RES 1, H */
        case 0x8c:
          DBG("RES 1, H");
          RES(1, H);
          break;

          /* RES 1, L */
        case 0x8d:
          DBG("RES 1, L");
          RES(1, L);
          break;

          /* RES 1, (HL) */
        case 0x8e:
          DBG("RES 1, (HL)");
          TODO("B");
          break;

          /* RES 1, A */
        case 0x8f:
          DBG("RES 1, A");
          RES(1, A);
          break;

          /* RES 2, B */
        case 0x90:
          DBG("RES 2, B");
          RES(2, B);
          break;

          /* RES 2, C */
        case 0x91:
          DBG("RES 2, C");
          RES(2, C);
          break;

          /* RES 2, D */
        case 0x92:
          DBG("RES 2, D");
          RES(2, D);
          break;

          /* RES 2, E */
        case 0x93:
          DBG("RES 2, E");
          RES(2, E);
          break;

          /* RES 2, H */
        case 0x94:
          DBG("RES 2, H");
          RES(2, H);
          break;

          /* RES 2, L */
        case 0x95:
          DBG("RES 2, L");
          RES(2, L);
          break;

          /* RES 2, (HL) */
        case 0x96:
          DBG("RES 2, (HL)");
          TODO("B");
          break;

          /* RES 2, A */
        case 0x97:
          DBG("RES 2, A");
          RES(2, A);
          break;

          /* RES 3, B */
        case 0x98:
          DBG("RES 3, B");
          RES(3, B);
          break;

          /* RES 3, C */
        case 0x99:
          DBG("RES 3, C");
          RES(3, C);
          break;

          /* RES 3, D */
        case 0x9a:
          DBG("RES 3, D");
          RES(3, D);
          break;

          /* RES 3, E */
        case 0x9b:
          DBG("RES 3, E");
          RES(3, E);
          break;

          /* RES 3, H */
        case 0x9c:
          DBG("RES 3, H");
          RES(3, H);
          break;

          /* RES 3, L */
        case 0x9d:
          DBG("RES 3, L");
          RES(3, L);
          break;

          /* RES 3, (HL) */
        case 0x9e:
          DBG("RES 3, (HL)");
          TODO("B");
          break;

          /* RES 3, A */
        case 0x9f:
          DBG("RES 3, A");
          RES(3, A);
          break;

          /* RES 4, B */
        case 0xa0:
          DBG("RES 4, B");
          RES(4, B);
          break;

          /* RES 4, C */
        case 0xa1:
          DBG("RES 4, C");
          RES(4, C);
          break;

          /* RES 4, D */
        case 0xa2:
          DBG("RES 4, D");
          RES(4, D);
          break;

          /* RES 4, E */
        case 0xa3:
          DBG("RES 4, E");
          RES(4, E);
          break;

          /* RES 4, H */
        case 0xa4:
          DBG("RES 4, H");
          RES(4, H);
          break;

          /* RES 4, L */
        case 0xa5:
          DBG("RES 4, L");
          RES(4, L);
          break;

          /* RES 4, (HL) */
        case 0xa6:
          DBG("RES 4, (HL)");
          TODO("B");
          break;

          /* RES 4, A */
        case 0xa7:
          DBG("RES 4, A");
          RES(4, A);
          break;

          /* RES 5, B */
        case 0xa8:
          DBG("RES 5, B");
          RES(5, B);
          break;

          /* RES 5, C */
        case 0xa9:
          DBG("RES 5, C");
          RES(5, C);
          break;

          /* RES 5, D */
        case 0xaa:
          DBG("RES 5, D");
          RES(5, D);
          break;

          /* RES 5, E */
        case 0xab:
          DBG("RES 5, E");
          RES(5, E);
          break;

          /* RES 5, H */
        case 0xac:
          DBG("RES 5, H");
          RES(5, H);
          break;

          /* RES 5, L */
        case 0xad:
          DBG("RES 5, L");
          RES(5, L);
          break;

          /* RES 5, (HL) */
        case 0xae:
          DBG("RES 5, (HL)");
          TODO("B");
          break;

          /* RES 5, A */
        case 0xaf:
          DBG("RES 5, A");
          RES(5, A);
          break;

          /* RES 6, B */
        case 0xb0:
          DBG("RES 6, B");
          RES(6, B);
          break;

          /* RES 6, C */
        case 0xb1:
          DBG("RES 6, C");
          RES(6, C);
          break;

          /* RES 6, D */
        case 0xb2:
          DBG("RES 6, D");
          RES(6, D);
          break;

          /* RES 6, E */
        case 0xb3:
          DBG("RES 6, E");
          RES(6, E);
          break;

          /* RES 6, H */
        case 0xb4:
          DBG("RES 6, H");
          RES(6, H);
          break;

          /* RES 6, L */
        case 0xb5:
          DBG("RES 6, L");
          RES(6, L);
          break;

          /* RES 6, (HL) */
        case 0xb6:
          DBG("RES 6, (HL)");
          TODO("B");
          break;

          /* RES 6, A */
        case 0xb7:
          DBG("RES 6, A");
          RES(6, A);
          break;

          /* RES 7, B */
        case 0xb8:
          DBG("RES 7, B");
          RES(7, B);
          break;

          /* RES 7, C */
        case 0xb9:
          DBG("RES 7, C");
          RES(7, C);
          break;

          /* RES 7, D */
        case 0xba:
          DBG("RES 7, D");
          RES(7, D);
          break;

          /* RES 7, E */
        case 0xbb:
          DBG("RES 7, E");
          RES(7, E);
          break;

          /* RES 7, H */
        case 0xbc:
          DBG("RES 7, H");
          RES(7, H);
          break;

          /* RES 7, L */
        case 0xbd:
          DBG("RES 7, L");
          RES(7, L);
          break;

          /* RES 7, (HL) */
        case 0xbe:
          DBG("RES 7, (HL)");
          TODO("B");
          break;

          /* RES 7, A */
        case 0xbf:
          DBG("RES 7, A");
          RES(7, A);
          break;

          /* SET 0, B */
        case 0xc0:
          DBG("SET 0, B");
          SET(0, B);
          break;

          /* SET 0, C */
        case 0xc1:
          DBG("SET 0, C");
          SET(0, C);
          break;

          /* SET 0, D */
        case 0xc2:
          DBG("SET 0, D");
          SET(0, D);
          break;

          /* SET 0, E */
        case 0xc3:
          DBG("SET 0, E");
          SET(0, E);
          break;

          /* SET 0, H */
        case 0xc4:
          DBG("SET 0, H");
          SET(0, H);
          break;

          /* SET 0, L */
        case 0xc5:
          DBG("SET 0, L");
          SET(0, L);
          break;

          /* SET 0, (HL) */
        case 0xc6:
          DBG("SET 0, (HL)");
          TODO("B");
          break;

          /* SET 0, A */
        case 0xc7:
          DBG("SET 0, A");
          SET(0, A);
          break;

          /* SET 1, B */
        case 0xc8:
          DBG("SET 1, B");
          SET(1, B);
          break;

          /* SET 1, C */
        case 0xc9:
          DBG("SET 1, C");
          SET(1, C);
          break;

          /* SET 1, D */
        case 0xca:
          DBG("SET 1, D");
          SET(1, D);
          break;

          /* SET 1, E */
        case 0xcb:
          DBG("SET 1, E");
          SET(1, E);
          break;

          /* SET 1, H */
        case 0xcc:
          DBG("SET 1, H");
          SET(1, H);
          break;

          /* SET 1, L */
        case 0xcd:
          DBG("SET 1, L");
          SET(1, L);
          break;

          /* SET 1, (HL) */
        case 0xce:
          DBG("SET 1, (HL)");
          TODO("B");
          break;

          /* SET 1, A */
        case 0xcf:
          DBG("SET 1, A");
          SET(1, A);
          break;

          /* SET 2, B */
        case 0xd0:
          DBG("SET 2, B");
          SET(2, B);
          break;

          /* SET 2, C */
        case 0xd1:
          DBG("SET 2, C");
          SET(2, C);
          break;

          /* SET 2, D */
        case 0xd2:
          DBG("SET 2, D");
          SET(2, D);
          break;

          /* SET 2, E */
        case 0xd3:
          DBG("SET 2, E");
          SET(2, E);
          break;

          /* SET 2, H */
        case 0xd4:
          DBG("SET 2, H");
          SET(2, H);
          break;

          /* SET 2, L */
        case 0xd5:
          DBG("SET 2, L");
          SET(2, L);
          break;

          /* SET 2, (HL) */
        case 0xd6:
          DBG("SET 2, (HL)");
          TODO("B");
          break;

          /* SET 2, A */
        case 0xd7:
          DBG("SET 2, A");
          SET(2, A);
          break;

          /* SET 3, B */
        case 0xd8:
          DBG("SET 3, B");
          SET(3, B);
          break;

          /* SET 3, C */
        case 0xd9:
          DBG("SET 3, C");
          SET(3, C);
          break;

          /* SET 3, D */
        case 0xda:
          DBG("SET 3, D");
          SET(3, D);
          break;

          /* SET 3, E */
        case 0xdb:
          DBG("SET 3, E");
          SET(3, E);
          break;

          /* SET 3, H */
        case 0xdc:
          DBG("SET 3, H");
          SET(3, H);
          break;

          /* SET 3, L */
        case 0xdd:
          DBG("SET 3, L");
          SET(3, L);
          break;

          /* SET 3, (HL) */
        case 0xde:
          DBG("SET 3, (HL)");
          TODO("B");
          break;

          /* SET 3, A */
        case 0xdf:
          DBG("SET 3, A");
          SET(3, A);
          break;

          /* SET 4, B */
        case 0xe0:
          DBG("SET 4, B");
          SET(4, B);
          break;

          /* SET 4, C */
        case 0xe1:
          DBG("SET 4, C");
          SET(4, C);
          break;

          /* SET 4, D */
        case 0xe2:
          DBG("SET 4, D");
          SET(4, D);
          break;

          /* SET 4, E */
        case 0xe3:
          DBG("SET 4, E");
          SET(4, E);
          break;

          /* SET 4, H */
        case 0xe4:
          DBG("SET 4, H");
          SET(4, H);
          break;

          /* SET 4, L */
        case 0xe5:
          DBG("SET 4, L");
          SET(4, L);
          break;

          /* SET 4, (HL) */
        case 0xe6:
          DBG("SET 4, (HL)");
          TODO("B");
          break;

          /* SET 4, A */
        case 0xe7:
          DBG("SET 4, A");
          SET(4, A);
          break;

          /* SET 5, B */
        case 0xe8:
          DBG("SET 5, B");
          SET(5, B);
          break;

          /* SET 5, C */
        case 0xe9:
          DBG("SET 5, C");
          SET(5, C);
          break;

          /* SET 5, D */
        case 0xea:
          DBG("SET 5, D");
          SET(5, D);
          break;

          /* SET 5, E */
        case 0xeb:
          DBG("SET 5, E");
          SET(5, E);
          break;

          /* SET 5, H */
        case 0xec:
          DBG("SET 5, H");
          SET(5, H);
          break;

          /* SET 5, L */
        case 0xed:
          DBG("SET 5, L");
          SET(5, L);
          break;

          /* SET 5, (HL) */
        case 0xee:
          DBG("SET 5, (HL)");
          TODO("B");
          break;

          /* SET 5, A */
        case 0xef:
          DBG("SET 5, A");
          SET(5, A);
          break;

          /* SET 6, B */
        case 0xf0:
          DBG("SET 6, B");
          SET(6, B);
          break;

          /* SET 6, C */
        case 0xf1:
          DBG("SET 6, C");
          SET(6, C);
          break;

          /* SET 6, D */
        case 0xf2:
          DBG("SET 6, D");
          SET(6, D);
          break;

          /* SET 6, E */
        case 0xf3:
          DBG("SET 6, E");
          SET(6, E);
          break;

          /* SET 6, H */
        case 0xf4:
          DBG("SET 6, H");
          SET(6, H);
          break;

          /* SET 6, L */
        case 0xf5:
          DBG("SET 6, L");
          SET(6, L);
          break;

          /* SET 6, (HL) */
        case 0xf6:
          DBG("SET 6, (HL)");
          TODO("B");
          break;

          /* SET 6, A */
        case 0xf7:
          DBG("SET 6, A");
          SET(6, A);
          break;

          /* SET 7, B */
        case 0xf8:
          DBG("SET 7, B");
          SET(7, B);
          break;

          /* SET 7, C */
        case 0xf9:
          DBG("SET 7, C");
          SET(7, C);
          break;

          /* SET 7, D */
        case 0xfa:
          DBG("SET 7, D");
          SET(7, D);
          break;

          /* SET 7, E */
        case 0xfb:
          DBG("SET 7, E");
          SET(7, E);
          break;

          /* SET 7, H */
        case 0xfc:
          DBG("SET 7, H");
          SET(7, H);
          break;

          /* SET 7, L */
        case 0xfd:
          DBG("SET 7, L");
          SET(7, L);
          break;

          /* SET 7, (HL) */
        case 0xfe:
          DBG("SET 7, (HL)");
          TODO("B");
          break;

          /* SET 7, A */
        case 0xff:
          DBG("SET 7, A");
          SET(7, A);
          break;
      }
      break;

      /* CALL Z, $aabb */
    case 0xcc:
      DBGF("CALL Z, 0x%04x", GET16(PC));
      CALL(FLAG(ZERO));
      break;

      /* CALL $aabb */
    case 0xcd:
      DBGF("CALL 0x%04x", GET16(PC));
      CALL(1);
      break;

      /* ADC A, $xx */
    case 0xce:
      DBGF("ADC A, 0x%02x", GET8(PC));
      ADC(GET8(PC++));
      CLK(2);
      break;

      /* RST $08 */
    case 0xcf:
      DBG("RST $08");
      RST(0x08);
      break;

      /* RET NC */
    case 0xd0:
      DBG("RET NC");
      RET(!FLAG(CARRY));
      break;

      /* POP DE */
    case 0xd1:
      DBG("POP DE");
      POP(DE);
      break;

      /* JP NC, $aabb */
    case 0xd2:
      DBGF("JP NC, 0x%04x", GET16(PC));
      JP(!FLAG(CARRY));
      break;

      /* OUT (n), A -- Unsupported */
    case 0xd3:
      DBG("Unsupported opcode");
      STOP();
      break;

      /* CALL NC, $aabb */
    case 0xd4:
      DBGF("CALL NC, 0x%04x", GET16(PC));
      CALL(!FLAG(CARRY));
      break;

      /* PUSH DE */
    case 0xd5:
      DBG("PUSH DE");
      PUSH(DE);
      break;

      /* SUB $xx */
    case 0xd6:
      DBGF("SUB %02x", GET8(PC));
      SUB(GET8(PC++));
      CLK(2);
      break;

      /* RST $10 */
    case 0xd7:
      DBG("RST $10");
      RST(0x10);
      break;

      /* RET C */
    case 0xd8:
      DBG("RET C");
      RET(FLAG(CARRY));
      break;

      /* RETI */
    case 0xd9:
      DBG("RETI");
      RET(1);
      IME = 1;
      break;
      
      /* JP C, $aabb */
    case 0xda:
      DBGF("JP C, 0x%04x", GET16(PC));
      JP(FLAG(CARRY));
      break;

      /* IN A, (n) - Unsupported */
    case 0xdb:
      DBG("Unsupported opcode");
      STOP();
      break;

      /* CALL C, $aabb */
    case 0xdc:
      DBGF("CALL C, 0x%04x", GET16(PC));
      CALL(FLAG(CARRY));
      break;

      /* Prefix - Unsupported */
    case 0xdd:
      DBG("Unsupported opcode");
      STOP();
      break;

      /* SBC A, $xx */
    case 0xde:
      DBGF("SBC A, 0x%02x", GET8(PC));
      SBC(GET8(PC++));
      CLK(2);
      break;

      /* RST $18 */
    case 0xdf:
      DBG("RST $18");
      RST(0x18);
      break;

      /* LD ($ff00+n), A */
    case 0xe0:
      T3 = 0xff00+GET8(PC++);
      DBGF("LD (0x%04x), A", T3);
      LDMEMOUT(T3, A);
      CLK(3);
      break;

      /* POP HL */
    case 0xe1:
      DBG("POP HL");
      POP(HL);
      break;

      /* LD ($ff00+C), A */
    case 0xe2:
      DBGF("LD (0x%04x), A", 0xff00+C);
      LDMEMOUT(0xff00+C, A);
      CLK(2);
      break;

      /* EX (SP), HL - Unsupported */
    case 0xe3:
      DBG("Unsupported opcode");
      STOP();
      break;

      /* CALL P0, nn - Unsupported */
    case 0xe4:
      DBG("Unsupported opcode");
      STOP();
      break;

      /* PUSH HL */
    case 0xe5:
      DBG("PUSH HL");
      PUSH(HL);
      break;

      /* AND $xx */
    case 0xe6:
      DBGF("AND 0x%02x", GET8(PC));
      AND(GET8(PC++));
      CLK(2);
      break;

      /* RST $20 */
    case 0xe7:
      DBG("RST $20");
      RST(0x20);
      break;

      /* ADD SP, $xx */
    case 0xe8:
      DBGF("ADD SP, %d", (int8_t)GET8(PC));
      /* TODO: Add half-carry support. */
      F = 0;
      T4 = (SP + (int8_t)GET8(PC));
      if(T4 > 0xffff) SETFLAG(CARRY);
      if(T4 < 0)      SETFLAG(CARRY);
      SP += (int8_t)GET8(PC++);
      CLK(4);
      break;
      
      /* JP (HL) */
    case 0xe9:
      DBG("JP (HL)");
      PC = HL;
      CLK(1);
      break;

      /* LD ($aabb), A */
    case 0xea:
      DBGF("LD (0x%04x), A", GET16(PC));
      LDMEMOUT(GET16(PC), A);
      PC += 2;
      CLK(4);
      break;

      /* EX DE, HL - Unsupported */
    case 0xeb:
      DBG("Unsupported opcode");
      STOP();
      break;

      /* CALL PE, nn - Unsupported */
    case 0xec:
      DBG("Unsupported opcode");
      STOP();
      break;

      /* Prefix - unsupported */
    case 0xed:
      DBG("Unsupported opcode");
      STOP();
      break;

      /* XOR $xx */
    case 0xee:
      DBGF("XOR 0x%02x", GET8(PC));
      XOR(GET8(PC++));
      CLK(2);
      break;

      /* RST $28 */
    case 0xef:
      DBG("RST $28");
      RST(0x28);
      break;

      /* LD A, ($ff00+n) */
    case 0xf0:
      DBGF("LD A, (0x%04x)", 0xff00+GET8(PC));
      LDMEMIN(A, 0xff00+GET8(PC++));
      CLK(4);
      break;

      /* POP AF */
    case 0xf1:
      DBG("POP AF");
      POP(AF);
      break;

      /* LD A, ($ff00+C) */
    case 0xf2:
      DBGF("LD A, (0x%04x)", 0xff00 + C);
      LDMEMIN(A, 0xff00+C);
      CLK(2);
      break;

      /* DI */
    case 0xf3:
      DBG("DI");
      DI();
      break;

      /* CALL P, nn - Unsupported */
    case 0xf4:
      DBG("Unsupported opcode");
      STOP();
      break;

      /* PUSH AF */
    case 0xf5:
      DBG("PUSH AF");
      PUSH(AF);
      break;

      /* OR $xx */
    case 0xf6:
      DBGF("OR 0x%02x", GET8(PC));
      OR(GET8(PC++));
      CLK(2);
      break;

      /* RST $30 */
    case 0xf7:
      DBG("RST $30");
      RST(0x30);
      break;

      /* LD HL, (SP + e8) */
    case 0xf8:
      DBGF("LD HL, SP + %d", (int8_t)GET8(PC));
      LD(HL, SP + (int8_t)GET8(PC++));
      CLK(3);
      break;

      /* LD SP, HL */
    case 0xf9:
      DBG("LD SP, HL");
      LD(SP, HL);
      CLK(2);
      break;

      /* LD A, ($aabb) */
    case 0xfa:
      DBGF("LD A, 0x%04x", GET16(PC));
      LDMEMIN(A, GET16(PC));
      CLK(4);
      PC += 2;
      break;

      /* EI */
    case 0xfb:
      DBG("EI");
      EI();
      break;

      /* CALL M, nn - Unsupported */
    case 0xfc:
      DBG("Unsupported opcode");
      STOP();
      break;

      /* Prefix - Unsupported */
    case 0xfd:
      DBG("Unsupported opcode");
      STOP();
      break;

      /* CP $xx */
    case 0xfe:
      DBGF("CP 0x%02x", GET8(PC));
      CP(GET8(PC++));
      CLK(2);
      break;

      /* RST $38 */
    case 0xff:
      DBG("RST $38");
      RST(0x38);
      break;
    }
  }

  //  sstep = 1;
  /*  if(PC == 0x27d7) sstep = 1;
  
    if(sstep) {
    DUMPREGS();
    POLL();
    }*/
  
  return actual;
}
