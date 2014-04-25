/**************************************************************************
 *
 * Copyright 2011-2014 Jose Fonseca
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


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <malloc.h>
#include <errno.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/limits.h> // PIPE_BUF

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <new>


#if __GNUC__ >= 4
   #define PUBLIC __attribute__ ((visibility("default")))
   #define PRIVATE __attribute__ ((visibility("hidden")))
#else
   #define PUBLIC
   #define PRIVATE
#endif

#define ARRAY_SIZE(x) (sizeof (x) / sizeof ((x)[0]))


#define MAX_STACK 16
#define MAX_MODULES 128
#define MAX_SYMBOLS 131071


extern "C" void *__libc_malloc(size_t size);
extern "C" void __libc_free(void *ptr);


/**
 * Unlike glibc backtrace, libunwind will not invoke malloc.
 */
static int
libunwind_backtrace(unw_context_t *uc, void **buffer, int size)
{
   int count = 0;
   int ret;

   assert(uc != NULL);

   unw_cursor_t cursor;
   ret = unw_init_local(&cursor, uc);
   if (ret != 0) {
      return count;
   }

   while (count < size) {
      unw_word_t ip;
      ret = unw_get_reg(&cursor, UNW_REG_IP, &ip);
      if (ret != 0 || ip == 0) {
         break;
      }

      buffer[count++] = (void *)ip;

      ret = unw_step(&cursor);
      if (ret <= 0) {
         break;
      }
   }

   return count;
}


struct header_t {
   size_t size;
   void *ptr;
};


static pthread_mutex_t
mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static ssize_t
total_size = 0;

static ssize_t
max_size = 0;

static int fd = -1;



struct Module {
   const char *dli_fname;
   void       *dli_fbase;
};

static Module modules[MAX_MODULES];
static unsigned numModules = 0;

struct Symbol {
   void *addr;
   Module *module;
};

static Symbol symbols[MAX_SYMBOLS];



class PipeBuf
{
protected:
   int _fd;
   char _buf[PIPE_BUF];
   size_t _written;

public:
   inline
   PipeBuf(int fd) :
      _fd(fd),
      _written(0)
   {}

   inline void
   write(const void *buf, size_t nbytes) {
      if (nbytes) {
         assert(_written + nbytes <= PIPE_BUF);
         memcpy(_buf + _written, buf, nbytes);
         _written += nbytes;
      }
   }

   inline void
   flush(void) {
      if (_written) {
         ssize_t ret;
         ret = ::write(_fd, _buf, _written);
         assert(ret >= 0);
         assert((size_t)ret == _written);
         _written = 0;
      }
   }


   inline
   ~PipeBuf(void) {
      flush();
   }
};


static void
_lookup(PipeBuf &buf, void *addr) {
   unsigned key = (size_t)addr % MAX_SYMBOLS;

   Symbol *sym = &symbols[key];

   bool newModule = false;

   if (sym->addr != addr) {
      Dl_info info;
      if (dladdr(addr, &info)) {
         Module *module = NULL;
         for (unsigned i = 0; i < numModules; ++i) {
            if (strcmp(modules[i].dli_fname, info.dli_fname) == 0) {
               module = &modules[i];
               break;
            }
         }
         if (!module && numModules < ARRAY_SIZE(modules)) {
            module = &modules[numModules++];
            module->dli_fname = info.dli_fname;
            module->dli_fbase = info.dli_fbase;
            newModule = true;
         }
         sym->module = module;
      } else {
         sym->module = NULL;
      }

      sym->addr = addr;
   }

   size_t offset;
   const char * name;
   unsigned char moduleNo;

   if (sym->module) {
      name = sym->module->dli_fname;
      offset = ((size_t)addr - (size_t)sym->module->dli_fbase);
      moduleNo = 1 + sym->module - modules;
   } else {
      name = "";
      offset = (size_t)addr;
      moduleNo = 0;
   }

   buf.write(&addr, sizeof addr);
   buf.write(&offset, sizeof offset);
   buf.write(&moduleNo, sizeof moduleNo);
   if (newModule) {
      size_t len = strlen(name);
      buf.write(&len, sizeof len);
      buf.write(name, len);
   }
}


enum
{
   READ_FD  = 0,
   WRITE_FD = 1
};


/**
 * Open a compressed stream for writing by forking a gzip process.
 */
static int
_gzopen(const char *name, int oflag, mode_t mode)
{
   int       parentToChild[2];
   pid_t     pid;
   int out;
   int ret;

   ret = pipe(parentToChild);
   assert(ret == 0);

   pid = fork();
   switch (pid) {
   case -1:
      fprintf(stderr, "memtrail: warning could not fork\n");
      close(parentToChild[READ_FD]);
      close(parentToChild[WRITE_FD]);
      return open(name, oflag, mode);

   case 0:
      // child
      out = open(name, oflag, mode);

      ret = dup2(parentToChild[READ_FD], STDIN_FILENO);
      assert(ret != -1);
      ret = dup2(out, STDOUT_FILENO);
      assert(ret != -1);
      ret = close(parentToChild[WRITE_FD]);
      assert(ret == 0);

      // Don't want to track gzip
      unsetenv("LD_PRELOAD");

      execlp("gzip", "gzip", "--fast", NULL);

      // This line should never be reached
      abort();

   default:
      // parent
      ret = close(parentToChild[READ_FD]);
      assert(ret == 0);

      return parentToChild[WRITE_FD];
   }

   return -1;
}


static void
_open(void) {
   if (fd < 0) {
      mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
      fd = _gzopen("memtrail.data", O_WRONLY | O_CREAT | O_TRUNC, mode);

      if (fd < 0) {
         fprintf(stderr, "could not open memtrail.data\n");
         abort();
      }

      unsigned char c = sizeof(void *);
      ssize_t ret;
      ret = ::write(fd, &c, sizeof c);
      assert(ret >= 0);
      assert((size_t)ret == sizeof c);
   }
}


/**
 * Update/log changes to memory allocations.
 */
static void
_update(const void *ptr, ssize_t size, unw_context_t *uc) {
   pthread_mutex_lock(&mutex);
   static int recursion = 0;

   if (recursion++ <= 0) {
      total_size += size;

      if (total_size > max_size) {
         max_size = total_size;
      }

      _open();

      PipeBuf buf(fd);

      buf.write(&ptr, sizeof ptr);
      buf.write(&size, sizeof size);

      if (size > 0) {
         void *addrs[MAX_STACK];
         size_t count = libunwind_backtrace(uc, addrs, ARRAY_SIZE(addrs));

         unsigned char c = (unsigned char) count;
         buf.write(&c, 1);

         for (size_t i = 0; i < count; ++i) {
            void *addr = addrs[i];
            _lookup(buf, addr);
         }
      }
   } else {
       fprintf(stderr, "memtrail: warning: recursion\n");
   }
   --recursion;

   pthread_mutex_unlock(&mutex);
}


static void *
_memalign(size_t alignment, size_t size, unw_context_t *uc)
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

   hdr->size = size;
   hdr->ptr = ptr;
   res = &hdr[1];
   assert(((size_t)res & (alignment - 1)) == 0);

   _update(res, size, uc);

   return res;
}


static void *
_malloc(size_t size, unw_context_t *uc)
{
   struct header_t *hdr;
   void *res;

   hdr = (struct header_t *)__libc_malloc(sizeof *hdr + size);
   if (!hdr) {
      return NULL;
   }

   hdr->size = size;
   hdr->ptr = hdr;
   res = &hdr[1];

   _update(res, size, uc);

   return res;
}

static void
_free(void *ptr)
{
   struct header_t *hdr;

   if (!ptr) {
      return;
   }

   hdr = (struct header_t *)ptr - 1;

   _update(ptr, -hdr->size, NULL);

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

   unw_context_t uc;
   unw_getcontext(&uc);
   *memptr =  _memalign(alignment, size, &uc);
   if (!*memptr) {
      return -ENOMEM;
   }

   return 0;
}

extern "C"
PUBLIC void *
memalign(size_t alignment, size_t size)
{
   unw_context_t uc;
   unw_getcontext(&uc);
   return _memalign(alignment, size, &uc);
}

extern "C"
PUBLIC void *
valloc(size_t size)
{
   unw_context_t uc;
   unw_getcontext(&uc);
   return _memalign(sysconf(_SC_PAGESIZE), size, &uc);
}

extern "C"
PUBLIC void *
malloc(size_t size)
{
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
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
   unw_context_t uc;
   unw_getcontext(&uc);
   ptr = _malloc(nmemb * size, &uc);
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

   unw_context_t uc;
   unw_getcontext(&uc);

   if (!ptr) {
      return _malloc(size, &uc);
   }

   hdr = (struct header_t *)ptr - 1;

   new_ptr = _malloc(size, &uc);
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
   unw_context_t uc;
   unw_getcontext(&uc);
   char *ptr = (char *)_malloc(size, &uc);
   if (ptr) {
      memcpy(ptr, s, size);
   }
   return ptr;
}


static int
_vasprintf(char **strp, const char *fmt, va_list ap, unw_context_t *uc)
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

   *strp = (char *)_malloc(size, uc);
   if (!*strp) {
      return -1;
   }

   return vsnprintf(*strp, size, fmt, ap);
}

extern "C"
PUBLIC int
vasprintf(char **strp, const char *fmt, va_list ap)
{
   unw_context_t uc;
   unw_getcontext(&uc);
   return _vasprintf(strp, fmt, ap, &uc);
}

extern "C"
PUBLIC int
asprintf(char **strp, const char *format, ...)
{
   unw_context_t uc;
   unw_getcontext(&uc);
   int res;
   va_list ap;
   va_start(ap, format);
   res = _vasprintf(strp, format, ap, &uc);
   va_end(ap);
   return res;
}


/*
 * C++
 */

PUBLIC void *
operator new(size_t size) throw (std::bad_alloc) {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
}


PUBLIC void *
operator new[] (size_t size) throw (std::bad_alloc) {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
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
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
}


PUBLIC void *
operator new[] (size_t size, const std::nothrow_t&) throw () {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
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


__attribute__ ((constructor(101)))
static void
on_start(void)
{
   // Only trace the current process.
   unsetenv("LD_PRELOAD");
   _open();
}


__attribute__ ((destructor(101)))
static void
on_exit(void)
{
    fprintf(stderr, "memtrail: maximum %zi bytes\n", max_size);
    fprintf(stderr, "memtrail: leaked %zi bytes\n", total_size);

    // We don't close the fd here, just in case another destructor that deals
    // with memory gets called after us.
}


// vim:set sw=3 ts=3 et:
