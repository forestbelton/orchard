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

#include <nds.h>
#include <stddef.h>
#include <stdio.h>

#include "gb.h"
#include "z80.h"

#define MAX_CYCLES    70221
#define BITVAL(a, n)  (!!((a) & BIT(n)))
#define BIT(n)        (1 << (n))
#define TESTBIT(X, B) ((X) & (1 << (B)))

#define MODE2_BOUND (456-80)
#define MODE3_BOUND (MODE2_BOUND-172)

typedef enum {
  intr_vblank = (1 << 0),
  intr_lcd    = (1 << 1),
  intr_timer  = (1 << 2),
  intr_pad    = (1 << 4)
} intr_t;

uint8_t bank_count       = 0;
uint8_t (*banks)[0x4000] = NULL;
unsigned int cur_bank    = 0;
FILE *logfile;

static int timer_counter;
static int scanline = 0;

static void gb_draw_scanline(void);
static void gb_service    (intr_t i);
static void gb_check_intrs(void);
static void gb_update     (uint8_t cycles);
static void gb_set_lcd    (void);
static uint16_t gb_get_color(uint8_t num, uint8_t palette);
static void gb_render_tile(uint8_t x, uint8_t y, uint8_t tile[2]);

void gb_init(void) {
  /* Initialize registers. */
  AF = 0x01b0;
  BC = 0x0013;
  DE = 0x00d8;
  HL = 0x014d;
  SP = 0xfffe;
  PC = 0x0100;

  /* Initialize memory-mapped registers. */
  TIMA = 0x00;
  TMA  = 0x00;
  TAC  = 0x00;
  NR10 = 0x80;
  NR11 = 0xbf;
  NR12 = 0xf3;
  NR14 = 0xbf;
  NR21 = 0x3f;
  NR22 = 0x00;
  NR24 = 0xbf;
  NR30 = 0x7f;
  NR31 = 0xff;
  NR32 = 0x9f;
  NR33 = 0xbf;
  NR41 = 0xff;
  NR42 = 0x00;
  NR43 = 0x00;
  NR40 = 0xbf;
  NR50 = 0x77;
  NR51 = 0xf3;
  NR52 = 0xf1; /* 0xf1 for GB, 0xf0 for SGB */
  LCDC = 0x91;
  SCY  = 0x00;
  SCX  = 0x43; /* 0x43 */
  LYC  = 0x00;
  BGP  = 0xfc;
  OBP0 = 0xff;
  OBP1 = 0xff;
  WY   = 0x00;
  WX   = 0x00;
  IE   = 0x00;
}

void gb_run(void) {
  uint32_t cycles = 0;
  
  while(cycles < MAX_CYCLES) {
    /* Execute next opcode and increase cycle count. */
    uint8_t t_cycles = z80_execute();
    cycles          += t_cycles;
    
    /* Update timers and graphics. */
    gb_update(t_cycles);
    
    /* Check for interrupts and handle them if necessary. */
    gb_check_intrs();
  }
}

/* Requests a given interrupt. */
void gb_intr(intr_t i) {
  IF |= i;
}

void gb_check_intrs(void) {
  /* Only service interrupts if interrupts are enabled. */
  if(IME) {
    /* Only service interrupts if there are actually any. */
    if(IF) {
      int i;
      
      /* Check each interrupt. */
      for(i = 0; i < 5; ++i) {
        if(TESTBIT(IE, i) && TESTBIT(IF, i)) {
          gb_service((1 << i));
        }
      }
    }
  }
}

/* Services an interrupt. */
void gb_service(intr_t i) {
  
  /* Disable interrupts and clear requested interrupt. */
  IME = 0;
  IF &= ~i;
  
  /* Preserve PC on the stack and jump to the interrupt
   * handler. */
  PUSHWORD(PC);
  
  switch(i) {
    case intr_vblank: PC = 0x40; break;
    case intr_lcd:    PC = 0x48; break;
    case intr_timer:  PC = 0x50; break;
    case intr_pad:    PC = 0x60; break;
  }
}

void gb_update(uint8_t cycles) {
  static int div_reg  = 0;
  
  /* Update division register. */
  div_reg += cycles;
  if(div_reg >= 255) {
    div_reg = 0;
    z80_memory[0xff04]++;
  }
  
  /* Only update the clock if it's active. */
  if(TESTBIT(TAC, 2)) {
    timer_counter -= cycles;
    
    /* If the timer has underflowed, update it. */
    if(timer_counter <= 0) {
      gb_set_clock();
      
      /* Request interrupt on timer overflow. */
      if(TIMA == 255) {
        TIMA = TMA;
        gb_intr(intr_timer);
      }
      
      else {
        ++TIMA;
      }
    }
  }
  
  /* Only update the LCD if it's active. */
  gb_set_lcd();
  if(TESTBIT(LCDC, 7)) {
    scanline += cycles;
    
    if(scanline >= 456) {
      scanline = 0;
      ++LY;
      
      /* We've entered VBlank. */
      if(LY == 144) {
        gb_intr(intr_vblank);
      }
      
      /* Reset scanline. */
      else if(LY > 153) LY = 0;
      
      /* Draw current scanline. */
      if(LY < 144) gb_draw_scanline();
    }
  }
}

/* Set the clock to a given frequency. */
void gb_set_clock() {
  switch(TAC & 0x3) {
    case 0: timer_counter = 1024; break;
    case 1: timer_counter = 16;   break;
    case 2: timer_counter = 64;   break;
    case 3: timer_counter = 256;  break;
  }
}

void gb_render_tiles() {
  uint8_t  y, i, WXD;
  uint16_t tile_addr = 0x8000;
  uint16_t bg_addr;
  int      use_window  = 0;
  int      signed_data = 0;
  
  /* If the window is enabled and lower/equal to the current scanline. */
  if(TESTBIT(LCDC, 5) && (WY <= LY)) use_window = 1;
  
  /* Calculate tile address. */
  if(!TESTBIT(LCDC, 4)) {
    tile_addr += 0x800;
    signed_data = 1;
  }
  
  /* Calculate background address. */
  bg_addr = TESTBIT(LCDC, use_window ? 6 : 3) ? 0x9c00 : 0x9800;
  
  /* Adjust window. */
  WXD = WX;
  
  /* Calculate y position. */
  y = use_window ? LY - WY : SCY + LY;
  //iprintf("LY: %u, y: %u\n", LY, y);
  //do { scanKeys(); } while(!(keysDown() & KEY_A));
  
  /* Draw scanline. */
  for(i = 0; i < 160; i += 8) {
    int      index;
    uint8_t  x, line, tile[2];
    uint16_t actual_addr;
    
    /* Calculate x position. */
    x = (use_window && (i >= WXD)) ? i - WXD : i + SCX;
    
    /* Find the given tile. */
    actual_addr = bg_addr + x/8 + (y/8 * 32);
    index       = signed_data ? (int8_t)GET8(actual_addr) + 128 : GET8(actual_addr);
    actual_addr = tile_addr + index * 16; /* Each tile is 16 bytes wide. */
    
    /* Calculate the line to render, and retrieve it. */
    line = (y % 8) * 2; /* Each line is 2 bytes wide. */
    tile[0] = GET8(actual_addr + line);
    tile[1] = GET8(actual_addr + line + 1);
    
    /* Render the tile. */
    gb_render_tile(i, y, tile);
  }
}

static void gb_render_tile(uint8_t x, uint8_t y, uint8_t tile[2]) {
  int     i;
  uint8_t color;
  
  for(i = 7; i >= 0; --i) {
    color = (BITVAL(tile[1], i) << 1) | BITVAL(tile[0], i);
    VRAM_A[(23+y)*SCREEN_WIDTH + x + (7 - i) + 47] = gb_get_color(color, BGP);
  }
}

static uint16_t gb_get_color(uint8_t color, uint8_t palette) {
  switch(color) {
    case 0: return RGB15(31, 31, 31);
    case 1: return RGB15(25, 25, 25);
    case 2: return RGB15(15, 15, 15);
    default:
    case 3: return RGB15(0,  0,  0);
  }
}

static void gb_draw_scanline() {
  /* Render tiles. */
  if(TESTBIT(LCDC, 0)) {
    printf("rendering tiles..\n");
    gb_render_tiles();
  }
  /* Render sprites. */
  /*if(TESTBIT(LCDC, 1)) gb_render_sprites();*/
}

static void gb_set_lcd() {
  int     cur_mode, next_mode, intr;
  
  if(!TESTBIT(LCDC, 7)) {
    LY    = 0;
    STAT &= 252;
    STAT |= BIT(0);
    return;
  }
  
  cur_mode  = STAT & 0x3;
  next_mode = 0;
  intr      = 0;
  
  /* Mode 1 (In VBlank). */
  if(LY >= 144) {
    next_mode = 1;
    STAT |= BIT(0);
    STAT &= ~BIT(1);
    intr  = TESTBIT(STAT, 4);
  }
  
  else {
    /* Mode 2 (Searching sprite attributes). */
    if(scanline >= MODE2_BOUND) {
      next_mode = 2;
      STAT |= BIT(1);
      STAT &= ~BIT(0);
      intr  = TESTBIT(STAT, 5);
    }
    
    /* Mode 3 (Transferring data). */
    else if(scanline >= MODE3_BOUND) {
      next_mode = 3;
      STAT |= BIT(1) | BIT(0);
    }
    
    /* Mode 0 (HBlank). */
    else {
      next_mode = 0;
      STAT &= ~(BIT(1) | BIT(0));
      intr  = TESTBIT(STAT, 3);
    }
  }
  
  /* Entered a new mode. Request interrupt. */
  if(intr && (cur_mode != next_mode)) {
    gb_intr(intr_lcd);
  }
  
  /* Check against comparison register. */
  if(LY == LYC) {
    STAT |= BIT(2);
    if(TESTBIT(STAT, 6))
      gb_intr(intr_lcd);
  }
  else {
    STAT &= ~BIT(2);
  }
}
