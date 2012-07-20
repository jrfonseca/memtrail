#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <errno.h>
#include <dlfcn.h>

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

static ssize_t
total_size = 0;

static ssize_t
max_size = 0;

PUBLIC int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
   void *ptr;
   struct header_t *hdr;

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

   total_size += size;
   if (total_size > max_size) max_size = total_size;
   hdr->size = size;
   hdr->ptr = ptr;

   *memptr = &hdr[1];

   assert(((size_t)*memptr & (alignment - 1)) == 0);

   return 0;
}



static inline void *_malloc(size_t size)
{
   struct header_t *hdr;

   hdr = (struct header_t *)real_malloc(sizeof *hdr + size);
   if (!hdr) {
      return NULL;
   }

   total_size += size;
   if (total_size > max_size) max_size = total_size;
   hdr->size = size;
   hdr->ptr = hdr;
   return &hdr[1];
}

static inline void _free(void *ptr)
{
   struct header_t *hdr;

   if (!ptr) {
      return;
   }

   hdr = (struct header_t *)ptr - 1;

   total_size -= hdr->size;

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
      free(ptr);
      return NULL;
   }

   if (!ptr) {
      return _malloc(size);
   }

   hdr = (struct header_t *)ptr - 1;

   if (hdr->size >= size) {
      total_size -= hdr->size - size;
      hdr->size = size;
      return ptr;
   }
  
   new_ptr = _malloc(size);
   if (new_ptr) {
      memcpy(new_ptr, ptr, size);
      free(ptr);
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
      fprintf(stderr, "leakcount: maximum %lu bytes\n", max_size);
      fprintf(stderr, "leakcount: leaked %lu bytes\n", total_size);
   }
};

static Main _main;
