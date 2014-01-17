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
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <malloc.h>
#include <errno.h>
#include <dlfcn.h>
#include <unistd.h>

#include <new>


#if __GNUC__ >= 4
   #define PUBLIC __attribute__ ((visibility("default")))
   #define PRIVATE __attribute__ ((visibility("hidden")))
#else
   #define PUBLIC
   #define PRIVATE
#endif


extern "C" void *__libc_malloc(size_t size);
extern "C" void __libc_free(void *ptr);


struct header_t {
   size_t size;
   void *ptr;
};


static size_t
total_size = 0;

static size_t
max_size = 0;


static inline void *
_memalign(size_t alignment, size_t size)
{
   void *ptr;
   struct header_t *hdr;
   void *res;

   if ((alignment & (alignment - 1)) != 0 ||
       (alignment & (sizeof(void*) - 1)) != 0) {
      return NULL;
   }

   ptr = __libc_malloc(alignment + sizeof *hdr + size);
   if (!ptr) {
      return NULL;
   }

   hdr = (struct header_t *)((((size_t)ptr + sizeof *hdr + alignment - 1) & ~(alignment - 1)) - sizeof *hdr);

   total_size += size;
   if (total_size > max_size) max_size = total_size;
   hdr->size = size;
   hdr->ptr = ptr;
   res = &hdr[1];
   assert(((size_t)res & (alignment - 1)) == 0);

   return res;
}


static inline void *
_malloc(size_t size)
{
   struct header_t *hdr;
   void *res;

   hdr = (struct header_t *)__libc_malloc(sizeof *hdr + size);
   if (!hdr) {
      return NULL;
   }

   total_size += size;
   if (total_size > max_size) max_size = total_size;
   hdr->size = size;
   hdr->ptr = hdr;
   res = &hdr[1];

   return res;
}

static inline void _free(void *ptr)
{
   struct header_t *hdr;

   if (!ptr) {
      return;
   }

   hdr = (struct header_t *)ptr - 1;

   total_size -= hdr->size;

   __libc_free(hdr->ptr);
}


/*
 * C
 */

extern "C"
PUBLIC int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
   *memptr = NULL;

   if ((alignment & (alignment - 1)) != 0 ||
       (alignment & (sizeof(void*) - 1)) != 0) {
      return EINVAL;
   }

   *memptr =  _memalign(alignment, size);
   if (!*memptr) {
      return -ENOMEM;
   }

   return 0;
}

extern "C"
PUBLIC void *
memalign(size_t alignment, size_t size)
{
   return _memalign(alignment, size);
}

extern "C"
PUBLIC void *
valloc(size_t size)
{
   return _memalign(sysconf(_SC_PAGESIZE), size);
}

extern "C"
PUBLIC void *
malloc(size_t size)
{
   return _malloc(size);
}

extern "C"
PUBLIC void
free(void *ptr)
{
   _free(ptr);
}


extern "C"
PUBLIC void *
calloc(size_t nmemb, size_t size)
{
   void *ptr;
   ptr = _malloc(nmemb * size);
   if (ptr) {
      memset(ptr, 0, nmemb * size);
   }
   return ptr;
}


extern "C"
PUBLIC void
cfree(void *ptr)
{
   _free(ptr);
}


extern "C"
PUBLIC void *
realloc(void *ptr, size_t size)
{
   struct header_t *hdr;
   void *new_ptr;

   if (!size) {
      _free(ptr);
      return NULL;
   }

   if (!ptr) {
      return _malloc(size);
   }

   hdr = (struct header_t *)ptr - 1;

   new_ptr = _malloc(size);
   if (new_ptr) {
      size_t min_size = hdr->size >= size ? size : hdr->size;
      memcpy(new_ptr, ptr, min_size);
      _free(ptr);
   }

   return new_ptr;
}


extern "C"
PUBLIC char *
strdup(const char *s)
{
   size_t size = strlen(s) + 1;
   char *ptr = (char *)_malloc(size);
   if (ptr) {
      memcpy(ptr, s, size);
   }
   return ptr;
}


extern "C"
PUBLIC int
vasprintf(char **strp, const char *fmt, va_list ap)
{
   size_t size;

   {
      va_list ap_copy;
      va_copy(ap_copy, ap);

      char junk;
      size = vsnprintf(&junk, 1, fmt, ap_copy);
      assert(size >= 0);

      va_end(ap_copy);
   }

   *strp = (char *)_malloc(size);
   if (!*strp) {
      return -1;
   }

   return vsnprintf(*strp, size, fmt, ap);
}

extern "C"
PUBLIC int
asprintf(char **strp, const char *format, ...)
{
   int res;
   va_list ap;
   va_start(ap, format);
   res = vasprintf(strp, format, ap);
   va_end(ap);
   return res;
}


/*
 * C++
 */

PUBLIC void *
operator new(size_t size) throw (std::bad_alloc) {
   return _malloc(size);
}


PUBLIC void *
operator new[] (size_t size) throw (std::bad_alloc) {
   return _malloc(size);
}


PUBLIC void
operator delete (void *ptr) throw () {
   _free(ptr);
}


PUBLIC void
operator delete[] (void *ptr) throw () {
   _free(ptr);
}


PUBLIC void *
operator new(size_t size, const std::nothrow_t&) throw () {
   return _malloc(size);
}


PUBLIC void *
operator new[] (size_t size, const std::nothrow_t&) throw () {
   return _malloc(size);
}


PUBLIC void
operator delete (void *ptr, const std::nothrow_t&) throw () {
   _free(ptr);
}


PUBLIC void
operator delete[] (void *ptr, const std::nothrow_t&) throw () {
   _free(ptr);
}


/*
 * Constructor/destructor
 */

class Main
{
public:
   Main() {
      // Only trace the current process.
      unsetenv("LD_PRELOAD");
   }

   ~Main() {
      fprintf(stderr, "leakcount: maximum %lu bytes\n", max_size);
      fprintf(stderr, "leakcount: leaked %lu bytes\n", total_size);
   }
};


static Main _main;


// vim:set sw=3 ts=3 et:
