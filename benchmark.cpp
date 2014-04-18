/**************************************************************************
 *
 * Copyright 2014 Jose Fonseca
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/


#include <assert.h>
#include <stdlib.h>
#include <stdio.h>


static size_t numAllocations = 256*1024;
static size_t allocationSize = 4;
static size_t leaked = 0;

#define NUM_FUNCTIONS 4

typedef void (*FunctionPointer)(const unsigned *i, unsigned n, bool b);

extern const FunctionPointer functionPointerss[NUM_FUNCTIONS];

#define FUNCTION(_index) \
   static void \
   fn##_index(const unsigned *indices, unsigned depth, bool leak) { \
      if (depth == 0) { \
         void * ptr = malloc(allocationSize); \
         if (!leak) { \
            free(ptr); \
         } else { \
            leaked += allocationSize; \
         } \
      } else { \
         --depth; \
         functionPointerss[indices[depth]](indices, depth, leak); \
      } \
   }

FUNCTION(0)
FUNCTION(1)
FUNCTION(2)
FUNCTION(3)


const FunctionPointer functionPointerss[NUM_FUNCTIONS] = {
   fn0,
   fn1,
   fn2,
   fn3
};


#define MAX_DEPTH 8


int
main(int argc, char *argv[])
{
   if (argc > 1) {
      numAllocations = atol(argv[1]);
      if (argc > 2) {
         allocationSize = atol(argv[2]);
      }
   }

   bool leak = false;
   for (unsigned i = 0; i < numAllocations; ++i) {
      unsigned indices[MAX_DEPTH];
      for (unsigned depth = 0; depth < MAX_DEPTH; ++depth) {
         // Random number, with non-uniform distribution
         unsigned index = rand() & 0xffff;
         index = (index * index) >> 16;
         index = (index * NUM_FUNCTIONS) >> 16;
         assert(index >= 0);
         assert(index < NUM_FUNCTIONS);

         indices[depth] = index;
      }

      leak = !leak;
      functionPointerss[indices[MAX_DEPTH - 1]](indices, MAX_DEPTH - 1, leak);
   }

   printf("Should leak %zu bytes...\n", leaked);

   return 0;
}


// vim:set sw=3 ts=3 et:
