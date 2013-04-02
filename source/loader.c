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
#include <stdio.h>

#include "gb.h"
#include "loader.h"

#define die(msg) do { \
  iprintf("error: %s\n", msg); \
  exit(EXIT_FAILURE); \
  } while(0)

void load_file(const char *name) {
  FILE         *f = fopen(name, "rb");
  unsigned int  i;
  
  if(!f)
    die("failed to open file");

  /* Read in first ROM bank. */
  if(fread(z80_memory, 1, 0x8000, f) != 0x8000)
    die("unexpected end of file loading initial bank");
  
  /* Allocate space for the rest of the ROM banks. */
  bank_count = z80_memory[0x148];
  banks      = malloc(sizeof *banks * (bank_count + 1));
  if(!banks)
    die("failed to allocate enough memory banks.");
  
  iprintf("allocated for %u extra banks\n", bank_count);
  
  /* Read in all of the banks. */
  fseek(f, 0x3fff, SEEK_SET);
  for(i = 0; i < bank_count + 1; ++i) {
    size_t sz = fread(banks[i], 1, sizeof banks[i], f);
    if(sz != sizeof banks[i]) {
      iprintf("size of read chunk: %u\n", sz);
      iprintf("position in file: %lu\n", ftell(f));
      die("unexpected end of file loading auxiliary banks");
    }
    iprintf("loaded bank.\n");
  }
  
  iprintf("loaded %u banks.\n", bank_count + 2);
  
  fclose(f);
}

void load_adapter(void) {
}
