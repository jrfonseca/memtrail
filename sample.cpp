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


#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <dlfcn.h>

#include "memtrail.h"


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

   // allocate 0 bytes
   p = malloc(0);
   assert(p);
   free(p);

   // free nothing
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

   // allocate 0 bytes
   p = calloc(0, 1);
   assert(p);
   free(p);
   p = calloc(1, 0);
   assert(p);
   free(p);
}


static void
test_realloc(void)
{
   void *p;

   // allocate some
   p = realloc(NULL, 1024);

   // grow some
   p = realloc(p, 2048);

   // free some
   p = realloc(p, 0);
   assert(!p);

   // allocate 0 bytes
   p = realloc(NULL, 0);
   assert(p);
   p = realloc(p, 0);
   assert(!p);
}


static void
test_memalign(void)
{
   void *p;
   void *q;

   // allocate some
   posix_memalign(&p, 16, 1024);
   assert(((size_t)p & 15) == 0);

   // leak some
   posix_memalign(&q, 4096, 1024);
   assert(((size_t)q & 4095) == 0);
   leaked += 1024;

   // free some
   free(p);

   // allocate 0 bytes
   posix_memalign(&p, sizeof (void*), 0);
   assert(p);
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


static void
test_string(void)
{
   char *p;
   int n;

   p = strdup("foo");
   free(p);

   p = NULL;
   n = asprintf(&p, "%u", 12345);
   assert(n == 5);

   free(p);
}


static void
test_subprocess(void)
{
   const char *ld_preload = getenv("LD_PRELOAD");
   assert(ld_preload == NULL || strstr(ld_preload, "memtrail.so") == NULL);

   system("env | grep LD_PRELOAD");
}


static void
test_snapshot(void)
{
   memtrail_snapshot();
   malloc(64);
   leaked += 64;
   memtrail_snapshot();
}


class TestGlobal
{
public:
   void *p;

   TestGlobal() {
      malloc(512);
      leaked += 512;

      p = malloc(256);
   }

   ~TestGlobal() {
      free(p);

      malloc(64);
      leaked += 64;

      printf("Should leak %zu bytes...\n", leaked);
   }
};

static TestGlobal test_global;


static void
test_atexit(void)
{
   malloc(32);
   leaked += 32;
}


int
main(int argc, char *argv[])
{
   test_dlsym();
   test_malloc();
   test_calloc();
   test_realloc();
   test_memalign();
   test_cxx();
   test_string();
   test_subprocess();
   test_snapshot();

   atexit(test_atexit);

   return 0;
}


// vim:set sw=3 ts=3 et:
