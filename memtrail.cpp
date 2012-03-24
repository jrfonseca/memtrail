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


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <errno.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/limits.h> // PIPE_BUF

#include <execinfo.h>


#if __GNUC__ >= 4
   #define PUBLIC __attribute__ ((visibility("default")))
   #define PRIVATE __attribute__ ((visibility("hidden")))
#else
   #define PUBLIC
   #define PRIVATE
#endif


typedef void *(*malloc_ptr_t)(size_t size);
typedef void (*free_ptr_t)(void *ptr);

static malloc_ptr_t malloc_ptr = NULL;
static free_ptr_t free_ptr = NULL;


/**
 * calloc is called by dlsym, potentially causing infinite recursion, so
 * use a dummy malloc to prevent that. See also
 * http://blog.bigpixel.ro/2010/09/interposing-calloc-on-linux/
 */
static void *
dummy_malloc(size_t size)
{
   (void)size;
   return NULL;
}


static void
dummy_free(void *ptr)
{
   (void)ptr;
}


static inline void *
real_malloc(size_t size)
{
   if (!malloc_ptr) {
      malloc_ptr = &dummy_malloc;
      malloc_ptr = (malloc_ptr_t)dlsym(RTLD_NEXT, "malloc");
      if (!malloc_ptr) {
         return NULL;
      }
   }

   assert(malloc_ptr != &malloc);

   return malloc_ptr(size);
}


static inline void
real_free(void *ptr)
{
   if (!free_ptr) {
      free_ptr = &dummy_free;
      free_ptr = (free_ptr_t)dlsym(RTLD_NEXT, "free");
      if (!free_ptr) {
         return;
      }
   }

   assert(free_ptr != &free);

   free_ptr(ptr);
}


struct header_t {
   size_t size;
   void *ptr;
};


static pthread_mutex_t
mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static size_t total_size = 0;

static int fd = -1;



struct Module {
   const char *dli_fname;
   void       *dli_fbase;
};

static Module modules[128];
unsigned numModules = 0;

struct Symbol {
   void *addr;
   Module *module;
};

#define MAX_SYMBOLS 131071
static Symbol symbols[MAX_SYMBOLS];

#define ARRAY_SIZE(x) (sizeof (x) / sizeof ((x)[0]))


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
         ::write(_fd, _buf, _written);
         _written = 0;
      }
   }


   inline
   ~PipeBuf(void) {
      flush();
   }
};


static inline void
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

enum PIPE_FILE_DESCRIPTERS
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
      fprintf(stderr, "memtrail: error: could not fork\n");
      abort();

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


/**
 * Update/log changes to memory allocations.
 */
static inline void
_update(const void *ptr, ssize_t size) {
   pthread_mutex_lock(&mutex);
   static int recursion = 0;

   if (recursion++ <= 0) {
      void *addrs[10];
      size_t count = backtrace(addrs, ARRAY_SIZE(addrs));

      total_size += size;

      if (fd < 0) {
         mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
         fd = _gzopen("memtrail.data", O_WRONLY | O_CREAT | O_TRUNC, mode);

         if (fd < 0) {
            fprintf(stderr, "could not open memtrail.data\n");
            abort();
         }

      }

      PipeBuf buf(fd);

      buf.write(&ptr, sizeof ptr);
      buf.write(&size, sizeof size);


      unsigned char c = (unsigned char) count;
      buf.write(&c, 1);

      for (size_t i = 0; i < count; ++i) {
         void *addr = addrs[i];
         _lookup(buf, addr);
      }
   } else {
       //fprintf(stderr, "memtrail: warning: recursion\n");
   }
   --recursion;

   pthread_mutex_unlock(&mutex);
}


PUBLIC int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
   void *ptr;
   struct header_t *hdr;
   void *res;

   *memptr = NULL;

   if ((alignment & (alignment - 1)) != 0 ||
       (alignment & (sizeof(void*) - 1)) != 0) {
      return EINVAL;
   }

   ptr = real_malloc(alignment + sizeof *hdr + size);
   if (!ptr) {
      return -ENOMEM;
   }

   hdr = (struct header_t *)((((size_t)ptr + sizeof *hdr + alignment - 1) & ~(alignment - 1)) - sizeof *hdr);

   hdr->size = size;
   hdr->ptr = ptr;
   res = &hdr[1];
   assert(((size_t)res & (alignment - 1)) == 0);

   _update(res, size);

   *memptr = res;

   return 0;
}



static inline void *
_malloc(size_t size)
{
   struct header_t *hdr;
   void *res;

   hdr = (struct header_t *)real_malloc(sizeof *hdr + size);
   if (!hdr) {
      return NULL;
   }

   hdr->size = size;
   hdr->ptr = hdr;
   res = &hdr[1];

   _update(res, size);

   return res;
}

static inline void _free(void *ptr)
{
   struct header_t *hdr;

   if (!ptr) {
      return;
   }

   hdr = (struct header_t *)ptr - 1;

   _update(ptr, -hdr->size);

   real_free(hdr->ptr);
}


PUBLIC void *
malloc(size_t size)
{
   return _malloc(size);
}

PUBLIC void
free(void *ptr)
{
   _free(ptr);
}


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

#if 0
   if (hdr->size >= size) {
      _update(ptr, size - hdr->size);
      hdr->size = size;
      return ptr;
   }
#endif
  
   new_ptr = _malloc(size);
   if (new_ptr) {
      size_t min_size = hdr->size >= size ? size : hdr->size;
      memcpy(new_ptr, ptr, min_size);
      _free(ptr);
   }

   return new_ptr;
}


PUBLIC void *
operator new(size_t size) {
   return _malloc(size);
}


PUBLIC void *
operator new[] (size_t size) {
   return _malloc(size);
}


PUBLIC void
operator delete (void *ptr) {
   _free(ptr);
}


PUBLIC void
operator delete[] (void *ptr) {
   _free(ptr);
}


class Main
{
public:
   Main() {
      // Only trace the current process.
      unsetenv("LD_PRELOAD");
   }

   ~Main() {
      fprintf(stderr, "memtrail: %lu bytes leaked\n", total_size);
   }
};


static Main _main;


// vim:set sw=3 ts=3 et:
