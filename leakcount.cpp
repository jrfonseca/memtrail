#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <errno.h>
#include <dlfcn.h>

typedef void *(*calloc_ptr_t)(size_t nmemb, size_t size);
typedef void *(*malloc_ptr_t)(size_t size);
typedef void (*free_ptr_t)(void *ptr);
typedef void *(*realloc_ptr_t)(void *ptr, size_t size);

static malloc_ptr_t malloc_ptr = NULL;
static calloc_ptr_t calloc_ptr = NULL;
static free_ptr_t free_ptr = NULL;
static realloc_ptr_t realloc_ptr = NULL;

struct header_t {
   size_t size;
   void *ptr;
};

static size_t total_size = 0;

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
   void *ptr;
   struct header_t *hdr;

   *memptr = NULL;

   if (alignment & (alignment - 1) != 0 ||
       alignment & (sizeof(void*) - 1) != 0) {
      return EINVAL;
   }

   if (!malloc_ptr) {
      malloc_ptr = (malloc_ptr_t)dlsym(RTLD_NEXT, "malloc");
      if (!malloc_ptr) {
	 return -ENOMEM;
      }
   }

   ptr = malloc_ptr(alignment + sizeof *hdr + size);
   if (!ptr) {
      return -ENOMEM;
   }

   hdr = (struct header_t *)(((size_t)ptr + sizeof *hdr + alignment - 1) & ~(alignment - 1));

   total_size += size;
   hdr->size = size;
   hdr->ptr = ptr;

   *memptr = &hdr[1];

   return 0;
}



static inline void *_malloc(size_t size)
{
   struct header_t *hdr;
   static unsigned reentrant = 0;

   if (!malloc_ptr) {
      if (reentrant) {
	 return NULL;
      }
      ++reentrant;
      malloc_ptr = (malloc_ptr_t)dlsym(RTLD_NEXT, "malloc");
      if (!malloc_ptr) {
         return NULL;
      }
      assert(malloc_ptr != &malloc);
   }

   hdr = (struct header_t *)malloc_ptr(sizeof *hdr + size);
   if (!hdr) {
      return NULL;
   }

   total_size += size;
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

   if (!free_ptr) {
      free_ptr = (free_ptr_t)dlsym(RTLD_NEXT, "free");
      if (!free_ptr) {
         return;
      }
   }

   free_ptr(hdr->ptr);
}


void *malloc(size_t size)
{
   return _malloc(size);
}

void free(void *ptr)
{
   _free(ptr);
}


/**
 * calloc is called by dlsym, potentially causing infinite recursion, per
 * http://blog.bigpixel.ro/2010/09/interposing-calloc-on-linux/ . By
 * implementing calloc in terms of malloc we avoid that, and have a simpler
 * implementation.
 */
void *calloc(size_t nmemb, size_t size)
{
   void *ptr;
   ptr = _malloc(nmemb * size);
   if (ptr) {
      memset(ptr, 0, nmemb * size);
   }
   return ptr;
}


void *realloc(void *ptr, size_t size)
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


void* operator new(size_t size) {
   return _malloc(size);
}


void* operator new[] (size_t size) {
   return _malloc(size);
}


void operator delete (void *ptr) {
   _free(ptr);
}


void operator delete[] (void *ptr) {
   _free(ptr);
}


class LeakCount
{
public:
   ~LeakCount() {
      fprintf(stderr, "leakcount: %lu bytes leaked\n", total_size);
   }
};

static LeakCount lq;
