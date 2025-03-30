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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>

#include <malloc.h>
#include <errno.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/limits.h> // PIPE_BUF
#include <link.h> // _r_debug, link_map

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <new>
#include <algorithm>

#include "list.h"


#define PUBLIC __attribute__ ((visibility("default")))
#define PRIVATE __attribute__ ((visibility("hidden")))

#define ARRAY_SIZE(x) (sizeof (x) / sizeof ((x)[0]))


#define RECORD 1

#define MAX_STACK 32
#define MAX_MODULES 128
#define MAX_SYMBOLS 131071


/* Minimum alignment for this platform */
#ifdef __x86_64__
#define MIN_ALIGN 16
#else
#define MIN_ALIGN (sizeof(double))
#endif


extern "C" void *__libc_malloc(size_t size);
extern "C" void __libc_free(void *ptr);


static void
_assert_fail(const char *expr,
             const char *file,
             unsigned line,
             const char *function)
{
   fprintf(stderr, "%s:%u:%s: Assertion `%s' failed.\n", file, line, function, expr);
   abort();
}


/**
 * glibc's assert macro invokes malloc, so roll our own to avoid recursion.
 */
#ifndef NDEBUG
#define assert(expr) ((expr) ? (void)0 : _assert_fail(#expr, __FILE__, __LINE__, __FUNCTION__))
#else
#define assert(expr) while (0) { (void)(expr) }
#endif


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


static char progname[PATH_MAX] = {0};


/**
 * Just like dladdr() but without holding lock.
 *
 * Calling dladdr() will dead-lock when another thread is doing dlopen() and
 * the newly loaded shared-object's global constructors call malloc.
 *
 * See also glibc/elf/rtld-debugger-interface.txt
 */
static int
_dladdr (const void *address, Dl_info *info) {
   struct link_map * lm = _r_debug.r_map;
   ElfW(Addr) addr = (ElfW(Addr)) address;

   /* XXX: we're effectively replacing odds of deadlocking with the odds of a
    * race condition when a new shared library is opened.  We should keep a
    * cache of this info to improve our odds.
    *
    * Another alternative would be to use /self/proc/maps
    */
   if (_r_debug.r_state != r_debug::RT_CONSISTENT) {
      fprintf(stderr, "memtrail: warning: inconsistent r_debug state\n");
   }

   if (0) fprintf(stderr, "0x%lx:\n", addr);

   assert(lm->l_prev == 0);
   while (lm->l_prev) {
      lm = lm->l_prev;
   }

   while (lm) {

      ElfW(Addr) l_addr;
      const char *l_name;
      if (lm->l_addr) {
         // Shared-object
         l_addr = lm->l_addr;
         l_name = lm->l_name;
      } else {
         // Main program
#if defined(__i386__)
         l_addr = 0x08048000;
#elif defined(__x86_64__)
         l_addr = 0x400000;
#elif defined(__aarch64__)
         l_addr = 0x400000;
#else
#error
#endif
         l_name = lm->l_name;
      }

      assert(l_name != nullptr);
      if (l_name[0] == 0 && lm == _r_debug.r_map) {
         // Determine the absolute path to progname
         if (progname[0] == 0) {
            size_t len = readlink("/proc/self/exe", progname, sizeof progname - 1);
            if (len <= 0) {
               strncpy(progname, program_invocation_name, PATH_MAX - 1);
               len = PATH_MAX - 1;
            }
            progname[len] = 0;
         }
         l_name = progname;
      }

      if (0) fprintf(stderr, "  0x%p, 0x%lx, %s\n", lm, l_addr, l_name);
      const ElfW(Ehdr) *l_ehdr = (const ElfW(Ehdr) *)l_addr;
      const ElfW(Phdr) *l_phdr = (const ElfW(Phdr) *)(l_addr + l_ehdr->e_phoff);
      for (int i = 0; i < l_ehdr->e_phnum; ++i) {
         if (l_phdr[i].p_type == PT_LOAD) {
            ElfW(Addr) start = lm->l_addr + l_phdr[i].p_vaddr;
            ElfW(Addr) stop = start + l_phdr[i].p_memsz;

            if (0) fprintf(stderr, "    0x%lx-0x%lx \n", start, stop);
            if (start <= addr && addr < stop) {
               info->dli_fname = l_name;
               info->dli_fbase = (void *)l_addr;
               info->dli_sname = NULL;
               info->dli_saddr = NULL;
               return 1;
            }
         }
      }

      lm = lm->l_next;
   }

   if (0) {
      int fd = open("/proc/self/maps", O_RDONLY);
      do {
         char buf[512];
         size_t nread = read(fd, buf, sizeof buf);
         if (!nread) {
            break;
         }
         ssize_t nwritten;
         nwritten = write(STDERR_FILENO, buf, nread);
         (void)nwritten;
      } while (true);
      close(fd);
   }

   return 0;
}


struct header_t {
   struct list_head list_head;

   // Real pointer
   void *ptr;

   // Size
   size_t size;

   unsigned allocated:1;
   unsigned pending:1;
   unsigned internal:1;

   unsigned char addr_count;
   void *addrs[MAX_STACK];
};


static pthread_mutex_t
mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static ssize_t
total_size = 0;

static ssize_t
max_size = 0;

static ssize_t
limit_size = SSIZE_MAX;

struct list_head
hdr_list = { &hdr_list, &hdr_list };

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
   {
      assert(fd >= 0);
   }

   inline void
   write(const void *buf, size_t nbytes) {
      if (!RECORD) {
         return;
      }

      if (nbytes) {
         assert(_written + nbytes <= PIPE_BUF);
         memcpy(_buf + _written, buf, nbytes);
         _written += nbytes;
      }
   }

   inline void
   flush(void) {
      if (!RECORD) {
         return;
      }

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
      if (_dladdr(addr, &info)) {
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
      fprintf(stderr, "memtrail: warning: could not fork\n");
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
         fprintf(stderr, "memtrail: error: could not open memtrail.data\n");
         abort();
      }

      unsigned char c = sizeof(void *);
      ssize_t ret;
      ret = ::write(fd, &c, sizeof c);
      assert(ret >= 0);
      assert((size_t)ret == sizeof c);
   }
}


static inline void
_log(struct header_t *hdr) {
   const void *ptr = hdr->ptr;
   ssize_t ssize = hdr->allocated ? (ssize_t)hdr->size : -(ssize_t)hdr->size;

   assert(ptr);
   assert(ssize);

   _open();

   PipeBuf buf(fd);
   buf.write(&ptr, sizeof ptr);
   buf.write(&ssize, sizeof ssize);

   if (hdr->allocated) {
      unsigned char c = (unsigned char) hdr->addr_count;
      buf.write(&c, 1);

      for (size_t i = 0; i < hdr->addr_count; ++i) {
         void *addr = hdr->addrs[i];
         _lookup(buf, addr);
      }
   }
}

static void
_flush(void) {
   struct header_t *it;
   struct header_t *tmp;
   for (it = (struct header_t *)hdr_list.next,
	     tmp = (struct header_t *)it->list_head.next;
        &it->list_head != &hdr_list;
	     it = tmp, tmp = (struct header_t *)tmp->list_head.next) {
      assert(it->pending);
      if (VERBOSITY >= 2) fprintf(stderr, "flush %p %zu\n", &it[1], it->size);
      if (!it->internal) {
         _log(it);
      }
      list_del(&it->list_head);
      if (!it->allocated) {
         __libc_free(it->ptr);
         it = nullptr;
      } else {
         it->pending = false;
      }
   }
}

static inline void
init(struct header_t *hdr,
     size_t size,
     void *ptr,
     unw_context_t *uc)
{
   hdr->ptr = ptr;
   hdr->size = size;
   hdr->allocated = true;
   hdr->pending = false;

   // Presume allocations created by libstdc++ before we initialized are
   // internal.  This is necessary to ignore its emergency_pool global.
   hdr->internal = fd == -1;

   if (RECORD) {
      hdr->addr_count = libunwind_backtrace(uc, hdr->addrs, ARRAY_SIZE(hdr->addrs));
   }
}


/**
 * Update/log changes to memory allocations.
 */
static inline void
_update(struct header_t *hdr,
        bool allocating = true)
{
   pthread_mutex_lock(&mutex);

   static int recursion = 0;

   if (recursion++ <= 0) {
      if (!allocating && max_size == total_size) {
         _flush();
      }

      hdr->allocated = allocating;
      ssize_t size = allocating ? (ssize_t)hdr->size : -(ssize_t)hdr->size;

      bool internal = hdr->internal;
      if (hdr->pending) {
         assert(!allocating);
         hdr->pending = false;
         list_del(&hdr->list_head);
         __libc_free(hdr->ptr);
         hdr = nullptr;
      } else {
         hdr->pending = true;
         list_add(&hdr->list_head, &hdr_list);
      }

      if (!internal) {
         if (size > 0 &&
             (total_size + size < total_size || // overflow
              total_size + size > limit_size)) {
            fprintf(stderr, "memtrail: warning: out of memory\n");
            _flush();
            _exit(1);
         }

         total_size += size;
         assert(total_size >= 0);

         if (total_size >= max_size) {
            max_size = total_size;
         }
      }
   } else {
      fprintf(stderr, "memtrail: warning: recursion\n");
      hdr->internal = true;

      assert(!hdr->pending);
      if (!hdr->pending) {
         if (!allocating) {
            __libc_free(hdr->ptr);
            hdr = nullptr;
         }
      }
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

   if (size == 0) {
      // Honour malloc(0), but allocate one byte for accounting purposes.
      ++size;
   }

   ptr = __libc_malloc(alignment + sizeof *hdr + size);
   if (!ptr) {
      return NULL;
   }

   hdr = (struct header_t *)((((size_t)ptr + sizeof *hdr + alignment - 1) & ~(alignment - 1)) - sizeof *hdr);

   init(hdr, size, ptr, uc);
   res = &hdr[1];
   assert(((size_t)res & (alignment - 1)) == 0);
   if (VERBOSITY >= 1) fprintf(stderr, "alloc %p %zu\n", res, size);

   _update(hdr);

   return res;
}


static inline void *
_malloc(size_t size, unw_context_t *uc)
{
   return _memalign(MIN_ALIGN, size, uc);
}

static void
_free(void *ptr)
{
   struct header_t *hdr;

   if (!ptr) {
      return;
   }

   hdr = (struct header_t *)ptr - 1;

   if (VERBOSITY >= 1) fprintf(stderr, "free %p %zu\n", ptr, hdr->size);

   _update(hdr, false);
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
aligned_alloc(size_t alignment, size_t size)
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
pvalloc(size_t size)
{
   unw_context_t uc;
   unw_getcontext(&uc);
   size_t pagesize = sysconf(_SC_PAGESIZE);
   return _memalign(pagesize, (size + pagesize - 1) & ~(pagesize - 1), &uc);
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

   unw_context_t uc;
   unw_getcontext(&uc);

   if (!ptr) {
      return _malloc(size, &uc);
   }

   if (!size) {
      _free(ptr);
      return NULL;
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
PUBLIC void *
reallocarray(void *ptr, size_t nmemb, size_t size)
{
   struct header_t *hdr;
   void *new_ptr;

   unw_context_t uc;
   unw_getcontext(&uc);

   if (nmemb && size) {
      size_t _size = nmemb * size;
      if (_size < size) {
         return NULL;
      }
      size = _size;
   } else {
      size = 0;
   }

   if (!ptr) {
      return _malloc(size, &uc);
   }

   if (!size) {
      _free(ptr);
      return NULL;
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


extern "C"
PUBLIC char *
strndup(const char *s, size_t n)
{
   size_t len = 0;
   while (n && s[len]) {
      ++len;
      --n;
   }

   unw_context_t uc;
   unw_getcontext(&uc);
   char *ptr = (char *)_malloc(len + 1, &uc);
   if (ptr) {
      memcpy(ptr, s, len);
      ptr[len] = 0;
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
 *
 * See also the output of
 *
 *   nm -D --defined-only /lib/x86_64-linux-gnu/libstdc++.so.6 | grep '\<_Z[dn]' | c++filt
 */

PUBLIC void *
operator new(size_t size) noexcept(false) {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
}


PUBLIC void *
operator new[] (size_t size) noexcept(false) {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
}


PUBLIC void
operator delete (void *ptr) noexcept {
   _free(ptr);
}


PUBLIC void
operator delete[] (void *ptr) noexcept {
   _free(ptr);
}


PUBLIC void *
operator new(size_t size, const std::nothrow_t&) noexcept {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
}


PUBLIC void *
operator new[] (size_t size, const std::nothrow_t&) noexcept {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _malloc(size, &uc);
}


PUBLIC void
operator delete (void *ptr, const std::nothrow_t&) noexcept {
   _free(ptr);
}


PUBLIC void
operator delete[] (void *ptr, const std::nothrow_t&) noexcept {
   _free(ptr);
}


PUBLIC void *
operator new(size_t size, std::align_val_t al) noexcept(false) {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _memalign(static_cast<size_t>(al), size, &uc);
}


PUBLIC void *
operator new[] (size_t size, std::align_val_t al) noexcept(false) {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _memalign(static_cast<size_t>(al), size, &uc);
}


PUBLIC void
operator delete (void *ptr, std::align_val_t al) noexcept {
   _free(ptr);
}


PUBLIC void
operator delete[] (void *ptr, std::align_val_t al) noexcept {
   _free(ptr);
}


PUBLIC void *
operator new(size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _memalign(static_cast<size_t>(al), size, &uc);
}


PUBLIC void *
operator new[] (size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
   unw_context_t uc;
   unw_getcontext(&uc);
   return _memalign(static_cast<size_t>(al), size, &uc);
}


PUBLIC void
operator delete (void *ptr, std::align_val_t al, const std::nothrow_t&) noexcept {
   _free(ptr);
}


PUBLIC void
operator delete[] (void *ptr, std::align_val_t al, const std::nothrow_t&) noexcept {
   _free(ptr);
}


/*
 * Snapshot.
 */


static size_t last_snapshot_size = 0;
static unsigned snapshot_no = 0;

extern "C"
PUBLIC void
memtrail_snapshot(void) {
   pthread_mutex_lock(&mutex);

   _flush();

   _open();

   static const void *ptr = NULL;
   static const ssize_t size = 0;
   PipeBuf buf(fd);
   buf.write(&ptr, sizeof ptr);
   buf.write(&size, sizeof size);

   size_t current_total_size = total_size;
   size_t current_delta_size;
   if (snapshot_no)
      current_delta_size = current_total_size - last_snapshot_size;
   else
      current_delta_size = 0;
   last_snapshot_size = current_total_size;

   ++snapshot_no;

   pthread_mutex_unlock(&mutex);

   fprintf(stderr, "memtrail: snapshot %zi bytes (%+zi bytes)\n", current_total_size, current_delta_size);
}


extern "C" void _IO_doallocbuf(FILE *ptr);


/*
 * Constructor/destructor
 */

__attribute__ ((constructor(101)))
static void
on_start(void)
{
   // Only trace the current process.
   unsetenv("LD_PRELOAD");

   _IO_doallocbuf(stdin);
   _IO_doallocbuf(stdout);
   _IO_doallocbuf(stderr);

   dlsym(RTLD_NEXT, "printf");

   _open();

   // Abort when the application allocates half of the physical memory, to
   // prevent the system from slowing down to a halt due to swapping
   long pagesize = sysconf(_SC_PAGESIZE);
   long phys_pages = sysconf(_SC_PHYS_PAGES);
   limit_size = (ssize_t) std::min((intmax_t) phys_pages / 2, SSIZE_MAX / pagesize) * pagesize;
   fprintf(stderr, "memtrail: limiting to %zi bytes\n", limit_size);
}


__attribute__ ((destructor(101)))
static void
on_exit(void)
{
   pthread_mutex_lock(&mutex);
   _flush();
   size_t current_max_size = max_size;
   size_t current_total_size = total_size;
   pthread_mutex_unlock(&mutex);

   fprintf(stderr, "memtrail: maximum %zi bytes\n", current_max_size);
   fprintf(stderr, "memtrail: leaked %zi bytes\n", current_total_size);

   // We don't close the fd here, just in case another destructor that deals
   // with memory gets called after us.
}


// vim:set sw=3 ts=3 et:
