#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <err.h>

int
main(int argc, const char *argv[])
{
   unsigned char trim = 0;

   if (argc > 1)
      trim = strtoul(argv[1], NULL, 10);

   bool leading = true;
   size_t rd, out_sz = 0, out_allocated = 0;
   char buf[4096], *out = NULL;
   while ((rd = fread(buf, 1, sizeof(buf), stdin))) {
      for (const char *s = buf; s < buf + rd; ++s) {
         if (*s == trim && leading)
            continue;

         if (out_sz >= out_allocated) {
            if (!(out = realloc(out, out_allocated += sizeof(buf))))
               err(EXIT_FAILURE, "realloc");
         }

         out[out_sz++] = *s;
         leading = false;
      }

      const char *s;
      for (s = out + (out_sz ? out_sz - 1 : 0); s > out && *s == trim; --s);

      const size_t to_write = (size_t)(s - out);
      if (fwrite(out, 1, to_write, stdout) != to_write)
         err(EXIT_FAILURE, "fwrite");

      memmove(out, s, (out_sz = out_sz - to_write));
   }

   free(out);
   return EXIT_SUCCESS;
}
