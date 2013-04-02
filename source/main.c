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

#include <fat.h>
#include <nds.h>
#include <stdio.h>

#include "gb.h"
#include "loader.h"
#include "z80.h"

int sstep = 0;

int main(void) {
  /* Initialize framebuffer mode. */
  videoSetMode(MODE_FB0);
  vramSetBankA(VRAM_A_LCD);
    
  /* Initialize console. */
  consoleDemoInit();
  
  /* Initialize output buffer to clear white. */
  {
    int x, y;
    for(y = 23; y < (23 + 144); ++y)
      for(x = 47; x < (160 + 47); ++x)
        VRAM_A[y*SCREEN_WIDTH + x] = RGB15(31, 31, 31);
  }
  
  /* Initialize Gameboy. */
  gb_init();
  
  /* Initialize FAT. */
  fatInitDefault();
  load_file("test.gb");
  
  /* Print version information to console. */
  iprintf("Orchard v0.1\n");
  iprintf("by Forest Belton (c) 2010\n");
  
  /* Execute loop. */
  while(1) {
    gb_run();
    
    scanKeys();
    if(keysDown() & KEY_L) sstep ^= 1;
    
    swiWaitForVBlank();
  }
  return 0;
}
