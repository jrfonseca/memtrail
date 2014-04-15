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


#include <stdlib.h>
#include <stdio.h>


size_t count = 256*1024;
size_t size = 4;


static void
fn0(void)
{
   for (size_t i = 0; i < count; ++i) {
      void * ptr = malloc(size);
      if (i % 2) {
         free(ptr);
      }
   }
}


static void
fn1(void)
{
   fn0();
}


static void
fn2(void)
{
   fn1();
}


static void
fn3(void)
{
   fn2();
}


int
main(int argc, char *argv[])
{
   if (argc > 1) {
      count = atol(argv[1]);
      if (argc > 2) {
         size = atol(argv[2]);
      }
   }

   fn3();

   printf("Should leak %zu bytes...\n", count * size);

   return 0;
}


// vim:set sw=3 ts=3 et:
