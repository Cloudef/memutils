#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <util.h>
#include <err.h>

static bool FOUND = false;

static void
usage(const char *argv0)
{
   fprintf(stderr, "usage: %s needle first [window-size] < haystack\n"
                   "       %s needle all [window-size] < haystack\n", argv0, argv0);
   exit(EXIT_FAILURE);
}

static const char*
search(const char *haystack, const char *needle, const size_t window_size)
{
   for (const char *s = haystack; s < haystack + window_size; ++s) {
      if (memcmp(s, needle, window_size))
         continue;
      return s;
   }
   return NULL;
}

static void
search_and_print_if_match(const char *haystack, const char *needle, const size_t window_size, const size_t offset, const bool exit_on_match)
{
   const char *match;
   if ((match = search(haystack, needle, window_size))) {
      FOUND = true;
      printf("%zu\n", offset + match - haystack);
      if (exit_on_match) {
         free((void*)needle);
         free((void*)haystack);
         exit(EXIT_SUCCESS);
      }
   }
}

int
main(int argc, const char *argv[])
{
   // default incase failure, or cant get size of file
   size_t window_size = 4096 * 1024;
   bool has_window_size = false;

   if (argc < 3)
      usage(argv[0]);

   enum {
      FIRST,
      ALL
   } mode;

   if (!strcmp(argv[2], "first"))
      mode = FIRST;
   else if (!strcmp(argv[2], "all"))
      mode = ALL;
   else
      errx(EXIT_FAILURE, "mode must be first or all");

   if (argc > 3) {
      window_size = hexdecstrtoull(argv[3], NULL);
      has_window_size = true;
   }

   FILE *f;
   if (!(f = fopen(argv[1], "rb")))
      err(EXIT_FAILURE, "fopen(%s)", argv[1]);

   if (!has_window_size) {
      fseek(f, 0, SEEK_END);
      const long tell = ftell(f);

      if (tell < 0)
         warnx("can't figure out the size of a needle, not a normal file? fallbacking to a window size of %zu bytes", window_size);
      else
         window_size = tell;

      fseek(f, 0, SEEK_SET);
   }

   char *needle;
   if (!(needle = malloc(window_size)))
      err(EXIT_FAILURE, "malloc");

   window_size = fread(needle, 1, window_size, f);
   fclose(f);

   char *haystack;
   if (!(haystack = calloc(2, window_size)))
         err(EXIT_FAILURE, "calloc");

   size_t rd = 0, offset = 0;
   while ((rd = fread(haystack + window_size * !!offset, 1, window_size * (1 + !offset), stdin))) {
      search_and_print_if_match(haystack, needle, (rd >= window_size ? window_size : 0), offset, (mode == FIRST));
      offset += window_size;
      memmove(haystack, haystack + window_size, window_size);
   }

   search_and_print_if_match(haystack, needle, (rd >= window_size ? window_size : 0), offset, (mode == FIRST));
   free(needle);
   free(haystack);
   return (FOUND ? EXIT_SUCCESS : EXIT_FAILURE);
}
