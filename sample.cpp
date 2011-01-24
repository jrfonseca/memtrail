#include <stdlib.h>
#include <stdio.h>

int
main(int argc, char *argv[])
{
   void *p;

   p = malloc(1024);
   free(p);

   p = calloc(1, 1024);
   free(p);

   posix_memalign(&p, 16, 1024);
   free(p);

   
   new char[512];

   malloc(1024);

   posix_memalign(&p, 64, 2048);

   printf("Should leak %lu bytes...\n", 512 + 1024 + 2048);

   return 0;
}
