/**************************************************************************
 *
 * Copyright 2011 Jose Fonseca
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

#include <dlfcn.h>


size_t leaked = 0;


static void
test_dlsym(void)
{
   dlsym(RTLD_NEXT, "foo");
}


static void
test_malloc(void)
{
   void *p;

   // allocate some
   p = malloc(1024);

   // leak some
   malloc(1024);
   leaked += 1024;

   // free some
   free(p);

   free(NULL);
}


static void
test_calloc(void)
{
   void *p;

   // allocate some
   p = calloc(2, 1024);

   // leak some
   calloc(2, 1024);
   leaked += 2 * 1024;

   // free some
   free(p);
}


static void
test_posix_memalign(void)
{
   void *p;
   void *q;

   // allocate some
   posix_memalign(&p, 16, 1024);

   // leak some
   posix_memalign(&q, 16, 1024);
   leaked += 1024;

   // free some
   free(p);
}


static void
test_cxx(void)
{
   char *p;
   char *q;

   // allocate some
   p = new char;
   q = new char[512];

   // leak some
   new char;
   new char[512];
   leaked += 1 + 512;

   // free some
   delete p;
   delete [] q;
}


int
main(int argc, char *argv[])
{
   test_dlsym();
   test_malloc();
   test_calloc();
   test_posix_memalign();
   test_cxx();

   printf("Should leak %lu bytes...\n", leaked);

   return 0;
}
